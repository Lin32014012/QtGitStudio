#pragma once

#include "ConflictResolutionPlan.h"
#include "GitConflictModel.h"

#include <QObject>

#include <functional>
#include <utility>

class GitCommandExecutor;
class GitConflictService;

enum class ConflictApplyError {
    None,
    StaleSession,
    ConflictChangedExternally,
    InvalidPlan,
    UnsupportedEncoding,
    WriteFailed,
    StageFailed,
    StageVerificationFailed,
    GitCommandFailed,
    UnknownOperation,
    ContinueFailed,
    AbortFailed,
    PartialSuccess,
};

struct ConflictApplyResult {
    ConflictApplyError error = ConflictApplyError::None;
    QString message;
    QString resolvedPath;
    QString failedPath;
    QStringList successFiles;
    QStringList remainingPlans;
    ConflictSnapshot snapshot;
    bool workingTreeUpdated = false;
    bool staged = false;
    bool continuedButMoreConflicts = false;
    bool stale = false;

    bool success() const { return error == ConflictApplyError::None; }
};

QString conflictApplyErrorName(ConflictApplyError error);

// The only service permitted to mutate a conflict worktree. It owns a
// repository-generation fence and re-scans the index before every pipeline.
class GitConflictResolutionService : public QObject {
    Q_OBJECT
public:
    using Completion = std::function<void(ConflictApplyResult)>;

    explicit GitConflictResolutionService(QObject *parent = nullptr);
    GitConflictResolutionService(GitCommandExecutor *executor, QObject *parent = nullptr);

    void setWorkingDirectory(const QString &path);
    QString workingDirectory() const { return m_workingDirectory; }
    quint64 repositoryGeneration() const { return m_repositoryGeneration; }

    void applyPlanAsync(const ConflictResolutionPlan &plan, Completion completion);
    void applyAllReadyAsync(const QList<ConflictResolutionPlan> &plans,
                            Completion completion);
    void continueOperationAsync(ConflictOperationType operation, Completion completion);
    void abortOperationAsync(ConflictOperationType operation, Completion completion);

    struct FailureInjection {
        bool failWrite = false;
        bool failStage = false;
        bool failVerify = false;
        bool failContinue = false;
    };
    void setFailureInjectionForTesting(FailureInjection injection) {
        m_failureInjection = injection;
    }
    void setBeforeStageHookForTesting(std::function<void()> hook) {
        m_beforeStageHook = std::move(hook);
    }
    void setBeforeContinueHookForTesting(std::function<void()> hook) {
        m_beforeContinueHook = std::move(hook);
    }

private:
    struct OperationContext {
        QString repository;
        quint64 generation = 0;
    };

    bool isCurrent(const OperationContext &context) const;
    bool validPlan(const ConflictResolutionPlan &plan, QString *reason) const;
    static const ConflictFile *findFile(const ConflictSnapshot &snapshot,
                                        const QString &path);
    static bool metadataMatches(const ConflictResolutionPlan &plan,
                                const ConflictFile &file);
    static bool workingTreeMatches(const QString &absolutePath,
                                   const ConflictResolutionPlan &plan);
    static QByteArray encodeTextResult(const QByteArray &currentBytes,
                                       const QString &text, bool *supported);
    static QStringList commandForOperation(ConflictOperationType operation,
                                           bool abort);
    void completeStale(Completion completion) const;
    void rescanAfterMutation(const OperationContext &context,
                             ConflictApplyResult result, Completion completion);
    void verifyContinueOutcome(const OperationContext &context,
                               ConflictOperationType operation,
                               ConflictApplyResult result, Completion completion);
    void applyValidatedPlan(const OperationContext &context,
                            const ConflictResolutionPlan &plan,
                            const ConflictFile &current,
                            Completion completion);
    void stageAndVerify(const OperationContext &context,
                        ConflictApplyResult result, bool alreadyStaged,
                        Completion completion);

    GitCommandExecutor *m_executor = nullptr;
    GitConflictService *m_conflictService = nullptr;
    QString m_workingDirectory;
    quint64 m_repositoryGeneration = 0;
    FailureInjection m_failureInjection;
    std::function<void()> m_beforeStageHook;
    std::function<void()> m_beforeContinueHook;
};

Q_DECLARE_METATYPE(ConflictApplyError)
Q_DECLARE_METATYPE(ConflictApplyResult)
