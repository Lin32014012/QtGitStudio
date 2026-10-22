#include "GitController.h"
#include "TextEncoding.h"

GitController::GitController(QObject *parent) : QObject(parent) {
    m_viewModel = new GitViewModel(this);
    m_branchService = new GitBranchService(this);
    m_commitService = new GitCommitService(this);
    m_remoteService = new GitRemoteService(this);
    m_mergeService = new GitMergeService(this);
}

GitController::~GitController() {}

void GitController::setRepositoryPath(const QString &path) {
    ++m_repositoryGeneration;
    m_repositoryPath = path;
    // The controller owns repository identity. Invalidate any in-flight
    // ViewModel refresh even when a caller switches repositories without
    // going through MainWindow.
    m_viewModel->resetSession();
    m_viewModel->setWorkingDirectory(path);
    m_branchService->setWorkingDirectory(path);
    m_commitService->setWorkingDirectory(path);
    m_remoteService->setWorkingDirectory(path);
    m_mergeService->setWorkingDirectory(path);
    if (!m_executor) {
        m_executor = new GitCommandExecutor(this);
    }
    m_executor->setWorkingDirectory(path);
}

void GitController::refresh() {
    m_viewModel->refresh();
    
    // Update branches
    QStringList branches = m_branchService->branches();
    m_viewModel->setBranches(branches);
    
    QString current = m_branchService->currentBranch();
    m_viewModel->setCurrentBranch(current);
}

bool GitController::createBranch(const QString &name) {
    bool ok = m_branchService->createBranch(name);
    if (ok) {
        refresh();
    } else {
        m_viewModel->setError(m_branchService->lastError().message);
    }
    return ok;
}

bool GitController::deleteBranch(const QString &name, bool force) {
    bool ok = m_branchService->deleteBranch(name, force);
    if (ok) {
        refresh();
    } else {
        m_viewModel->setError(m_branchService->lastError().message);
    }
    return ok;
}

bool GitController::checkoutBranch(const QString &name) {
    bool ok = m_branchService->checkoutBranch(name);
    if (ok) {
        refresh();
    } else {
        m_viewModel->setError(m_branchService->lastError().message);
    }
    return ok;
}

bool GitController::lastDeleteRejectedAsUnmerged() const {
    return m_branchService->lastDeleteRejectedAsUnmerged();
}

bool GitController::stageFiles(const QStringList &files) {
    bool ok = m_commitService->stageFiles(files);
    if (ok) {
        refresh();
    } else {
        m_viewModel->setError(m_commitService->lastError().message);
    }
    return ok;
}

bool GitController::unstageFiles(const QStringList &files) {
    const bool ok = m_commitService->unstageFiles(files);
    if (ok) {
        refresh();
    } else {
        m_viewModel->setError(m_commitService->lastError().message);
    }
    return ok;
}

QStringList GitController::stagedFiles() {
    const QStringList files = m_commitService->stagedFiles();
    if (m_commitService->lastError().hasError()) {
        m_viewModel->setError(m_commitService->lastError().message);
    }
    return files;
}

bool GitController::createCommit(const QString &message) {
    bool ok = m_commitService->commit(message);
    if (ok) {
        refresh();
    } else {
        m_viewModel->setError(m_commitService->lastError().message);
    }
    return ok;
}

void GitController::refreshAsync(RefreshCompletion completion) {
    const quint64 generation = m_repositoryGeneration;
    const QString path = m_repositoryPath;
    m_viewModel->refreshAsync([this, completion, generation, path](GitError snapshotError) {
        if (!isRepositorySessionCurrent(generation, path)) {
            return;
        }
        if (snapshotError.hasError()) {
            if (completion) {
                completion(false, snapshotError.message);
            }
            return;
        }
        m_branchService->branchesAsync([this, completion, generation, path](QStringList branches, GitError branchError) {
            if (!isRepositorySessionCurrent(generation, path)) {
                return;
            }
            if (branchError.hasError()) {
                m_viewModel->setError(branchError.message);
                if (completion) {
                    completion(false, branchError.message);
                }
                return;
            }
            m_viewModel->setBranches(branches);
            m_branchService->currentBranchAsync([this, completion, generation, path](QString currentBranch, GitError currentError) {
                if (!isRepositorySessionCurrent(generation, path)) {
                    return;
                }
                if (currentError.hasError()) {
                    m_viewModel->setError(currentError.message);
                    if (completion) {
                        completion(false, currentError.message);
                    }
                    return;
                }
                m_viewModel->setCurrentBranch(currentBranch);
                if (completion) {
                    completion(true, {});
                }
            });
        });
    });
}

void GitController::validateBranchNameAsync(
    const QString &name,
    OperationCompletion completion) {
    m_branchService->validateBranchNameAsync(
        name,
        [completion](GitBranchService::OperationResult serviceResult) {
            OperationResult result;
            result.success = serviceResult.success;
            result.error = serviceResult.error.message;
            completion(result);
        });
}

void GitController::workspaceStatusAsync(WorkspaceStatusCompletion completion) {
    m_branchService->workspaceStatusAsync(completion);
}

void GitController::createBranchAsync(const QString &name, OperationCompletion completion) {
    createBranchAsync(name, {}, false, completion);
}

void GitController::createBranchAsync(
    const QString &name,
    const QString &startPoint,
    bool checkoutAfterCreation,
    OperationCompletion completion) {
    const quint64 generation = m_repositoryGeneration;
    const QString path = m_repositoryPath;
    m_branchService->createBranchAsync(
        name,
        startPoint,
        checkoutAfterCreation,
        [this, completion, generation, path](GitBranchService::OperationResult result) {
            if (!isRepositorySessionCurrent(generation, path)) {
                return;
            }
            if (!result.success) {
                failOperation(result.error, false, completion);
                return;
            }
            refreshAfterOperation(generation, path, completion);
        });
}

void GitController::deleteBranchAsync(
    const QString &name,
    bool force,
    OperationCompletion completion) {
    const quint64 generation = m_repositoryGeneration;
    const QString path = m_repositoryPath;
    m_branchService->deleteBranchAsync(
        name,
        force,
        [this, completion, generation, path](GitBranchService::OperationResult result) {
            if (!isRepositorySessionCurrent(generation, path)) {
                return;
            }
            if (!result.success) {
                failOperation(result.error, result.rejectedAsUnmerged, completion);
                return;
            }
            refreshAfterOperation(generation, path, completion);
        });
}

void GitController::checkoutBranchAsync(const QString &name, OperationCompletion completion) {
    const quint64 generation = m_repositoryGeneration;
    const QString path = m_repositoryPath;
    m_branchService->checkoutBranchAsync(name, [this, completion, generation, path](GitBranchService::OperationResult result) {
        if (!isRepositorySessionCurrent(generation, path)) {
            return;
        }
        if (!result.success) {
            failOperation(result.error, false, completion);
            return;
        }
        refreshAfterOperation(generation, path, completion);
    });
}

void GitController::stageFilesAsync(const QStringList &files, OperationCompletion completion) {
    const quint64 generation = m_repositoryGeneration;
    const QString path = m_repositoryPath;
    m_commitService->stageFilesAsync(files, [this, completion, generation, path](GitCommitService::OperationResult result) {
        if (!isRepositorySessionCurrent(generation, path)) {
            return;
        }
        if (!result.success) {
            failOperation(result.error, false, completion);
            return;
        }
        refreshAfterOperation(generation, path, completion);
    });
}

void GitController::unstageAllAsync(OperationCompletion completion) {
    const quint64 generation = m_repositoryGeneration;
    const QString path = m_repositoryPath;
    m_commitService->stagedFilesAsync([this, completion, generation, path](QStringList files, GitError listError) {
        // Controller-level stage-2 fence: an A file list must never start an
        // unstage operation after the shared service has moved to repo B.
        if (!isRepositorySessionCurrent(generation, path)) {
            return;
        }
        if (listError.hasError()) {
            failOperation(listError, false, completion);
            return;
        }
        if (files.isEmpty()) {
            OperationResult result;
            result.success = true;
            completion(result);
            return;
        }
        m_commitService->unstageFilesAsync(
            files,
            [this, completion, generation, path](GitCommitService::OperationResult unstageResult) {
                if (!isRepositorySessionCurrent(generation, path)) {
                    return;
                }
                if (!unstageResult.success) {
                    failOperation(unstageResult.error, false, completion);
                    return;
                }
                refreshAfterOperation(generation, path, completion);
            });
    });
}

void GitController::createCommitAsync(const QString &message, OperationCompletion completion) {
    const quint64 generation = m_repositoryGeneration;
    const QString path = m_repositoryPath;
    m_commitService->commitAsync(message, [this, completion, generation, path](GitCommitService::OperationResult result) {
        if (!isRepositorySessionCurrent(generation, path)) {
            return;
        }
        if (!result.success) {
            failOperation(result.error, false, completion);
            return;
        }
        refreshAfterOperation(generation, path, completion);
    });
}

bool GitController::isRepositorySessionCurrent(quint64 generation,
                                               const QString &path) const {
    return generation == m_repositoryGeneration && path == m_repositoryPath;
}

void GitController::refreshAfterOperation(quint64 generation,
                                          const QString &path,
                                          OperationCompletion completion) {
    if (!isRepositorySessionCurrent(generation, path)) {
        return;
    }
    refreshAsync([this, generation, path, completion](bool success, const QString &error) {
        if (!isRepositorySessionCurrent(generation, path)) {
            return;
        }
        OperationResult result;
        result.success = success;
        result.error = error;
        completion(result);
    });
}

void GitController::failOperation(
    const GitError &error,
    bool rejectedAsUnmerged,
    OperationCompletion completion) {
    m_viewModel->setError(error.message);
    OperationResult result;
    result.error = error.message;
    result.rejectedAsUnmerged = rejectedAsUnmerged;
    completion(result);
}

void GitController::fileDiffAsync(const QString &path, FileDiffCompletion completion) {
    if (!m_executor) {
        FileDiffResult out;
        out.success = false;
        out.error = QStringLiteral("Git 执行器不可用");
        completion(out);
        return;
    }
    // Snapshot the repository execution context for the complete request.
    // The controller's long-lived executor is retargeted when the user
    // switches repositories; using it for the second command could combine
    // Repo A's unstaged output with Repo B's staged output.
    auto *requestExecutor = m_fileDiffExecutorFactory
        ? m_fileDiffExecutorFactory(this)
        : new GitCommandExecutor(this);
    Q_ASSERT(requestExecutor);
    requestExecutor->setWorkingDirectory(m_executor->workingDirectory());
    requestExecutor->setGitPath(m_executor->gitPath());
    requestExecutor->setExtraEnvironment(m_executor->extraEnvironment());

    auto finishFailure = [requestExecutor, completion](
                              const GitCommandExecutor::Result &result) {
        FileDiffResult out;
        out.success = false;
        out.error = result.error.message;
        if (out.error.isEmpty()) {
            out.error = QString::fromUtf8(result.errorOutput);
        }
        requestExecutor->deleteLater();
        completion(out);
    };

    // Unstaged diff first, then staged diff; both use the immutable request
    // executor captured above.
    requestExecutor->executeAsync(
        QStringLiteral("diff"),
        {QStringLiteral("--no-ext-diff"), QStringLiteral("--unified=3"),
         QStringLiteral("--"), path},
        [requestExecutor, path, completion, finishFailure](
            GitCommandExecutor::Result unstaged) {
            if (unstaged.error.hasError()) {
                finishFailure(unstaged);
                return;
            }
            requestExecutor->executeAsync(
                QStringLiteral("diff"),
                {QStringLiteral("--cached"), QStringLiteral("--no-ext-diff"),
                 QStringLiteral("--unified=3"), QStringLiteral("--"), path},
                [requestExecutor, path, completion, unstaged, finishFailure](
                    GitCommandExecutor::Result staged) {
                    if (staged.error.hasError()) {
                        finishFailure(staged);
                        return;
                    }
                    FileDiffResult out;
                    out.success = true;
                    // Non-UTF-8 (e.g. GBK) lines are decoded per line.
                    out.text = TextEncoding::decodeMixedLines(unstaged.output)
                        + TextEncoding::decodeMixedLines(staged.output);
                    // Git marks binary files with "Binary files ... differ"
                    // (or the newer "Files ... differ"); NUL bytes are a
                    // second-order signal. We only READ git's verdict.
                    out.binary = out.text.contains(QStringLiteral("Binary files"))
                        || out.text.contains(QStringLiteral("Files "))
                        || out.text.contains(QChar('\0'));
                    requestExecutor->deleteLater();
                    completion(out);
                });
        });
}

void GitController::remotesAsync(RemotesCompletion completion) {
    m_remoteService->remotesAsync(
        [completion](const GitRemoteService::RemotesResult &serviceResult) {
            RemoteResult out;
            out.success = serviceResult.success;
            out.error = serviceResult.error;
            out.remotes = serviceResult.remotes;
            completion(out);
        });
}

void GitController::fetchAsync(RemoteOperationCompletion completion) {
    m_remoteService->fetchAsync(completion);
}

void GitController::pullAsync(RemoteOperationCompletion completion) {
    m_remoteService->pullAsync(completion);
}

void GitController::pushAsync(RemoteOperationCompletion completion) {
    m_remoteService->pushAsync(completion);
}

void GitController::mergeAsync(const QString &branch, MergeCompletion completion) {
    m_mergeService->mergeAsync(branch, completion);
}

void GitController::abortMergeAsync(
    std::function<void(bool, const QString &)> completion) {
    m_mergeService->abortAsync(completion);
}
