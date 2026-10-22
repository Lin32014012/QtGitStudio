#include "GitConflictResolutionService.h"

#include "GitCommandExecutor.h"
#include "GitConflictService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <QCryptographicHash>

#include <memory>

namespace {

ConflictApplyResult failure(ConflictApplyError error, const QString &message) {
    ConflictApplyResult result;
    result.error = error;
    result.message = message;
    return result;
}

bool isSafeRelativePath(const QString &path) {
    if (path.isEmpty() || QDir::isAbsolutePath(path)) {
        return false;
    }
    const QString cleaned = QDir::cleanPath(path);
    return cleaned != QStringLiteral("..") && !cleaned.startsWith(QStringLiteral("../"))
        && cleaned != QStringLiteral(".") && cleaned == path;
}

bool isValidUtf8(const QByteArray &bytes) {
    const QByteArray body = bytes.startsWith("\xEF\xBB\xBF") ? bytes.mid(3) : bytes;
    return QString::fromUtf8(body).toUtf8() == body;
}

} // namespace

QString conflictApplyErrorName(ConflictApplyError error) {
    switch (error) {
    case ConflictApplyError::None: return QStringLiteral("None");
    case ConflictApplyError::StaleSession: return QStringLiteral("StaleSession");
    case ConflictApplyError::ConflictChangedExternally: return QStringLiteral("ConflictChangedExternally");
    case ConflictApplyError::InvalidPlan: return QStringLiteral("InvalidPlan");
    case ConflictApplyError::UnsupportedEncoding: return QStringLiteral("UnsupportedEncoding");
    case ConflictApplyError::WriteFailed: return QStringLiteral("WriteFailed");
    case ConflictApplyError::StageFailed: return QStringLiteral("StageFailed");
    case ConflictApplyError::StageVerificationFailed: return QStringLiteral("StageVerificationFailed");
    case ConflictApplyError::GitCommandFailed: return QStringLiteral("GitCommandFailed");
    case ConflictApplyError::UnknownOperation: return QStringLiteral("UnknownOperation");
    case ConflictApplyError::ContinueFailed: return QStringLiteral("ContinueFailed");
    case ConflictApplyError::AbortFailed: return QStringLiteral("AbortFailed");
    case ConflictApplyError::PartialSuccess: return QStringLiteral("PartialSuccess");
    }
    return QStringLiteral("Unknown");
}

GitConflictResolutionService::GitConflictResolutionService(QObject *parent)
    : QObject(parent)
    , m_executor(new GitCommandExecutor(this))
    , m_conflictService(new GitConflictService(m_executor, this)) {
}

GitConflictResolutionService::GitConflictResolutionService(GitCommandExecutor *executor,
                                                           QObject *parent)
    : QObject(parent)
    , m_executor(executor)
    , m_conflictService(new GitConflictService(executor, this)) {
    Q_ASSERT(m_executor);
}

void GitConflictResolutionService::setWorkingDirectory(const QString &path) {
    ++m_repositoryGeneration;
    m_workingDirectory = path;
    m_executor->setWorkingDirectory(path);
    m_conflictService->setWorkingDirectory(path);
}

bool GitConflictResolutionService::isCurrent(const OperationContext &context) const {
    return context.generation == m_repositoryGeneration
        && context.repository == m_workingDirectory;
}

bool GitConflictResolutionService::validPlan(const ConflictResolutionPlan &plan,
                                             QString *reason) const {
    if (!plan.ready || plan.unresolvedHunks != 0) {
        *reason = QStringLiteral("The resolution plan is not ready.");
        return false;
    }
    if (plan.repositoryPath != m_workingDirectory
        || plan.sessionGeneration != m_repositoryGeneration) {
        *reason = QStringLiteral("The resolution plan belongs to an old repository session.");
        return false;
    }
    if (!isSafeRelativePath(plan.filePath)) {
        *reason = QStringLiteral("The resolution path is not a safe repository-relative path.");
        return false;
    }
    if (plan.deleteFile) {
        return true;
    }
    if (plan.action == ResolutionAction::None || plan.action == ResolutionAction::Delete) {
        *reason = QStringLiteral("The resolution action is incomplete.");
        return false;
    }
    return true;
}

const ConflictFile *GitConflictResolutionService::findFile(const ConflictSnapshot &snapshot,
                                                            const QString &path) {
    for (const ConflictFile &file : snapshot.files) {
        if (file.path == path) {
            return &file;
        }
    }
    return nullptr;
}

bool GitConflictResolutionService::metadataMatches(const ConflictResolutionPlan &plan,
                                                    const ConflictFile &file) {
    return plan.conflictType == file.type
        && plan.expectedBaseObjectId == file.baseObjectId
        && plan.expectedOursObjectId == file.oursObjectId
        && plan.expectedTheirsObjectId == file.theirsObjectId;
}

bool GitConflictResolutionService::workingTreeMatches(const QString &absolutePath,
                                                       const ConflictResolutionPlan &plan) {
    QFile current(absolutePath);
    const bool exists = current.exists();
    if (exists != plan.expectedWorkingTreeExists) {
        return false;
    }
    if (!exists || !current.open(QIODevice::ReadOnly)) {
        return !exists && plan.expectedWorkingTreeSha256.isEmpty();
    }
    const QString actual = QString::fromLatin1(
        QCryptographicHash::hash(current.readAll(), QCryptographicHash::Sha256).toHex());
    return actual == plan.expectedWorkingTreeSha256;
}

QByteArray GitConflictResolutionService::encodeTextResult(const QByteArray &currentBytes,
                                                           const QString &text,
                                                           bool *supported) {
    *supported = isValidUtf8(currentBytes);
    if (!*supported) {
        return {};
    }
    QByteArray result = text.toUtf8();
    if (currentBytes.contains("\r\n")) {
        result.replace("\r\n", "\n");
        result.replace("\n", "\r\n");
    }
    if (currentBytes.startsWith("\xEF\xBB\xBF")) {
        result.prepend("\xEF\xBB\xBF");
    }
    return result;
}

QStringList GitConflictResolutionService::commandForOperation(
    ConflictOperationType operation, bool abort) {
    const QString action = abort ? QStringLiteral("--abort") : QStringLiteral("--continue");
    switch (operation) {
    case ConflictOperationType::Merge: return {QStringLiteral("merge"), action};
    case ConflictOperationType::Rebase: return {QStringLiteral("rebase"), action};
    case ConflictOperationType::CherryPick: return {QStringLiteral("cherry-pick"), action};
    case ConflictOperationType::Revert: return {QStringLiteral("revert"), action};
    case ConflictOperationType::Unknown: break;
    }
    return {};
}

void GitConflictResolutionService::completeStale(Completion completion) const {
    ConflictApplyResult result = failure(
        ConflictApplyError::StaleSession,
        QStringLiteral("Repository session changed; no write was performed."));
    result.stale = true;
    completion(std::move(result));
}

void GitConflictResolutionService::applyPlanAsync(const ConflictResolutionPlan &plan,
                                                   Completion completion) {
    QString invalidReason;
    if (!validPlan(plan, &invalidReason)) {
        completion(failure(plan.repositoryPath != m_workingDirectory
                               || plan.sessionGeneration != m_repositoryGeneration
                           ? ConflictApplyError::StaleSession
                           : ConflictApplyError::InvalidPlan,
                           invalidReason));
        return;
    }
    const OperationContext context{m_workingDirectory, m_repositoryGeneration};
    m_conflictService->scanConflictsAsync(
        [this, context, plan, completion = std::move(completion)](ConflictScanResult scan) mutable {
            if (!isCurrent(context) || scan.stale) {
                completeStale(std::move(completion));
                return;
            }
            if (scan.error.hasError()) {
                completion(failure(ConflictApplyError::GitCommandFailed, scan.error.message));
                return;
            }
            const ConflictFile *current = findFile(scan.snapshot, plan.filePath);
            if (!current || !metadataMatches(plan, *current)) {
                completion(failure(ConflictApplyError::ConflictChangedExternally,
                                   QStringLiteral("Conflict index metadata changed; the result was not written.")));
                return;
            }
            applyValidatedPlan(context, plan, *current, std::move(completion));
        });
}

void GitConflictResolutionService::applyValidatedPlan(const OperationContext &context,
                                                       const ConflictResolutionPlan &plan,
                                                       const ConflictFile &current,
                                                       Completion completion) {
    if (!isCurrent(context)) {
        completeStale(std::move(completion));
        return;
    }
    ConflictApplyResult result;
    result.resolvedPath = plan.filePath;
    const QString absolutePath = QDir(context.repository).absoluteFilePath(plan.filePath);
    if (!workingTreeMatches(absolutePath, plan)) {
        completion(failure(ConflictApplyError::ConflictChangedExternally,
                           QStringLiteral("The worktree file changed; the result was not written.")));
        return;
    }
    if (m_failureInjection.failWrite) {
        completion(failure(ConflictApplyError::WriteFailed, QStringLiteral("Injected write failure.")));
        return;
    }
    if (plan.deleteFile) {
        m_executor->executeAsync(QStringLiteral("rm"), {QStringLiteral("--"), plan.filePath},
            [this, context, result, completion = std::move(completion)](GitCommandExecutor::Result command) mutable {
                if (!isCurrent(context)) { completeStale(std::move(completion)); return; }
                if (command.error.hasError()) {
                    completion(failure(ConflictApplyError::StageFailed, QString::fromUtf8(command.errorOutput)));
                    return;
                }
                ConflictApplyResult staged = result;
                staged.workingTreeUpdated = true;
                staged.staged = true;
                stageAndVerify(context, std::move(staged), true, std::move(completion));
            });
        return;
    }

    if (plan.binary) {
        const QString source = plan.sourceObjectId.isEmpty()
            ? (plan.action == ResolutionAction::Ours ? current.oursObjectId : current.theirsObjectId)
            : plan.sourceObjectId;
        if (source.isEmpty()) {
            completion(failure(ConflictApplyError::InvalidPlan,
                               QStringLiteral("A binary resolution requires a selected blob.")));
            return;
        }
        m_executor->executeAsync(QStringLiteral("show"), {source},
            [this, context, absolutePath, result, completion = std::move(completion)](GitCommandExecutor::Result blob) mutable {
                if (!isCurrent(context)) { completeStale(std::move(completion)); return; }
                if (blob.error.hasError()) {
                    completion(failure(ConflictApplyError::GitCommandFailed, QString::fromUtf8(blob.errorOutput)));
                    return;
                }
                QSaveFile output(absolutePath);
                if (!output.open(QIODevice::WriteOnly) || output.write(blob.output) != blob.output.size()
                    || !output.commit()) {
                    completion(failure(ConflictApplyError::WriteFailed,
                                       QStringLiteral("Could not atomically write the binary result.")));
                    return;
                }
                ConflictApplyResult written = result;
                written.workingTreeUpdated = true;
                stageAndVerify(context, std::move(written), false, std::move(completion));
            });
        return;
    }

    QFile input(absolutePath);
    if (!input.open(QIODevice::ReadOnly)) {
        completion(failure(ConflictApplyError::WriteFailed,
                           QStringLiteral("Could not read the current conflict file.")));
        return;
    }
    bool supported = false;
    const QByteArray currentBytes = input.readAll();
    input.close(); // QSaveFile must be able to atomically replace this path on Windows.
    const QByteArray bytes = encodeTextResult(currentBytes, plan.resultContent, &supported);
    if (!supported) {
        completion(failure(ConflictApplyError::UnsupportedEncoding,
                           QStringLiteral("Only UTF-8, UTF-8 BOM, and ASCII text can be safely written.")));
        return;
    }
    if (!isCurrent(context)) {
        completeStale(std::move(completion));
        return;
    }
    QSaveFile output(absolutePath);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
        completion(failure(ConflictApplyError::WriteFailed,
                           QStringLiteral("Could not atomically write the resolution result.")));
        return;
    }
    result.workingTreeUpdated = true;
    stageAndVerify(context, std::move(result), false, std::move(completion));
}

void GitConflictResolutionService::stageAndVerify(const OperationContext &context,
                                                  ConflictApplyResult result, bool alreadyStaged,
                                                  Completion completion) {
    if (m_beforeStageHook) {
        m_beforeStageHook();
    }
    if (!isCurrent(context)) {
        completeStale(std::move(completion));
        return;
    }
    if (m_failureInjection.failStage && !alreadyStaged) {
        ConflictApplyResult failed = failure(ConflictApplyError::StageFailed,
                                             QStringLiteral("Injected stage failure."));
        failed.workingTreeUpdated = result.workingTreeUpdated;
        failed.resolvedPath = result.resolvedPath;
        completion(std::move(failed));
        return;
    }
    if (alreadyStaged) {
        if (m_failureInjection.failVerify) {
            ConflictApplyResult failed = failure(ConflictApplyError::StageVerificationFailed,
                QStringLiteral("Injected index verification failure."));
            failed.workingTreeUpdated = result.workingTreeUpdated;
            failed.resolvedPath = result.resolvedPath;
            completion(std::move(failed));
            return;
        }
        const QString verifiedPath = result.resolvedPath;
        m_executor->executeAsync(QStringLiteral("ls-files"),
            {QStringLiteral("-u"), QStringLiteral("--"), verifiedPath},
            [this, context, result = std::move(result), completion = std::move(completion)](
                GitCommandExecutor::Result verified) mutable {
                if (!isCurrent(context)) { completeStale(std::move(completion)); return; }
                if (verified.error.hasError() || !verified.output.trimmed().isEmpty()) {
                    ConflictApplyResult failed = failure(ConflictApplyError::StageVerificationFailed,
                        verified.error.hasError() ? QString::fromUtf8(verified.errorOutput)
                                                   : QStringLiteral("Unmerged index entries remain."));
                    failed.workingTreeUpdated = result.workingTreeUpdated;
                    failed.resolvedPath = result.resolvedPath;
                    completion(std::move(failed));
                    return;
                }
                ConflictApplyResult completed = result;
                completed.staged = true;
                rescanAfterMutation(context, std::move(completed), std::move(completion));
            });
        return;
    }
    const QString stagedPath = result.resolvedPath;
    m_executor->executeAsync(QStringLiteral("add"),
        {QStringLiteral("--"), stagedPath},
        [this, context, result = std::move(result), completion = std::move(completion)](
            GitCommandExecutor::Result staged) mutable {
            if (!isCurrent(context)) { completeStale(std::move(completion)); return; }
            if (staged.error.hasError()) {
                ConflictApplyResult failed = failure(ConflictApplyError::StageFailed,
                                                     QString::fromUtf8(staged.errorOutput));
                failed.workingTreeUpdated = result.workingTreeUpdated;
                failed.resolvedPath = result.resolvedPath;
                completion(std::move(failed));
                return;
            }
            auto verifyAfterAdd = [this, context, result = std::move(result), completion = std::move(completion)]() mutable {
                stageAndVerify(context, std::move(result), true, std::move(completion));
            };
            verifyAfterAdd();
        });
}

void GitConflictResolutionService::rescanAfterMutation(const OperationContext &context,
                                                        ConflictApplyResult result,
                                                        Completion completion) {
    m_conflictService->scanConflictsAsync(
        [this, context, result = std::move(result), completion = std::move(completion)](
            ConflictScanResult scan) mutable {
            if (!isCurrent(context) || scan.stale) { completeStale(std::move(completion)); return; }
            if (scan.error.hasError()) {
                completion(failure(ConflictApplyError::GitCommandFailed, scan.error.message));
                return;
            }
            ConflictApplyResult done = result;
            done.snapshot = std::move(scan.snapshot);
            if (!result.resolvedPath.isEmpty()) {
                done.successFiles = {result.resolvedPath};
            }
            completion(std::move(done));
        });
}

void GitConflictResolutionService::verifyContinueOutcome(
    const OperationContext &context, ConflictOperationType operation,
    ConflictApplyResult result, Completion completion) {
    QStringList markers;
    switch (operation) {
    case ConflictOperationType::Merge: markers = {QStringLiteral("MERGE_HEAD")}; break;
    case ConflictOperationType::Rebase: markers = {QStringLiteral("rebase-merge"), QStringLiteral("rebase-apply")}; break;
    case ConflictOperationType::CherryPick: markers = {QStringLiteral("CHERRY_PICK_HEAD")}; break;
    case ConflictOperationType::Revert: markers = {QStringLiteral("REVERT_HEAD")}; break;
    case ConflictOperationType::Unknown: break;
    }
    QStringList arguments{QStringLiteral("--git-path")};
    for (int index = 0; index < markers.size(); ++index) {
        if (index > 0) arguments.append(QStringLiteral("--git-path"));
        arguments.append(markers.at(index));
    }
    m_executor->executeAsync(QStringLiteral("rev-parse"), arguments,
        [this, context, result = std::move(result), completion = std::move(completion)](
            GitCommandExecutor::Result paths) mutable {
            if (!isCurrent(context)) { completeStale(std::move(completion)); return; }
            if (paths.error.hasError()) {
                completion(failure(ConflictApplyError::GitCommandFailed,
                                   QString::fromUtf8(paths.errorOutput)));
                return;
            }
            bool operationStillActive = false;
            for (const QByteArray &relative : paths.output.split('\n')) {
                const QString gitPath = QString::fromUtf8(relative).trimmed();
                if (!gitPath.isEmpty() && QFileInfo(QDir(context.repository).filePath(gitPath)).exists()) {
                    operationStillActive = true;
                    break;
                }
            }
            if (operationStillActive && !result.snapshot.hasConflicts) {
                completion(failure(ConflictApplyError::ContinueFailed,
                    QStringLiteral("Git reported success but the operation state is still active.")));
                return;
            }
            result.continuedButMoreConflicts = result.snapshot.hasConflicts;
            completion(std::move(result));
        });
}

void GitConflictResolutionService::applyAllReadyAsync(const QList<ConflictResolutionPlan> &plans,
                                                       Completion completion) {
    QList<ConflictResolutionPlan> ready;
    for (const ConflictResolutionPlan &plan : plans) {
        if (plan.ready && plan.unresolvedHunks == 0) {
            ready.append(plan);
        }
    }
    if (ready.isEmpty()) {
        completion(failure(ConflictApplyError::InvalidPlan,
                           QStringLiteral("No ready resolutions to apply.")));
        return;
    }
    auto aggregate = std::make_shared<ConflictApplyResult>();
    auto index = std::make_shared<int>(0);
    auto next = std::make_shared<std::function<void()>>();
    *next = [this, ready, aggregate, index, next, completion = std::move(completion)]() mutable {
        if (*index >= ready.size()) {
            completion(std::move(*aggregate));
            return;
        }
        const ConflictResolutionPlan plan = ready.at(*index);
        applyPlanAsync(plan, [ready, aggregate, index, next, completion, plan](
                            ConflictApplyResult result) mutable {
            if (!result.success()) {
                if (!aggregate->successFiles.isEmpty()) {
                    result.error = ConflictApplyError::PartialSuccess;
                    result.successFiles = aggregate->successFiles;
                }
                result.failedPath = plan.filePath;
                for (int i = *index + 1; i < ready.size(); ++i) {
                    result.remainingPlans.append(ready.at(i).filePath);
                }
                completion(std::move(result));
                return;
            }
            aggregate->successFiles.append(plan.filePath);
            aggregate->snapshot = result.snapshot;
            ++*index;
            (*next)();
        });
    };
    (*next)();
}

void GitConflictResolutionService::continueOperationAsync(ConflictOperationType operation,
                                                          Completion completion) {
    const QStringList command = commandForOperation(operation, false);
    if (command.isEmpty()) {
        completion(failure(ConflictApplyError::UnknownOperation,
                           QStringLiteral("Unknown Git operation.")));
        return;
    }
    const OperationContext context{m_workingDirectory, m_repositoryGeneration};
    m_conflictService->scanConflictsAsync(
        [this, context, command, operation, completion = std::move(completion)](ConflictScanResult scan) mutable {
            if (!isCurrent(context) || scan.stale) { completeStale(std::move(completion)); return; }
            if (scan.error.hasError()) {
                completion(failure(ConflictApplyError::GitCommandFailed, scan.error.message));
                return;
            }
            if (scan.snapshot.hasConflicts) {
                completion(failure(ConflictApplyError::InvalidPlan,
                    QStringLiteral("Unmerged index entries remain; continue is unavailable.")));
                return;
            }
            if (m_failureInjection.failContinue) {
                completion(failure(ConflictApplyError::ContinueFailed,
                                   QStringLiteral("Injected continue failure.")));
                return;
            }
            if (m_beforeContinueHook) {
                m_beforeContinueHook();
            }
            if (!isCurrent(context)) {
                completeStale(std::move(completion));
                return;
            }
            m_executor->setExtraEnvironment({QStringLiteral("GIT_EDITOR=true")});
            m_executor->executeAsync(command.first(), command.mid(1),
                [this, context, operation, completion = std::move(completion)](
                    GitCommandExecutor::Result commandResult) mutable {
                    if (!isCurrent(context)) { completeStale(std::move(completion)); return; }
                    if (commandResult.error.hasError()) {
                        if (operation == ConflictOperationType::Rebase) {
                            const QString diagnostic = QString::fromUtf8(commandResult.errorOutput);
                            ConflictApplyResult progressed;
                            rescanAfterMutation(context, std::move(progressed),
                                [diagnostic, completion = std::move(completion)](
                                    ConflictApplyResult rescan) mutable {
                                    if (rescan.stale) {
                                        completion(std::move(rescan));
                                        return;
                                    }
                                    if (rescan.success() && rescan.snapshot.hasConflicts) {
                                        rescan.continuedButMoreConflicts = true;
                                        completion(std::move(rescan));
                                        return;
                                    }
                                    completion(failure(ConflictApplyError::ContinueFailed, diagnostic));
                                });
                            return;
                        }
                        completion(failure(ConflictApplyError::ContinueFailed,
                                           QString::fromUtf8(commandResult.errorOutput)));
                        return;
                    }
                    ConflictApplyResult result;
                    rescanAfterMutation(context, std::move(result),
                        [this, context, operation, completion = std::move(completion)](
                            ConflictApplyResult result) mutable {
                            if (!result.success()) {
                                completion(std::move(result));
                                return;
                            }
                            verifyContinueOutcome(context, operation, std::move(result),
                                                  std::move(completion));
                        });
                });
        });
}

void GitConflictResolutionService::abortOperationAsync(ConflictOperationType operation,
                                                       Completion completion) {
    const QStringList command = commandForOperation(operation, true);
    if (command.isEmpty()) {
        completion(failure(ConflictApplyError::UnknownOperation,
                           QStringLiteral("Unknown Git operation.")));
        return;
    }
    const OperationContext context{m_workingDirectory, m_repositoryGeneration};
    m_executor->executeAsync(command.first(), command.mid(1),
        [this, context, completion = std::move(completion)](
            GitCommandExecutor::Result commandResult) mutable {
            if (!isCurrent(context)) { completeStale(std::move(completion)); return; }
            if (commandResult.error.hasError()) {
                completion(failure(ConflictApplyError::AbortFailed,
                                   QString::fromUtf8(commandResult.errorOutput)));
                return;
            }
            ConflictApplyResult result;
            rescanAfterMutation(context, std::move(result), std::move(completion));
        });
}
