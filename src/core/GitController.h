#pragma once
#include "GitViewModel.h"
#include "GitBranchService.h"
#include "GitCommitService.h"
#include "GitRemoteService.h"
#include "GitMergeService.h"
#include <QObject>
#include <functional>

class GitController : public QObject {
    Q_OBJECT
public:
    struct OperationResult {
        bool success = false;
        QString error;
        bool rejectedAsUnmerged = false;
    };

    using RefreshCompletion = std::function<void(bool, QString)>;
    using OperationCompletion = std::function<void(OperationResult)>;
    using WorkspaceStatus = GitBranchService::WorkspaceStatus;
    using WorkspaceStatusCompletion = GitBranchService::WorkspaceStatusCompletion;
    using RepoState = GitBranchService::RepoState;

    explicit GitController(QObject *parent = nullptr);
    ~GitController();
    
    void setRepositoryPath(const QString &path);
    GitViewModel *viewModel() const { return m_viewModel; }
    // v4-P2-1.1: empty-repository awareness (forwarded from the service).
    RepoState repoState() const { return m_branchService->repoState(); }
    QString headBranchName() const { return m_branchService->headBranchName(); }
    
    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool createBranch(const QString &name);
    Q_INVOKABLE bool deleteBranch(const QString &name, bool force = false);
    Q_INVOKABLE bool checkoutBranch(const QString &name);
    Q_INVOKABLE bool stageFiles(const QStringList &files);
    Q_INVOKABLE bool unstageFiles(const QStringList &files);
    QStringList stagedFiles();
    Q_INVOKABLE bool createCommit(const QString &message);
    void refreshAsync(RefreshCompletion completion = {});
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
    void stageFilesAsync(const QStringList &files, OperationCompletion completion);
    void unstageAllAsync(OperationCompletion completion);
    void createCommitAsync(const QString &message, OperationCompletion completion);
    void setCommitServiceForTest(GitCommitService *service) {
        m_commitService = service;
        if (m_commitService) {
            m_commitService->setWorkingDirectory(m_repositoryPath);
        }
    }

    // v4-P0-R1.3: working-tree file diff (staged + unstaged) with a
    // binary verdict. Does NOT change git diff behaviour - it only reads
    // the standard `git diff` output.
    struct FileDiffResult {
        bool success = false;
        QString text;
        bool binary = false;
        QString error;
    };
    using FileDiffCompletion = std::function<void(FileDiffResult)>;
    void fileDiffAsync(const QString &path, FileDiffCompletion completion);
    // Deterministic race-test seam. Production leaves this empty and uses a
    // normal GitCommandExecutor whose repository is snapshotted per request.
    using FileDiffExecutorFactory =
        std::function<GitCommandExecutor *(QObject *parent)>;
    void setFileDiffExecutorFactoryForTest(FileDiffExecutorFactory factory) {
        m_fileDiffExecutorFactory = factory;
    }
    QString workingDirectory() const {
        return m_executor ? m_executor->workingDirectory() : QString();
    }
    QString gitExecutablePath() const {
        return m_executor ? m_executor->gitPath() : QString();
    }
    quint64 repositoryGenerationForTest() const {
        return m_repositoryGeneration;
    }

    bool lastDeleteRejectedAsUnmerged() const;
    
    // v4-P1-2 Remote support.
    struct RemoteResult {
        QList<GitRemote> remotes;
        bool success = false;
        // v4-P6-21 ISSUE-05: real diagnostics for failed remote reads.
        QString error;
    };
    using RemotesCompletion = std::function<void(const RemoteResult &)>;
    using RemoteOperationCompletion = GitRemoteService::OperationCompletion;
    void remotesAsync(RemotesCompletion completion);
    void fetchAsync(RemoteOperationCompletion completion);
    void pullAsync(RemoteOperationCompletion completion);
    void pushAsync(RemoteOperationCompletion completion);
    // Deterministic remote-race seam. Ownership remains with the caller.
    void setRemoteServiceForTest(GitRemoteService *service) {
        m_remoteService = service;
        if (m_remoteService) {
            m_remoteService->setWorkingDirectory(m_repositoryPath);
        }
    }

    // v4-P1-4 Merge support.
    using MergeCompletion = GitMergeService::MergeCompletion;
    void mergeAsync(const QString &branch, MergeCompletion completion);
    void abortMergeAsync(std::function<void(bool, const QString &)> completion);

private:
    bool isRepositorySessionCurrent(quint64 generation,
                                    const QString &path) const;
    void refreshAfterOperation(quint64 generation, const QString &path,
                               OperationCompletion completion);
    void failOperation(const GitError &error, bool rejectedAsUnmerged, OperationCompletion completion);

    GitViewModel *m_viewModel;
    GitBranchService *m_branchService;
    GitCommitService *m_commitService;
    GitRemoteService *m_remoteService = nullptr;
    GitMergeService *m_mergeService = nullptr;
    GitCommandExecutor *m_executor = nullptr;
    QString m_repositoryPath;
    quint64 m_repositoryGeneration = 0;
    FileDiffExecutorFactory m_fileDiffExecutorFactory;
};
