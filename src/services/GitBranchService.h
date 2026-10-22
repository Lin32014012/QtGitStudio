#pragma once
#include <QObject>
#include <QStringList>
#include "GitError.h"
#include <functional>

class GitCommandExecutor;

class GitBranchService : public QObject {
    Q_OBJECT
public:
    // v4-P2-1.1: repository branch-state classification.
    enum class RepoState {
        Normal,          // has commits, on a branch
        EmptyRepository, // initialized but no first commit yet
        DetachedHead,    // has commits, HEAD detached
        NoBranch,        // unusual: HEAD does not resolve at all
    };

    struct OperationResult {
        bool success = false;
        GitError error;
        bool rejectedAsUnmerged = false;
    };

    struct WorkspaceStatus {
        bool success = false;
        bool hasTrackedChanges = false;
        bool hasStagedChanges = false;
        bool hasUntrackedFiles = false;
        bool hasConflicts = false;
        QString rawPorcelain;
        GitError error;

        bool isClean() const {
            return success && !hasTrackedChanges && !hasStagedChanges &&
                !hasUntrackedFiles && !hasConflicts;
        }
    };

    using BranchListCompletion = std::function<void(QStringList, GitError)>;
    using BranchCompletion = std::function<void(QString, GitError)>;
    using OperationCompletion = std::function<void(OperationResult)>;
    using WorkspaceStatusCompletion = std::function<void(WorkspaceStatus)>;

    explicit GitBranchService(QObject *parent = nullptr);
    ~GitBranchService();
    
    void setWorkingDirectory(const QString &dir);
    
    QStringList branches();
    QString currentBranch();
    // v4-P2-1.1: classify Normal / EmptyRepository / DetachedHead /
    // NoBranch. Synchronous; cheap (two git probes).
    RepoState repoState();
    // Branch name the HEAD symbolic ref points at (empty when detached
    // or unresolvable). Works for empty repositories where
    // `branch --show-current` returns nothing.
    QString headBranchName();
    bool createBranch(const QString &name);
    bool createBranch(
        const QString &name,
        const QString &startPoint,
        bool checkoutAfterCreation);
    bool deleteBranch(const QString &name, bool force = false);
    bool checkoutBranch(const QString &name);
    void branchesAsync(BranchListCompletion completion);
    void currentBranchAsync(BranchCompletion completion);
    void validateBranchNameAsync(const QString &name, OperationCompletion completion);
    void workspaceStatusAsync(WorkspaceStatusCompletion completion);
    void createBranchAsync(const QString &name, OperationCompletion completion);
    void createBranchAsync(
        const QString &name,
        const QString &startPoint,
        bool checkoutAfterCreation,
        OperationCompletion completion);
    void deleteBranchAsync(const QString &name, bool force, OperationCompletion completion);
    void checkoutBranchAsync(const QString &name, OperationCompletion completion);
    bool lastDeleteRejectedAsUnmerged() const { return m_lastDeleteRejectedAsUnmerged; }
    // Allows deterministic tests to hold the first stage of a multi-step
    // command while the owning repository is retargeted.
    void setExecutorForTest(GitCommandExecutor *executor) { m_executor = executor; }
    quint64 repositoryGenerationForTest() const {
        return m_repositoryGeneration;
    }
    
    GitError lastError() const { return m_lastError; }
    
private:
    bool runGit(const QStringList &args, int *exitCode = nullptr, QString *output = nullptr, QString *stderrOutput = nullptr);
    
    GitCommandExecutor *m_executor;
    GitError m_lastError;
    bool m_lastDeleteRejectedAsUnmerged = false;
    quint64 m_repositoryGeneration = 0;
};
