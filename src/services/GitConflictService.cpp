#include "GitConflictService.h"

#include "GitCommandExecutor.h"
#include "GitConflictParser.h"

#include <QDir>
#include <QFileInfo>

#include <memory>

namespace {

GitError invalidOutput(const QString &message) {
    GitError error;
    error.type = GitErrorType::InvalidOutput;
    error.message = message;
    return error;
}

bool pathExistsForRepository(const QString &repository, const QString &gitPath) {
    if (gitPath.isEmpty()) {
        return false;
    }
    QFileInfo info(gitPath);
    if (info.isRelative()) {
        info.setFile(QDir(repository).absoluteFilePath(gitPath));
    }
    return info.exists();
}

struct ContentLoadState {
    ConflictFile file;
    ConflictFileContent content;
    GitError error;
    QString repository;
    quint64 generation = 0;
    int nextStage = 1;
};

bool hasStage(const ConflictFile &file, int stage) {
    return stage == 1 ? file.hasBase : stage == 2 ? file.hasOurs : file.hasTheirs;
}

QString objectIdForStage(const ConflictFile &file, int stage) {
    return stage == 1 ? file.baseObjectId
                      : stage == 2 ? file.oursObjectId : file.theirsObjectId;
}

void setTruncated(ConflictFileContent *content, int stage) {
    if (stage == 1) content->baseTruncated = true;
    if (stage == 2) content->oursTruncated = true;
    if (stage == 3) content->theirsTruncated = true;
    content->contentTruncated = true;
}

void setContent(ConflictFileContent *content, int stage, const QByteArray &bytes) {
    if (stage == 1) content->baseContent = bytes;
    if (stage == 2) content->oursContent = bytes;
    if (stage == 3) content->theirsContent = bytes;
}

} // namespace

GitConflictService::GitConflictService(QObject *parent)
    : QObject(parent)
    , m_executor(new GitCommandExecutor(this)) {
}

GitConflictService::GitConflictService(GitCommandExecutor *executor,
                                       QObject *parent)
    : QObject(parent)
    , m_executor(executor) {
    Q_ASSERT(m_executor);
}

void GitConflictService::setWorkingDirectory(const QString &path) {
    // Increment on every assignment, including an A→B→A round trip.  A
    // path equality check alone cannot distinguish those repository sessions.
    ++m_repositoryGeneration;
    m_workingDirectory = path;
    m_executor->setWorkingDirectory(path);
}

QString GitConflictService::workingDirectory() const {
    return m_workingDirectory;
}

void GitConflictService::setPreviewLimit(qint64 bytes) {
    m_previewLimit = qMax<qint64>(0, bytes);
}

bool GitConflictService::isSessionCurrent(quint64 generation,
                                          const QString &repository) const {
    return generation == m_repositoryGeneration && repository == m_workingDirectory;
}

void GitConflictService::scanConflictsAsync(ScanCompletion completion) {
    const QString operationRepository = m_workingDirectory;
    const quint64 operationGeneration = m_repositoryGeneration;
    m_executor->executeAsync(
        QStringLiteral("status"),
        {QStringLiteral("--porcelain=v2"), QStringLiteral("-z")},
        [this, completion = std::move(completion), operationRepository,
         operationGeneration](GitCommandExecutor::Result statusResult) mutable {
            if (!isSessionCurrent(operationGeneration, operationRepository)) {
                ConflictScanResult stale;
                stale.stale = true;
                completion(std::move(stale));
                return;
            }
            if (statusResult.error.hasError()) {
                ConflictScanResult failed;
                failed.snapshot.repositoryPath = operationRepository;
                failed.error = statusResult.error;
                completion(std::move(failed));
                return;
            }
            const auto typesByPath =
                GitConflictParser::parsePorcelainV2Unmerged(statusResult.output);
            m_executor->executeAsync(
                QStringLiteral("ls-files"),
                {QStringLiteral("-u"), QStringLiteral("-z")},
                [this, completion = std::move(completion), operationRepository,
                 operationGeneration, typesByPath](GitCommandExecutor::Result indexResult) mutable {
                    if (!isSessionCurrent(operationGeneration, operationRepository)) {
                        ConflictScanResult stale;
                        stale.stale = true;
                        completion(std::move(stale));
                        return;
                    }
                    if (indexResult.error.hasError()) {
                        ConflictScanResult failed;
                        failed.snapshot.repositoryPath = operationRepository;
                        failed.error = indexResult.error;
                        completion(std::move(failed));
                        return;
                    }
                    ConflictSnapshot snapshot;
                    snapshot.repositoryPath = operationRepository;
                    snapshot.files = GitConflictParser::parseUnmergedIndex(
                        indexResult.output, typesByPath);
                    snapshot.conflictCount = snapshot.files.size();
                    snapshot.hasConflicts = snapshot.conflictCount > 0;
                    if (!snapshot.hasConflicts) {
                        ConflictScanResult result;
                        result.snapshot = std::move(snapshot);
                        completion(std::move(result));
                        return;
                    }
                    detectOperationAsync(std::move(snapshot), operationGeneration,
                                         operationRepository, std::move(completion));
                });
        });
}

void GitConflictService::detectOperationAsync(ConflictSnapshot snapshot,
                                               quint64 generation,
                                               const QString &repository,
                                               ScanCompletion completion) {
    m_executor->executeAsync(
        QStringLiteral("rev-parse"),
        {QStringLiteral("--git-path"), QStringLiteral("MERGE_HEAD"),
         QStringLiteral("--git-path"), QStringLiteral("rebase-merge"),
         QStringLiteral("--git-path"), QStringLiteral("rebase-apply"),
         QStringLiteral("--git-path"), QStringLiteral("CHERRY_PICK_HEAD"),
         QStringLiteral("--git-path"), QStringLiteral("REVERT_HEAD")},
        [this, snapshot = std::move(snapshot), generation, repository,
         completion = std::move(completion)](GitCommandExecutor::Result result) mutable {
            if (!isSessionCurrent(generation, repository)) {
                ConflictScanResult stale;
                stale.stale = true;
                completion(std::move(stale));
                return;
            }
            if (result.error.hasError()) {
                ConflictScanResult failed;
                failed.snapshot = std::move(snapshot);
                failed.error = result.error;
                completion(std::move(failed));
                return;
            }
            const QList<QByteArray> paths = result.output.split('\n');
            const auto existsAt = [&paths, &repository](int index) {
                return index >= 0 && index < paths.size()
                    && pathExistsForRepository(repository,
                                               QString::fromUtf8(paths.at(index)).trimmed());
            };
            if (existsAt(0)) {
                snapshot.operationType = ConflictOperationType::Merge;
            } else if (existsAt(1) || existsAt(2)) {
                snapshot.operationType = ConflictOperationType::Rebase;
            } else if (existsAt(3)) {
                snapshot.operationType = ConflictOperationType::CherryPick;
            } else if (existsAt(4)) {
                snapshot.operationType = ConflictOperationType::Revert;
            }
            ConflictScanResult completed;
            completed.snapshot = std::move(snapshot);
            completion(std::move(completed));
        });
}

void GitConflictService::loadConflictContentAsync(const ConflictFile &file,
                                                   ContentCompletion completion) {
    const QString operationRepository = m_workingDirectory;
    const quint64 operationGeneration = m_repositoryGeneration;
    auto state = std::make_shared<ContentLoadState>();
    state->file = file;
    state->repository = operationRepository;
    state->generation = operationGeneration;
    state->content.path = file.path;

    auto advance = std::make_shared<std::function<void()>>();
    *advance = [this, state, completion = std::move(completion), advance]() mutable {
        if (!isSessionCurrent(state->generation, state->repository)) {
            ConflictFileContentResult stale;
            stale.stale = true;
            completion(std::move(stale));
            return;
        }
        while (state->nextStage <= 3 && !hasStage(state->file, state->nextStage)) {
            ++state->nextStage;
        }
        if (state->nextStage > 3) {
            state->content.isBinary = state->content.baseContent.contains('\0')
                || state->content.oursContent.contains('\0')
                || state->content.theirsContent.contains('\0');
            ConflictFileContentResult completed;
            completed.content = std::move(state->content);
            completed.error = state->error;
            completion(std::move(completed));
            return;
        }
        const int stage = state->nextStage;
        const QString objectId = objectIdForStage(state->file, stage);
        m_executor->executeAsync(
            QStringLiteral("cat-file"), {QStringLiteral("-s"), objectId},
            [this, state, completion, advance, stage, objectId](GitCommandExecutor::Result sizeResult) mutable {
                if (!isSessionCurrent(state->generation, state->repository)) {
                    ConflictFileContentResult stale;
                    stale.stale = true;
                    completion(std::move(stale));
                    return;
                }
                if (sizeResult.error.hasError()) {
                    ConflictFileContentResult failed;
                    failed.content = std::move(state->content);
                    failed.error = sizeResult.error;
                    completion(std::move(failed));
                    return;
                }
                bool validSize = false;
                const qint64 size = sizeResult.output.trimmed().toLongLong(&validSize);
                if (!validSize || size < 0) {
                    ConflictFileContentResult failed;
                    failed.content = std::move(state->content);
                    failed.error = invalidOutput(QStringLiteral("Invalid blob size"));
                    completion(std::move(failed));
                    return;
                }
                if (size > m_previewLimit) {
                    setTruncated(&state->content, stage);
                    ++state->nextStage;
                    (*advance)();
                    return;
                }
                m_executor->executeAsync(
                    QStringLiteral("show"), {objectId},
                    [this, state, completion, advance, stage](GitCommandExecutor::Result blobResult) mutable {
                        if (!isSessionCurrent(state->generation, state->repository)) {
                            ConflictFileContentResult stale;
                            stale.stale = true;
                            completion(std::move(stale));
                            return;
                        }
                        if (blobResult.error.hasError()) {
                            ConflictFileContentResult failed;
                            failed.content = std::move(state->content);
                            failed.error = blobResult.error;
                            completion(std::move(failed));
                            return;
                        }
                        setContent(&state->content, stage, blobResult.output);
                        ++state->nextStage;
                        (*advance)();
                    });
            });
    };
    (*advance)();
}
