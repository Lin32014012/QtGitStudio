#pragma once
#include <QMainWindow>
#include <QString>
#include <functional>
#include <QPointer>
#include "GitEnvironmentService.h"
#include "GitConflictModel.h"
#include "GitRebaseService.h"
#include "NotificationManager.h"

class PracticeBanner;
class QLabel;
class QPushButton;
class QToolButton;
class QTextEdit;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QStackedWidget;
class QFrame;
class QDragEnterEvent;
class QDropEvent;
class QResizeEvent;
class GitController;
class WelcomeWidget;
class ProjectController;
class BranchTreeWidget;
class RepositoryResolver;
class DropOpenSession;
class DiffViewerWidget;
class GitEnvironmentService;
class GitEnvironmentDialog;
class GitHubAccountService;
class GitHubAccountDialog;
class GitTagService;
class GitStashService;
class GitCherryPickService;
class GitRebaseService;
class GitBisectService;
class GitConflictService;
class GitConflictResolutionService;
class ConflictResolverWidget;
class QDialog;
class NotificationManager;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    
    QString getVersion() const;
    GitController *controller() const { return m_controller; }
    bool projectOperationBusyForTest() const { return m_operationBusy; }
    int refreshInvocationCountForTest() const {
        return m_refreshInvocationCount;
    }
    bool repositorySessionReadyForTest() const {
        return m_repositorySessionReady;
    }
    quint64 repositoryGenerationForTest() const {
        return m_repositoryGeneration;
    }

    // v4-P6-21R2 P0: injectable directory picker (public test seam) -
    // production uses the native QFileDialog; tests inject a stub so
    // rapid init clicks never open a native modal picker (which
    // fast-fails 0xc0000409 when a timer closes it on the visible
    // Windows platform).
    using DirectoryPicker = std::function<QString(QWidget *, const QString &,
                                                  const QString &)>;
    void setDirectoryPicker(DirectoryPicker picker) {
        m_directoryPicker = std::move(picker);
    }
    
private slots:
    void onGoHome();
    void onRefresh();
    void onOpenRepository();
    void onSnapshotChanged();
    void onLoadingChanged();
    void onErrorChanged();
    void onOpenProject();
    void onInitProject();
    void onProjectSelected(const QString &path);
    void onCloneRepository();
    void onCreateBranch();
    void onSwitchBranch();
    void onDeleteBranch();
    void onBranchSelected(QTreeWidgetItem *item, int column);
    void onBranchContextMenu(const QPoint &pos);
    void onBranchesChanged();
    void onCurrentBranchChanged();
    void onRefreshRemotes();
    void onFetch();
    void onPull();
    void onPush();
    void onStageAll();
    void onUnstageAll();
    void onStageSelected();
    void onFileSelectionChanged();
    void onCommit();
    void onCommitFinished();
    void onCommitError();
    void onSwitchProject();
    void showWorkspace();
    // v4-P6-15: sync the branch list selection from the relation graph.
    bool selectBranchInTree(const QString &branch);
    // v4-P6-12: dedicated no-repository guide page (three entry
    // buttons, no blank workspace controls).
    void showNoRepository();
    void showWelcome();
    void requestDeleteSelectedBranch();
    void requestSwitchSelectedBranch();
    void onMergeSelected();
    void onAbortMerge();
    void onEnvironmentRequested();
    void onGitHubAccountRequested();
    void onRefreshClicked();
    void onResolveConflicts();
    void onLearnModeClicked();
    // Scaffolding hook: opens the practice dialog at startup when
    // QTGITSTUDIO_OPEN_PRACTICE is set (screenshots / manual QA).
    void openPracticeDialog();
    void onCommitGraphRequested();
    void onTagManagerRequested();
    void onStashRequested();
    void onBisectRequested();
    void onReleaseRequested();
    void showNotification(NotificationManager::Level level,
                          const QString &title, const QString &message);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    // Defined by CMake from project(QtGitStudio VERSION ...).
    QString version() const { return QStringLiteral(QTGITSTUDIO_VERSION); }
    QString selectedBranch() const;
    void loadProject(const QString &path,
                     QPushButton *feedbackButton = nullptr,
                     bool requireGitRepository = false,
                     std::function<void(bool)> onDone = {});
    void performDeleteBranch(const QString &branch, bool force);
    void refreshConflictSummary();
    void closeConflictResolver();
    void updateBranchActionState();
    void updateStageActionState();
    void performCherryPick(const QString &hash);
    // v4-P4-2: rebase flow (confirm -> service -> results).
    void performRebase(const QString &targetBranch);
    void showRebaseConflict(const RebaseResult &result);
    void onRebaseContinue();
    void onRebaseAbort();
    void showDropOverlay(const QString &path);
    void hideDropOverlay();
    void dropSessionResolved(const QString &path, bool isRepo, const QString &branch);
    void promptGitMissing(const GitEnvironmentService::GitEnvironment &env);

    GitController *m_controller;
    ProjectController *m_projectController;
    RepositoryResolver *m_dropResolver;
    DropOpenSession *m_dropSession;
    GitEnvironmentService *m_environmentService;
    GitHubAccountService *m_githubAccountService;
    GitTagService *m_tagService;
    GitStashService *m_stashService;
    GitCherryPickService *m_cherryPickService;
    GitRebaseService *m_rebaseService;
    GitBisectService *m_bisectService;
    NotificationManager *m_notifications;
    QLabel *m_dropOverlay;
    
    // Pages
    QStackedWidget *m_stack;
    WelcomeWidget *m_welcomeWidget;
    QWidget *m_noRepoWidget;   // v4-P6-12: empty repository guide page
    QWidget *m_workspaceWidget;
    // v4-P6-15: history + branch relation widgets.
    QTabWidget *m_changesTab;
    class CommitHistoryWidget *m_commitHistory;
    QTabWidget *m_branchTab;
    
    // Status card widgets
    // Strip carrying the active practice while its sandbox is open.
    PracticeBanner *m_practiceBanner = nullptr;
    QFrame *m_statusCard;
    QLabel *m_projectLabel;
    QLabel *m_gitStatusLabel;
    QLabel *m_branchLabel;
    QLabel *m_fileCountLabel;
    QFrame *m_remoteCard;
    QLabel *m_remotesLabel;
    QPushButton *m_fetchButton;
    QPushButton *m_pullButton;
    QPushButton *m_pushButton;
    
    // Workspace widgets
    QLabel *m_versionLabel;
    QLabel *m_suggestionLabel;
    QPushButton *m_refreshButton;
    QPushButton *m_learnButton;
    QPushButton *m_goHomeButton;
    QPushButton *m_switchProjectButton;
    QTextEdit *m_errorDisplay;
    
    // Branch widgets
    BranchTreeWidget *m_branchTree;
    QLineEdit *m_branchSearchEdit;
    QPushButton *m_switchBranchButton;
    QPushButton *m_createBranchButton;
    QPushButton *m_deleteBranchButton;
    QPushButton *m_mergeButton;
    QPushButton *m_abortMergeButton;
    QPushButton *m_graphButton;
    QPushButton *m_tagButton;
    QPushButton *m_stashButton;
    QPushButton *m_bisectButton;
    QPushButton *m_releaseButton;
    QToolButton *m_workflowButton;
    QString m_pendingBranchSelection;
    QString m_currentProjectPath;
    
    // Commit widgets
    QTreeWidget *m_changesTree;
    DiffViewerWidget *m_diffViewer;
    QFrame *m_conflictBanner = nullptr;
    QLabel *m_conflictSummaryLabel = nullptr;
    QPushButton *m_resolveConflictsButton = nullptr;
    GitConflictService *m_conflictService = nullptr;
    GitConflictResolutionService *m_conflictResolutionService = nullptr;
    ConflictSnapshot m_conflictSnapshot;
    quint64 m_conflictSnapshotGeneration = 0;
    QPointer<QDialog> m_conflictResolverDialog;
    QPushButton *m_stageButton;
    QPushButton *m_stageAllButton;
    QPushButton *m_unstageAllButton;
    QLineEdit *m_commitMessageEdit;
    QPushButton *m_commitButton;
    DirectoryPicker m_directoryPicker;
    // v4-P6-18 P0-2: project-operation re-entrancy guard (quick clicks
    // on 打开仓库/初始化仓库/克隆仓库 must not start parallel loads).
    bool m_operationBusy = false;
    int m_refreshInvocationCount = 0;
    // v4-P6-21R2: commit action single source of truth - enabled only
    // by updateCommitActionState(); guarded at runtime by this flag.
    bool m_commitOperationBusy = false;
    void updateCommitActionState();
    // v4-P6-21 ISSUE-03: repository session state - every repo-bound
    // service must move together and old async callbacks are invalidated.
    quint64 m_repositoryGeneration = 0;
    quint64 m_diffRequestGeneration = 0;
    void applyRepositorySession(const QString &path);
    void clearRepositorySession();
    void completeRepositoryTransition(bool repositoryAvailable);
    // v4-P6-21R2: every repository-bound async completion must verify it
    // still belongs to the CURRENT session before touching UI.
    bool isRepositorySessionCurrent(quint64 generation,
                                    const QString &path) const {
        return generation == m_repositoryGeneration && path == m_currentProjectPath;
    }
    // v4-P6-21R4 P1-02: bump feedback tokens on session switch so late
    // success/error timers from the old repository can never touch the
    // new repository's buttons.
    void invalidateOperationFeedback();
    // v4-P6-21 ISSUE-06: single derived state for the remote buttons -
    // any remote-list callback may only update remoteAvailable; enabled
    // is derived as repoAvailable && remoteAvailable && !busy.
    bool m_remoteOperationBusy = false;
    // v4-P6-21R2: remotes actually configured (set by onRefreshRemotes).
    bool m_remoteAvailable = false;
    // False from the instant a repository switch starts until the new
    // snapshot/branch pipeline has completed (or rollback has finished).
    // Repository-bound UI entries hard-gate on this state.
    bool m_repositorySessionReady = false;
    void finishOperation() { m_operationBusy = false; }
    // v4-P6-21 ISSUE-06: derived remote-button state (implemented in
    // MainWindow.cpp - needs the complete QPushButton type).
    void updateRemoteButtonsEnabled();
};
