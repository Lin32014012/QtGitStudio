#include "GitBranchService.h"
#include "GitCommandExecutor.h"

namespace {
GitError asyncError(const QString &context, const GitCommandExecutor::Result &result) {
    GitError error = result.error;
    error.type = GitErrorType::ProcessFailed;
    const QString stderrOutput = QString::fromUtf8(result.errorOutput);
    error.message = context + stderrOutput;
    error.exitCode = result.exitCode;
    error.stderrOutput = stderrOutput;
    return error;
}

bool switchCommandUnavailable(const QString &stderrOutput) {
    return stderrOutput.contains("not a git command", Qt::CaseInsensitive) ||
        stderrOutput.contains("unknown subcommand", Qt::CaseInsensitive);
}
}

GitBranchService::GitBranchService(QObject *parent) : QObject(parent) {
    m_executor = new GitCommandExecutor(this);
}

GitBranchService::~GitBranchService() {}

void GitBranchService::setWorkingDirectory(const QString &dir) {
    ++m_repositoryGeneration;
    m_executor->setWorkingDirectory(dir);
}

QStringList GitBranchService::branches() {
    m_lastError = GitError();
    QStringList result;
    
    int exitCode = 0;
    QString output;
    QString stderrOutput;
    
    bool ok = runGit({"branch", "--format=%(refname:short)"}, &exitCode, &output, &stderrOutput);
    
    if (!ok || exitCode != 0) {
        m_lastError.type = GitErrorType::ProcessFailed;
        m_lastError.message = "Failed to list branches: " + stderrOutput;
        return result;
    }
    
    QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            result.append(trimmed);
        }
    }
    
    return result;
}

QString GitBranchService::currentBranch() {
    m_lastError = GitError();
    
    int exitCode = 0;
    QString output;
    QString stderrOutput;
    
    bool ok = runGit({"branch", "--show-current"}, &exitCode, &output, &stderrOutput);
    
    if (!ok || exitCode != 0) {
        m_lastError.type = GitErrorType::ProcessFailed;
        m_lastError.message = "Failed to get current branch: " + stderrOutput;
        return QString();
    }
    
    QString branch = output.trimmed();
    if (branch.isEmpty()) {
        // Empty repositories have no branch yet (HEAD is unborn);
        // detached HEAD also returns empty here. Distinguish via the
        // symbolic ref so empty repos show their intended branch name.
        const QString head = headBranchName();
        branch = head.isEmpty() ? QStringLiteral("(detached)") : head;
    }
    
    return branch;
}

GitBranchService::RepoState GitBranchService::repoState() {
    int exitCode = 0;
    QString stderrOutput;
    // Probe 1: does HEAD resolve to a commit?
    const bool headResolves =
        runGit({"rev-parse", "--verify", "-q", "HEAD"}, &exitCode, nullptr, &stderrOutput)
        && exitCode == 0;
    // Probe 2: does HEAD point at a (possibly unborn) branch?
    const QString head = headBranchName();

    if (headResolves && !head.isEmpty()) {
        return RepoState::Normal;
    }
    if (headResolves && head.isEmpty()) {
        return RepoState::DetachedHead;
    }
    if (!headResolves && !head.isEmpty()) {
        return RepoState::EmptyRepository;
    }
    return RepoState::NoBranch;
}

QString GitBranchService::headBranchName() {
    int exitCode = 0;
    QString output;
    QString stderrOutput;
    const bool ok =
        runGit({"symbolic-ref", "-q", "--short", "HEAD"}, &exitCode, &output, &stderrOutput);
    if (!ok || exitCode != 0) {
        return QString();
    }
    return output.trimmed();
}

bool GitBranchService::createBranch(const QString &name) {
    return createBranch(name, {}, false);
}

bool GitBranchService::createBranch(
    const QString &name,
    const QString &startPoint,
    bool checkoutAfterCreation) {
    m_lastError = GitError();
    
    if (name.isEmpty()) {
        m_lastError.type = GitErrorType::ProcessFailed;
        m_lastError.message = "Branch name cannot be empty";
        return false;
    }
    
    int exitCode = 0;
    QString stderrOutput;

    bool ok = false;
    if (checkoutAfterCreation) {
        QStringList arguments{"switch", "-c", name};
        if (!startPoint.isEmpty()) {
            arguments.append(startPoint);
        }
        ok = runGit(arguments, &exitCode, nullptr, &stderrOutput);
        if ((!ok || exitCode != 0) && switchCommandUnavailable(stderrOutput)) {
            arguments = {"checkout", "-b", name};
            if (!startPoint.isEmpty()) {
                arguments.append(startPoint);
            }
            ok = runGit(arguments, &exitCode, nullptr, &stderrOutput);
        }
    } else {
        QStringList arguments{"branch", "--", name};
        if (!startPoint.isEmpty()) {
            arguments.append(startPoint);
        }
        ok = runGit(arguments, &exitCode, nullptr, &stderrOutput);
    }
    
    if (!ok || exitCode != 0) {
        m_lastError.type = GitErrorType::ProcessFailed;
        m_lastError.message = "Failed to create branch: " + stderrOutput;
        m_lastError.exitCode = exitCode;
        m_lastError.stderrOutput = stderrOutput;
        return false;
    }
    
    return true;
}

bool GitBranchService::deleteBranch(const QString &name, bool force) {
    m_lastError = GitError();
    m_lastDeleteRejectedAsUnmerged = false;
    
    if (name.isEmpty()) {
        m_lastError.type = GitErrorType::ProcessFailed;
        m_lastError.message = "Branch name cannot be empty";
        return false;
    }
    
    int exitCode = 0;
    QString stderrOutput;
    
    bool ok = runGit({"branch", force ? "-D" : "-d", "--", name}, &exitCode, nullptr, &stderrOutput);
    
    if (!ok || exitCode != 0) {
        if (!force) {
            const auto ancestry = m_executor->execute("merge-base", {"--is-ancestor", name, "HEAD"}, 10000);
            m_lastDeleteRejectedAsUnmerged = ancestry.exitCode == 1;
        }
        m_lastError.type = GitErrorType::ProcessFailed;
        m_lastError.message = "Failed to delete branch: " + stderrOutput;
        m_lastError.exitCode = exitCode;
        m_lastError.stderrOutput = stderrOutput;
        return false;
    }
    
    return true;
}

bool GitBranchService::checkoutBranch(const QString &name) {
    m_lastError = GitError();
    
    if (name.isEmpty()) {
        m_lastError.type = GitErrorType::ProcessFailed;
        m_lastError.message = "Branch name cannot be empty";
        return false;
    }
    
    int exitCode = 0;
    QString stderrOutput;
    
    bool ok = runGit({"switch", "--", name}, &exitCode, nullptr, &stderrOutput);
    if ((!ok || exitCode != 0) &&
        (stderrOutput.contains("not a git command", Qt::CaseInsensitive) ||
         stderrOutput.contains("unknown subcommand", Qt::CaseInsensitive))) {
        ok = runGit({"checkout", "--", name}, &exitCode, nullptr, &stderrOutput);
    }
    
    if (!ok || exitCode != 0) {
        m_lastError.type = GitErrorType::ProcessFailed;
        m_lastError.message = "Failed to checkout branch: " + stderrOutput;
        m_lastError.exitCode = exitCode;
        m_lastError.stderrOutput = stderrOutput;
        return false;
    }
    
    return true;
}

void GitBranchService::branchesAsync(BranchListCompletion completion) {
    m_executor->executeAsync(
        "branch",
        {"--format=%(refname:short)"},
        [completion](GitCommandExecutor::Result commandResult) {
            if (commandResult.error.hasError()) {
                completion({}, asyncError("Failed to list branches: ", commandResult));
                return;
            }

            QStringList branches;
            const QStringList lines = QString::fromUtf8(commandResult.output).split('\n', Qt::SkipEmptyParts);
            for (const QString &line : lines) {
                const QString branch = line.trimmed();
                if (!branch.isEmpty()) {
                    branches.append(branch);
                }
            }
            completion(branches, GitError());
        },
        10000);
}

void GitBranchService::currentBranchAsync(BranchCompletion completion) {
    // v4-P6-21R6 P1-01: immutable operation context - the fallback
    // stage must never run against a repository that changed while
    // `branch --show-current` was pending.
    const QString operationRepo = m_executor->workingDirectory();
    const quint64 operationGeneration = m_repositoryGeneration;
    m_executor->executeAsync(
        "branch",
        {"--show-current"},
        [this, completion, operationRepo,
         operationGeneration](GitCommandExecutor::Result commandResult) {
            if (commandResult.error.hasError()) {
                completion({}, asyncError("Failed to get current branch: ", commandResult));
                return;
            }
            QString branch = QString::fromUtf8(commandResult.output).trimmed();
            if (branch.isEmpty()) {
                // Session fence BEFORE the fallback stage.
                if (m_executor->workingDirectory() != operationRepo
                    || m_repositoryGeneration != operationGeneration) {
                    completion({}, asyncError("Repository changed while reading branch: ",
                                              commandResult));
                    return;
                }
                // `branch --show-current` is empty for both detached and
                // unborn HEAD.  symbolic-ref succeeds for the latter and
                // preserves its intended branch name.
                m_executor->executeAsync(
                    "symbolic-ref", {"-q", "--short", "HEAD"},
                    [completion](GitCommandExecutor::Result refResult) {
                        QString branch = QString::fromUtf8(refResult.output)
                                              .trimmed();
                        if (branch.isEmpty()) {
                            branch = "(detached)";
                        }
                        completion(branch, GitError());
                    },
                    10000);
                return;
            }
            completion(branch, GitError());
        },
        10000);
}

void GitBranchService::workspaceStatusAsync(WorkspaceStatusCompletion completion) {
    m_executor->executeAsync(
        "status",
        {"--porcelain=v2", "--untracked-files=all"},
        [completion](GitCommandExecutor::Result commandResult) {
            WorkspaceStatus status;
            status.rawPorcelain = QString::fromUtf8(commandResult.output);
            if (commandResult.error.hasError()) {
                status.error = asyncError("Failed to inspect workspace: ", commandResult);
                completion(status);
                return;
            }

            status.success = true;
            const QStringList lines = status.rawPorcelain.split('\n', Qt::SkipEmptyParts);
            for (const QString &line : lines) {
                if (line.startsWith("? ")) {
                    status.hasUntrackedFiles = true;
                    continue;
                }
                if (line.startsWith("u ")) {
                    status.hasConflicts = true;
                    status.hasTrackedChanges = true;
                    continue;
                }
                if ((line.startsWith("1 ") || line.startsWith("2 ")) && line.size() >= 4) {
                    const QChar indexState = line.at(2);
                    const QChar workTreeState = line.at(3);
                    status.hasStagedChanges |= indexState != '.';
                    status.hasTrackedChanges |= workTreeState != '.';
                }
            }
            completion(status);
        },
        10000);
}

void GitBranchService::validateBranchNameAsync(
    const QString &name,
    OperationCompletion completion) {
    if (name.isEmpty()) {
        OperationResult result;
        result.error.type = GitErrorType::ProcessFailed;
        result.error.message = "Branch name cannot be empty";
        completion(result);
        return;
    }

    m_executor->executeAsync(
        "check-ref-format",
        {"--branch", name},
        [completion](GitCommandExecutor::Result commandResult) {
            OperationResult result;
            result.success = !commandResult.error.hasError();
            if (!result.success) {
                result.error = asyncError("Invalid branch name: ", commandResult);
            }
            completion(result);
        },
        10000);
}

void GitBranchService::createBranchAsync(const QString &name, OperationCompletion completion) {
    createBranchAsync(name, {}, false, completion);
}

void GitBranchService::createBranchAsync(
    const QString &name,
    const QString &startPoint,
    bool checkoutAfterCreation,
    OperationCompletion completion) {
    if (name.isEmpty()) {
        OperationResult result;
        result.error.type = GitErrorType::ProcessFailed;
        result.error.message = "Branch name cannot be empty";
        completion(result);
        return;
    }

    const auto finishCreation = [completion](GitCommandExecutor::Result commandResult) {
        OperationResult result;
        result.success = !commandResult.error.hasError();
        if (!result.success) {
            result.error = asyncError("Failed to create branch: ", commandResult);
        }
        completion(result);
    };

    if (checkoutAfterCreation) {
        const QString operationRepo = m_executor->workingDirectory();
        const quint64 operationGeneration = m_repositoryGeneration;
        QStringList switchArguments{"-c", name};
        if (!startPoint.isEmpty()) {
            switchArguments.append(startPoint);
        }
        m_executor->executeAsync(
            "switch",
            switchArguments,
            [this, name, startPoint, operationRepo, operationGeneration,
             finishCreation](GitCommandExecutor::Result commandResult) {
                const QString stderrOutput = QString::fromUtf8(commandResult.errorOutput);
                if (commandResult.error.hasError() && switchCommandUnavailable(stderrOutput)) {
                    if (m_executor->workingDirectory() != operationRepo
                        || m_repositoryGeneration != operationGeneration) {
                        GitCommandExecutor::Result aborted;
                        aborted.error.type = GitErrorType::ProcessFailed;
                        aborted.error.message =
                            "Repository changed while creating branch - operation aborted";
                        finishCreation(aborted);
                        return;
                    }
                    QStringList checkoutArguments{"-b", name};
                    if (!startPoint.isEmpty()) {
                        checkoutArguments.append(startPoint);
                    }
                    m_executor->executeAsync(
                        "checkout",
                        checkoutArguments,
                        finishCreation,
                        10000);
                    return;
                }
                finishCreation(commandResult);
            },
            10000);
        return;
    }

    QStringList branchArguments{"--", name};
    if (!startPoint.isEmpty()) {
        branchArguments.append(startPoint);
    }
    m_executor->executeAsync(
        "branch",
        branchArguments,
        [finishCreation](GitCommandExecutor::Result commandResult) {
            finishCreation(commandResult);
        },
        10000);
}

void GitBranchService::deleteBranchAsync(
    const QString &name,
    bool force,
    OperationCompletion completion) {
    if (name.isEmpty()) {
        OperationResult result;
        result.error.type = GitErrorType::ProcessFailed;
        result.error.message = "Branch name cannot be empty";
        completion(result);
        return;
    }

    m_executor->executeAsync(
        "branch",
        {force ? "-D" : "-d", "--", name},
        [this, name, force, completion](GitCommandExecutor::Result commandResult) {
            if (!commandResult.error.hasError()) {
                OperationResult result;
                result.success = true;
                completion(result);
                return;
            }

            const GitError deleteError = asyncError("Failed to delete branch: ", commandResult);
            if (force) {
                OperationResult result;
                result.error = deleteError;
                completion(result);
                return;
            }

            m_executor->executeAsync(
                "merge-base",
                {"--is-ancestor", name, "HEAD"},
                [completion, deleteError](GitCommandExecutor::Result ancestryResult) {
                    OperationResult result;
                    result.error = deleteError;
                    result.rejectedAsUnmerged = ancestryResult.exitCode == 1;
                    completion(result);
                },
                10000);
        },
        10000);
}

void GitBranchService::checkoutBranchAsync(const QString &name, OperationCompletion completion) {
    if (name.isEmpty()) {
        OperationResult result;
        result.error.type = GitErrorType::ProcessFailed;
        result.error.message = "Branch name cannot be empty";
        completion(result);
        return;
    }

    const auto finishCheckout = [completion](GitCommandExecutor::Result commandResult) {
        OperationResult result;
        result.success = !commandResult.error.hasError();
        if (!result.success) {
            result.error = asyncError("Failed to checkout branch: ", commandResult);
        }
        completion(result);
    };
    const QString operationRepo = m_executor->workingDirectory();
    const quint64 operationGeneration = m_repositoryGeneration;
    m_executor->executeAsync(
        "switch",
        {"--", name},
        [this, name, operationRepo, operationGeneration,
         finishCheckout](GitCommandExecutor::Result commandResult) {
            const QString stderrOutput = QString::fromUtf8(commandResult.errorOutput);
            if (commandResult.error.hasError() && switchCommandUnavailable(stderrOutput)) {
                if (m_executor->workingDirectory() != operationRepo
                    || m_repositoryGeneration != operationGeneration) {
                    GitCommandExecutor::Result aborted;
                    aborted.error.type = GitErrorType::ProcessFailed;
                    aborted.error.message =
                        "Repository changed while switching branch - operation aborted";
                    finishCheckout(aborted);
                    return;
                }
                m_executor->executeAsync("checkout", {"--", name}, finishCheckout, 10000);
                return;
            }
            finishCheckout(commandResult);
        },
        10000);
}

bool GitBranchService::runGit(const QStringList &args, int *exitCode, QString *output, QString *stderrOutput) {
    if (args.isEmpty()) {
        m_lastError.type = GitErrorType::ProcessFailed;
        m_lastError.message = "Git command cannot be empty";
        return false;
    }

    const auto result = m_executor->execute(args.first(), args.mid(1), 10000);
    if (exitCode) *exitCode = result.exitCode;
    if (output) *output = QString::fromUtf8(result.output);
    if (stderrOutput) *stderrOutput = QString::fromUtf8(result.errorOutput);
    if (result.error.hasError()) {
        m_lastError = result.error;
        return false;
    }

    return true;
}
