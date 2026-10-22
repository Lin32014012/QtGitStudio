#include "GitCommitService.h"
#include "GitCommandExecutor.h"

namespace {
GitError asyncCommitError(const QString &context, const GitCommandExecutor::Result &result) {
    GitError error = result.error;
    error.type = GitErrorType::ProcessFailed;
    const QString stderrOutput = QString::fromUtf8(result.errorOutput);
    error.message = context + stderrOutput;
    error.exitCode = result.exitCode;
    error.stderrOutput = stderrOutput;
    return error;
}
}

GitCommitService::GitCommitService(QObject *parent) : QObject(parent) {
    m_executor = new GitCommandExecutor(this);
}

GitCommitService::~GitCommitService() {}

void GitCommitService::setWorkingDirectory(const QString &dir) {
    ++m_repositoryGeneration;
    m_executor->setWorkingDirectory(dir);
}

bool GitCommitService::stageFiles(const QStringList &files) {
    m_lastError = GitError();
    
    if (files.isEmpty()) {
        m_lastError.type = GitErrorType::ProcessFailed;
        m_lastError.message = "No files to stage";
        return false;
    }
    
    QStringList args;
    args << "add";
    for (const QString &file : files) {
        args << file;
    }
    
    int exitCode = 0;
    QString stderrOutput;
    
    bool ok = runGit(args, &exitCode, nullptr, &stderrOutput);
    
    if (!ok || exitCode != 0) {
        m_lastError.type = GitErrorType::ProcessFailed;
        m_lastError.message = "Failed to stage files: " + stderrOutput;
        return false;
    }
    
    return true;
}

QStringList GitCommitService::stagedFiles() {
    m_lastError = GitError();

    int exitCode = 0;
    QString output;
    QString stderrOutput;
    const bool ok = runGit(
        {"diff", "--cached", "--name-only", "--no-renames", "-z"},
        &exitCode,
        &output,
        &stderrOutput);
    if (!ok || exitCode != 0) {
        m_lastError.type = GitErrorType::ProcessFailed;
        m_lastError.message = "Failed to list staged files: " + stderrOutput;
        m_lastError.exitCode = exitCode;
        m_lastError.stderrOutput = stderrOutput;
        return {};
    }

    QStringList files;
    const QStringList entries = output.split(QChar('\0'), Qt::SkipEmptyParts);
    for (const QString &entry : entries) {
        files.append(entry);
    }
    return files;
}

bool GitCommitService::unstageFiles(const QStringList &files) {
    m_lastError = GitError();

    if (files.isEmpty()) {
        m_lastError.type = GitErrorType::ProcessFailed;
        m_lastError.message = "No files to unstage";
        return false;
    }

    QStringList args;
    if (hasHead()) {
        args << "restore" << "--staged" << "--";
    } else {
        args << "rm" << "--cached" << "--ignore-unmatch" << "--";
    }
    args.append(files);

    int exitCode = 0;
    QString stderrOutput;
    const bool ok = runGit(args, &exitCode, nullptr, &stderrOutput);
    if (!ok || exitCode != 0) {
        m_lastError.type = GitErrorType::ProcessFailed;
        m_lastError.message = "Failed to unstage files: " + stderrOutput;
        m_lastError.exitCode = exitCode;
        m_lastError.stderrOutput = stderrOutput;
        return false;
    }

    return true;
}

bool GitCommitService::commit(const QString &message) {
    m_lastError = GitError();
    
    if (message.isEmpty()) {
        m_lastError.type = GitErrorType::ProcessFailed;
        m_lastError.message = "Commit message cannot be empty";
        return false;
    }
    
    int exitCode = 0;
    QString stderrOutput;
    
    bool ok = runGit({"commit", "-m", message}, &exitCode, nullptr, &stderrOutput);
    
    if (!ok || exitCode != 0) {
        m_lastError.type = GitErrorType::ProcessFailed;
        m_lastError.message = "Failed to commit: " + stderrOutput;
        return false;
    }
    
    return true;
}

void GitCommitService::stageFilesAsync(const QStringList &files, OperationCompletion completion) {
    if (files.isEmpty()) {
        OperationResult result;
        result.error.type = GitErrorType::ProcessFailed;
        result.error.message = "No files to stage";
        completion(result);
        return;
    }

    QStringList args = {"add"};
    args.append(files);
    m_executor->executeAsync(
        args.first(),
        args.mid(1),
        [completion](GitCommandExecutor::Result commandResult) {
            OperationResult result;
            result.success = !commandResult.error.hasError();
            if (!result.success) {
                result.error = asyncCommitError("Failed to stage files: ", commandResult);
            }
            completion(result);
        },
        10000);
}

void GitCommitService::stagedFilesAsync(FilesCompletion completion) {
    m_executor->executeAsync(
        "diff",
        {"--cached", "--name-only", "--no-renames", "-z"},
        [completion](GitCommandExecutor::Result commandResult) {
            if (commandResult.error.hasError()) {
                completion({}, asyncCommitError("Failed to list staged files: ", commandResult));
                return;
            }

            QStringList files;
            const QStringList entries = QString::fromUtf8(commandResult.output).split(QChar('\0'), Qt::SkipEmptyParts);
            for (const QString &entry : entries) {
                files.append(entry);
            }
            completion(files, GitError());
        },
        10000);
}

void GitCommitService::unstageFilesAsync(
    const QStringList &files,
    OperationCompletion completion) {
    if (files.isEmpty()) {
        OperationResult result;
        result.error.type = GitErrorType::ProcessFailed;
        result.error.message = "No files to unstage";
        completion(result);
        return;
    }

    // v4-P6-21R6 P1-01: capture the IMMUTABLE operation context - the
    // shared executor may be retargeted to repo B between stage 1 and
    // stage 2; stage 2 must never run against a different repository.
    const QString operationRepo = m_executor->workingDirectory();
    const quint64 operationGeneration = m_repositoryGeneration;

    m_executor->executeAsync(
        "rev-parse",
        {"--verify", "HEAD"},
        [this, files, completion, operationRepo,
         operationGeneration](GitCommandExecutor::Result headResult) {
            // Session fence BEFORE stage 2: the repository may have
            // changed while rev-parse was pending.
            if (m_executor->workingDirectory() != operationRepo
                || m_repositoryGeneration != operationGeneration) {
                OperationResult result;
                result.error.type = GitErrorType::ProcessFailed;
                result.error.message =
                    "Repository changed while unstaging - operation aborted";
                completion(result);
                return;
            }
            QStringList args;
            if (!headResult.error.hasError()) {
                args = {"restore", "--staged", "--"};
            } else {
                args = {"rm", "--cached", "--ignore-unmatch", "--"};
            }
            args.append(files);
            m_executor->executeAsync(
                args.first(),
                args.mid(1),
                [completion](GitCommandExecutor::Result commandResult) {
                    OperationResult result;
                    result.success = !commandResult.error.hasError();
                    if (!result.success) {
                        result.error = asyncCommitError("Failed to unstage files: ", commandResult);
                    }
                    completion(result);
                },
                10000);
        },
        10000);
}

void GitCommitService::commitAsync(const QString &message, OperationCompletion completion) {
    if (message.isEmpty()) {
        OperationResult result;
        result.error.type = GitErrorType::ProcessFailed;
        result.error.message = "Commit message cannot be empty";
        completion(result);
        return;
    }
    m_executor->executeAsync(
        "commit",
        {"-m", message},
        [completion](GitCommandExecutor::Result commandResult) {
            OperationResult result;
            result.success = !commandResult.error.hasError();
            if (!result.success) {
                result.error = asyncCommitError("Failed to commit: ", commandResult);
            }
            completion(result);
        },
        10000);
}

bool GitCommitService::hasHead() {
    int exitCode = 0;
    const bool ok = runGit({"rev-parse", "--verify", "HEAD"}, &exitCode);
    m_lastError = GitError();
    return ok && exitCode == 0;
}

bool GitCommitService::runGit(const QStringList &args, int *exitCode, QString *output, QString *stderrOutput) {
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
