#include "MainWindow.h"
#include "Theme.h"
#include "ActionButtonFeedback.h"
#include "BranchSwitchErrorDialog.h"
#include "BranchTreeWidget.h"
#include "CreateBranchDialog.h"
#include "DropOpenSession.h"
#include "DiffViewerWidget.h"
#include "ConflictResolverWidget.h"
#include "EncodingDiagnostic.h"
#include "GitController.h"
#include "GitConflictService.h"
#include "GitConflictResolutionService.h"
#include "GitCommitGraphService.h"
#include "CommitGraphWidget.h"
#include "GitEnvironmentService.h"
#include "GitEnvironmentDialog.h"
#include "GitCommandExecutor.h"
#include "GitHubAccountService.h"
#include "CredentialStoreFactory.h"
#include "GitHubAccountDialog.h"
#include "GitTagService.h"
#include "TagManagerDialog.h"
#include "GitStashService.h"
#include "StashWidget.h"
#include "GitCherryPickService.h"
#include "GitRebaseService.h"
#include "GitBisectService.h"
#include "BisectWidget.h"
#include "GitHubReleaseService.h"
#include "ReleaseWidget.h"
#include "LearningWidget.h"
#include "PracticeDialog.h"
#include "NotificationManager.h"
#include "UserFacingError.h"
#include "ProjectController.h"
#include "RepositoryResolver.h"
#include "WelcomeWidget.h"
#include "CloneDialog.h"
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QLineEdit>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QScrollArea>
#include <QStackedWidget>
#include "CommitHistoryWidget.h"
#include "BranchGraphViewerDialog.h"
#include <QTabWidget>
#include <QFrame>
#include <QMenu>
#include <QToolButton>
#include <QDialog>
#include <QMessageBox>
#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QStyle>
#include <QShortcut>
#include <QSizePolicy>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QResizeEvent>
#include <QDesktopServices>

#include <algorithm>

namespace {

// QSS does not watch dynamic properties, so a state flip needs an explicit
// repolish for the new selector to take effect.
void restyle(QWidget *w) {
    if (!w) return;
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}
QString workspaceRiskText(const GitBranchService::WorkspaceStatus &status) {
    if (!status.success) {
        return "切换前无法读取工作区状态，将由 Git 直接判断是否可以安全切换。";
    }
    if (status.isClean()) {
        return {};
    }

    QStringList states;
    if (status.hasTrackedChanges) {
        states.append("未暂存修改");
    }
    if (status.hasStagedChanges) {
        states.append("已暂存修改");
    }
    if (status.hasUntrackedFiles) {
        states.append("未跟踪文件");
    }
    if (status.hasConflicts) {
        states.append("冲突状态");
    }
    return "切换前检查：当前工作区包含" + states.join("、") +
        "。Git 将继续判断本次切换是否安全，不会自动丢弃或强制覆盖修改。";
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowIcon(QApplication::windowIcon());
    m_controller = new GitController(this);
    m_conflictService = new GitConflictService(this);
    m_conflictResolutionService = new GitConflictResolutionService(this);
    m_projectController = new ProjectController(this);
    // v4-P6-7: one-time legacy registry -> IniFormat migration of the
    // recent-projects list (no-op when already migrated / new data exists).
    m_projectController->migrateLegacyRecentProjects();

    // Central widget with stacked layout
    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);
    
    m_stack = new QStackedWidget(this);
    
    // Welcome page
    m_welcomeWidget = new WelcomeWidget(this);
    m_stack->addWidget(m_welcomeWidget);
    
    // Workspace page
    m_workspaceWidget = new QWidget(this);
    QVBoxLayout *workspaceLayout = new QVBoxLayout(m_workspaceWidget);
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    workspaceLayout->setSpacing(12);
    
    // Header with switch project button
    QHBoxLayout *headerLayout = new QHBoxLayout();
    m_goHomeButton = new QPushButton(QStringLiteral("返回首页"), this);
    m_goHomeButton->setObjectName("goHomeButton");
    m_goHomeButton->setToolTip("返回欢迎页，当前项目不会被关闭。");
    // v4-P6-12: no Unicode arrow glyph - standard pixmap icon.
    m_goHomeButton->setIcon(
        Theme::icon(QStringLiteral("back.svg"), QStyle::SP_ArrowBack));
    m_goHomeButton->setIconSize(QSize(16, 16));
    ActionButtonFeedback::configure(
        m_goHomeButton,
        {"#6B7280", "#4B5563", "#374151"},
        {"正在返回…", "已返回首页", "返回失败"});
    m_goHomeButton->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    headerLayout->addWidget(m_goHomeButton);
    
    m_versionLabel = new QLabel("QtGitStudio " + version(), this);
    m_versionLabel->setProperty("role", QStringLiteral("version"));
    headerLayout->addWidget(m_versionLabel);
    headerLayout->addStretch();
    
    m_switchProjectButton = new QPushButton("更换项目", this);
    m_switchProjectButton->setObjectName("switchProjectButton");
    m_switchProjectButton->setToolTip("选择另一个本地 Git 仓库。");
    ActionButtonFeedback::configure(
        m_switchProjectButton,
        {"#64748B", "#475569", "#334155"},
        {"正在打开…", "打开成功", "打开失败"});
    m_switchProjectButton->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    headerLayout->addWidget(m_switchProjectButton);
    
    m_refreshButton = new QPushButton("刷新状态", this);
    m_refreshButton->setObjectName("refreshButton");
    m_refreshButton->setToolTip("重新读取当前仓库的分支和文件状态。");
    ActionButtonFeedback::configure(
        m_refreshButton,
        {"#0D6EFD", "#0B5ED7", "#084298"},
        {"正在刷新…", "刷新成功", "刷新失败"});
    m_refreshButton->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    headerLayout->addWidget(m_refreshButton);

    // v4-P6-4: restore the learning-mode entry (minimal entry only -
    // the original learning system code is gone, no re-design).
    // v4-P6-12/13: plain text only - no Emoji glyphs, no QStyle
    // standard pixmap (SP_DialogHelpButton renders as a red pixel-art
    // icon on Windows), no font files, no external paths.
    m_learnButton = new QPushButton(QStringLiteral("学习模式"), this);
    m_learnButton->setObjectName("learnButton");
    m_learnButton->setToolTip("学习模式：Git 基础与 QtGitStudio 操作指南。");
    m_learnButton->setCursor(Qt::PointingHandCursor);
    // v4-P6-11/12/13: same sizing family as the other header buttons
    // (padding 9px 14px / radius 6px / 14px) with a soft green
    // semantic; full hover/pressed/disabled states.
    m_learnButton->setProperty("role", QStringLiteral("learn"));
    headerLayout->addWidget(m_learnButton);
    workspaceLayout->addLayout(headerLayout);

    // v4-P6-9: fixed top bar above a vertically scrollable repository
    // content area. The top bar never scrolls; everything below it
    // (overview, remote, workflow, branches, file changes, diff) lives
    // inside the QScrollArea so small windows can reach the bottom.
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    workspaceLayout->setSpacing(0);
    auto *contentScroll = new QScrollArea(m_workspaceWidget);
    contentScroll->setObjectName("workspaceScrollArea");
    contentScroll->setWidgetResizable(true);
    contentScroll->setFrameShape(QFrame::NoFrame);
    contentScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto *contentWidget = new QWidget(contentScroll);
    auto *contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(0, 4, 6, 8);
    contentLayout->setSpacing(6);
    contentScroll->setWidget(contentWidget);
    workspaceLayout->addWidget(contentScroll, 1);
    
    // v4-P6-9: compact overview card (2x2 grid instead of four tall
    // stacked cards).
    m_statusCard = new QFrame(this);
    m_statusCard->setObjectName("statusCard");
    m_statusCard->setFrameShape(QFrame::StyledPanel);
    m_statusCard->setProperty("role", QStringLiteral("card"));
    m_statusCard->setAttribute(Qt::WA_StyledBackground, true);
    auto *statusLayout = new QGridLayout(m_statusCard);
    statusLayout->setContentsMargins(10, 8, 10, 8);
    statusLayout->setHorizontalSpacing(24);
    statusLayout->setVerticalSpacing(6);
    // v4-P6-12: balanced columns + word wrap so a long project name
    // never stretches the card beyond the window (no horizontal bar).
    statusLayout->setColumnStretch(0, 1);
    statusLayout->setColumnStretch(1, 1);

    m_projectLabel = new QLabel("当前项目：(未选择)", this);
    m_projectLabel->setObjectName("projectLabel");
    m_projectLabel->setWordWrap(true);
    m_projectLabel->setProperty("role", QStringLiteral("cardTitle"));
    statusLayout->addWidget(m_projectLabel, 0, 0);

    m_gitStatusLabel = new QLabel("Git状态：--", this);
    m_gitStatusLabel->setWordWrap(true);
    m_gitStatusLabel->setProperty("role", QStringLiteral("bodyText"));
    statusLayout->addWidget(m_gitStatusLabel, 0, 1);

    m_branchLabel = new QLabel("当前分支：(未选择)", this);
    m_branchLabel->setObjectName("branchLabel");
    m_branchLabel->setWordWrap(true);
    m_branchLabel->setProperty("role", QStringLiteral("bodyText"));
    statusLayout->addWidget(m_branchLabel, 1, 0);

    m_fileCountLabel = new QLabel("文件变化：0 个文件需要处理", this);
    m_fileCountLabel->setWordWrap(true);
    m_fileCountLabel->setProperty("role", QStringLiteral("bodyText"));
    statusLayout->addWidget(m_fileCountLabel, 1, 1);

    contentLayout->addWidget(m_statusCard);

    // v4-P1-2 Remote area.
    m_remoteCard = new QFrame(this);
    m_remoteCard->setObjectName("remoteCard");
    m_remoteCard->setProperty("role", QStringLiteral("card"));
    m_remoteCard->setAttribute(Qt::WA_StyledBackground, true);
    auto *remoteLayout = new QVBoxLayout(m_remoteCard);
    remoteLayout->setContentsMargins(8, 4, 8, 4);
    remoteLayout->setSpacing(2);
    auto *remoteTitle = new QLabel("远程仓库 (Remote)", this);
    remoteTitle->setProperty("role", QStringLiteral("cardTitle"));
    remoteLayout->addWidget(remoteTitle);
    m_remotesLabel = new QLabel("无远程仓库", this);
    m_remotesLabel->setObjectName("remotesLabel");
    m_remotesLabel->setProperty("role", QStringLiteral("bodyText"));
    m_remotesLabel->setWordWrap(true);
    remoteLayout->addWidget(m_remotesLabel);
    auto *remoteButtons = new QHBoxLayout();
    m_fetchButton = new QPushButton("Fetch", this);
    m_fetchButton->setObjectName("fetchButton");
    m_fetchButton->setEnabled(false);
    m_pullButton = new QPushButton("Pull", this);
    m_pullButton->setObjectName("pullButton");
    m_pullButton->setEnabled(false);
    m_pushButton = new QPushButton("Push", this);
    m_pushButton->setObjectName("pushButton");
    m_pushButton->setEnabled(false);
    ActionButtonFeedback::configure(
        m_fetchButton,
        {"#0F9F9A", "#0D8B87", "#0B7471"},
        {"Fetching...", "Fetch succeeded", "Fetch failed"});
    ActionButtonFeedback::configure(
        m_pullButton,
        {"#0F9F9A", "#0D8B87", "#0B7471"},
        {"Pulling...", "Pull succeeded", "Pull failed"});
    ActionButtonFeedback::configure(
        m_pushButton,
        {"#0F9F9A", "#0D8B87", "#0B7471"},
        {"Pushing...", "Push succeeded", "Push failed"});
    for (QPushButton *b : {m_fetchButton, m_pullButton, m_pushButton}) {
        remoteButtons->addWidget(b);
    }
    remoteLayout->addLayout(remoteButtons);
    contentLayout->addWidget(m_remoteCard);
    
    // Suggestion
    m_suggestionLabel = new QLabel("建议：选择一个项目开始", this);
    m_suggestionLabel->setObjectName("suggestionLabel");
    m_suggestionLabel->setProperty("role", QStringLiteral("hint"));
    m_suggestionLabel->setWordWrap(true);
    contentLayout->addWidget(m_suggestionLabel);
    
    // Branch area
    // v4-P6-1 Navigation: workspace sections under one "Git Workflow"
    // heading (visual grouping only - no layout/business change).
    auto *workflowHeading = new QLabel("Git 工作流", this);
    workflowHeading->setProperty("role", QStringLiteral("sectionHeading"));
    contentLayout->addWidget(workflowHeading);

    QHBoxLayout *branchTitleLayout = new QHBoxLayout();
    QLabel *branchAreaLabel = new QLabel("分支列表", this);
    branchAreaLabel->setProperty("role", QStringLiteral("cardTitle"));
    branchTitleLayout->addWidget(branchAreaLabel);
    branchTitleLayout->addStretch();

    m_branchSearchEdit = new QLineEdit(this);
    m_branchSearchEdit->setObjectName("branchSearchEdit");
    m_branchSearchEdit->setPlaceholderText("搜索分支…");
    m_branchSearchEdit->setClearButtonEnabled(true);
    m_branchSearchEdit->setMaximumWidth(260);
    branchTitleLayout->addWidget(m_branchSearchEdit);
    contentLayout->addLayout(branchTitleLayout);

    m_branchTree = new BranchTreeWidget(this);
    m_branchTree->setObjectName("branchTree");
    m_branchTree->setMinimumHeight(150);
    m_branchTree->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_branchTree->setContextMenuPolicy(Qt::CustomContextMenu);

    // v4-P6-15: branch area - [列表] [关系图] tabs.
    m_branchTab = new QTabWidget(this);
    m_branchTab->setObjectName("branchTab");
    auto *branchListPage = new QWidget(m_branchTab);
    auto *branchListLayout = new QVBoxLayout(branchListPage);
    branchListLayout->setContentsMargins(0, 0, 0, 0);
    branchListLayout->addWidget(m_branchTree);
    m_branchTab->addTab(branchListPage, QStringLiteral("列表"));
    // v4-P6-20: the embedded 关系图 tab is removed - the graph lives
    // only in the standalone BranchGraphViewerDialog ("查看关系图"
    // corner button). BranchRelationWidget/GitCommitGraphService stay
    // for the viewer.
    contentLayout->addWidget(m_branchTab, 2);
    // v4-P6-18: standalone branch-graph viewer (large graphs deserve
    // their own window; the embedded tab stays compact).
    auto *openGraphButton =
        new QPushButton(QStringLiteral("查看关系图"), m_branchTab);
    openGraphButton->setObjectName("openGraphViewerButton");
    openGraphButton->setProperty("role", QStringLiteral("ghost"));
    m_branchTab->setCornerWidget(openGraphButton, Qt::TopRightCorner);
    connect(openGraphButton, &QPushButton::clicked, this, [this]() {
        if (m_currentProjectPath.isEmpty()) {
            return;
        }
        BranchGraphViewerDialog viewer(m_currentProjectPath, this);
        connect(&viewer, &BranchGraphViewerDialog::switchBranchRequested,
                this, [this](const QString &branch) {
                    if (selectBranchInTree(branch)) {
                        requestSwitchSelectedBranch();
                    }
                });
        connect(&viewer, &BranchGraphViewerDialog::deleteBranchRequested,
                this, [this](const QString &branch) {
                    if (selectBranchInTree(branch)) {
                        requestDeleteSelectedBranch();
                    }
                });
        connect(&viewer, &BranchGraphViewerDialog::mergeBranchRequested,
                this, [this](const QString &branch) {
                    if (selectBranchInTree(branch)) {
                        onMergeSelected();
                    }
                });
        connect(&viewer, &BranchGraphViewerDialog::rebaseBranchRequested,
                this, [this](const QString &branch) {
                    if (selectBranchInTree(branch)) {
                        performRebase(branch);
                    }
                });
        viewer.exec();
    });
    
    QHBoxLayout *branchButtonLayout = new QHBoxLayout();
    m_switchBranchButton = new QPushButton("切换分支", this);
    m_switchBranchButton->setObjectName("switchBranchButton");
    ActionButtonFeedback::configure(
        m_switchBranchButton,
        {"#2563EB", "#1D4ED8", "#1E40AF"},
        {"正在切换…", "切换成功", "切换失败"});
    branchButtonLayout->addWidget(m_switchBranchButton);

    m_createBranchButton = new QPushButton("创建分支", this);
    m_createBranchButton->setObjectName("createBranchButton");
    ActionButtonFeedback::configure(
        m_createBranchButton,
        {"#16A34A", "#15803D", "#166534"},
        {"正在创建…", "创建分支成功", "创建失败"});
    branchButtonLayout->addWidget(m_createBranchButton);
    
    m_deleteBranchButton = new QPushButton("删除分支", this);
    m_deleteBranchButton->setObjectName("deleteBranchButton");
    ActionButtonFeedback::configure(
        m_deleteBranchButton,
        {"#DC2626", "#B91C1C", "#991B1B"},
        {"正在删除…", "删除成功", "删除失败"});
    branchButtonLayout->addWidget(m_deleteBranchButton);

    // v4-P1-4 Merge support.
    m_mergeButton = new QPushButton("合并分支", this);
    m_mergeButton->setObjectName("mergeButton");
    m_mergeButton->setEnabled(false);
    ActionButtonFeedback::configure(
        m_mergeButton,
        {"#7C3AED", "#6D28D9", "#5B21B6"},
        {"正在合并…", "合并成功", "合并失败"});
    branchButtonLayout->addWidget(m_mergeButton);
    // v4-P6-11: unified with the branch-button family (same padding /
    // radius / font as ActionButtonFeedback) while keeping a dangerous
    // 灰红 semantic - no more floating orange outlier.
    m_abortMergeButton = new QPushButton("中止合并", this);
    m_abortMergeButton->setObjectName("abortMergeButton");
    m_abortMergeButton->setEnabled(false);
    m_abortMergeButton->setProperty("role", QStringLiteral("dangerOutline"));
    branchButtonLayout->addWidget(m_abortMergeButton);
    contentLayout->addLayout(branchButtonLayout);
    
    // v4-P6-15: the entry point now opens the commit history tab
    // (the graph is no longer user-visible).
    auto *graphRow = new QHBoxLayout();
    auto *graphTitle = new QLabel("提交历史", this);
    graphTitle->setProperty("role", QStringLiteral("cardTitle"));
    m_graphButton = new QPushButton("打开提交历史", this);
    m_graphButton->setObjectName("commitGraphButton");
    m_graphButton->setProperty("role", QStringLiteral("ghost"));
    m_graphButton->setEnabled(false);
    // v4-P3-4: tag management entry next to the commit graph.
    m_tagButton = new QPushButton("标签管理", this);
    m_tagButton->setObjectName("tagManagerButton");
    m_tagButton->setProperty("role", QStringLiteral("ghost"));
    m_tagButton->setEnabled(false);
    // v4-P3-5: stash management entry next to the commit graph.
    m_stashButton = new QPushButton("Stash 管理", this);
    m_stashButton->setObjectName("stashManagerButton");
    m_stashButton->setProperty("role", QStringLiteral("ghost"));
    m_stashButton->setEnabled(false);
    // v4-P4-3: bisect entry next to the commit graph.
    m_bisectButton = new QPushButton("二分查找", this);
    m_bisectButton->setObjectName("bisectButton");
    m_bisectButton->setProperty("role", QStringLiteral("ghost"));
    m_bisectButton->setEnabled(false);
    // v4-P4-4: GitHub release management entry.
    m_releaseButton = new QPushButton("Release 管理", this);
    m_releaseButton->setObjectName("releaseButton");
    m_releaseButton->setProperty("role", QStringLiteral("ghost"));
    m_releaseButton->setEnabled(false);

    // v4-P6-6 regression fix: the five advanced buttons were removed
    // from the graph row in v4-P6-4 but stayed visible as floating
    // widgets at MainWindow's top-left corner (that leaked "Release 管理"
    // onto the Welcome page). They are now hidden - they only exist as
    // menu-action triggers for the workflow menu.
    m_graphButton->hide();
    m_tagButton->hide();
    m_stashButton->hide();
    m_bisectButton->hide();
    m_releaseButton->hide();

    // v4-P6-4: collapse the five advanced entries into one
    // "Git 工作流" menu button to shorten the first screen. The five
    // buttons keep existing behavior/connections; the menu re-triggers
    // their clicked() so no entry is lost.
    m_workflowButton = new QToolButton(this);
    m_workflowButton->setObjectName("workflowButton");
    m_workflowButton->setText(QStringLiteral("Git 工作流 ▾"));
    m_workflowButton->setPopupMode(QToolButton::InstantPopup);
    m_workflowButton->setToolTip(
        QStringLiteral("标签管理 / Stash 管理 / 二分查找 / Release 管理"));
    m_workflowButton->setProperty("role", QStringLiteral("toolbar"));
    m_workflowButton->setEnabled(false);
    auto *workflowMenu = new QMenu(this);
    // v4-P6-15: the commit graph is no longer user-visible - the
    // history tab replaces it; the workflow menu keeps the rest.
    QAction *actTag = workflowMenu->addAction(QStringLiteral("标签管理"));
    QAction *actStash = workflowMenu->addAction(QStringLiteral("Stash 管理"));
    QAction *actBisect = workflowMenu->addAction(QStringLiteral("二分查找"));
    QAction *actRelease = workflowMenu->addAction(QStringLiteral("Release 管理"));
    connect(actTag, &QAction::triggered,
            m_tagButton, &QPushButton::click);
    connect(actStash, &QAction::triggered,
            m_stashButton, &QPushButton::click);
    connect(actBisect, &QAction::triggered,
            m_bisectButton, &QPushButton::click);
    connect(actRelease, &QAction::triggered,
            m_releaseButton, &QPushButton::click);
    // Keep menu items in sync with the (hidden) buttons' enable state.
    connect(workflowMenu, &QMenu::aboutToShow, this,
            [this, workflowMenu, actTag, actStash, actBisect,
             actRelease]() {
                actTag->setEnabled(m_tagButton->isEnabled());
                actStash->setEnabled(m_stashButton->isEnabled());
                actBisect->setEnabled(m_bisectButton->isEnabled());
                actRelease->setEnabled(m_releaseButton->isEnabled());
            });
    m_workflowButton->setMenu(workflowMenu);

    graphRow->addWidget(graphTitle);
    graphRow->addStretch();
    graphRow->addWidget(m_workflowButton);
    contentLayout->addLayout(graphRow);
    connect(m_graphButton, &QPushButton::clicked,
            this, &MainWindow::onCommitGraphRequested);
    connect(m_tagButton, &QPushButton::clicked,
            this, &MainWindow::onTagManagerRequested);
    connect(m_stashButton, &QPushButton::clicked,
            this, &MainWindow::onStashRequested);
    connect(m_bisectButton, &QPushButton::clicked,
            this, &MainWindow::onBisectRequested);
    connect(m_releaseButton, &QPushButton::clicked,
            this, &MainWindow::onReleaseRequested);

    // v4-P6-15: changes / commit-history tabs (commit history replaces
    // the graph as the main presentation).
    m_changesTab = new QTabWidget(this);
    m_changesTab->setObjectName("changesTab");
    auto *changesPage = new QWidget(m_changesTab);
    auto *changesPageLayout = new QVBoxLayout(changesPage);
    changesPageLayout->setContentsMargins(0, 0, 0, 0);
    changesPageLayout->setSpacing(6);
    auto *commitAreaLabel = new QLabel("文件变化", changesPage);
    commitAreaLabel->setProperty("role", QStringLiteral("cardTitle"));
    changesPageLayout->addWidget(commitAreaLabel);

    m_conflictBanner = new QFrame(changesPage);
    m_conflictBanner->setObjectName(QStringLiteral("conflictSummaryBanner"));
    m_conflictBanner->setFrameShape(QFrame::StyledPanel);
    m_conflictBanner->setProperty("role", QStringLiteral("bannerDanger"));
    m_conflictBanner->setAttribute(Qt::WA_StyledBackground, true);
    auto *conflictBannerLayout = new QHBoxLayout(m_conflictBanner);
    conflictBannerLayout->setContentsMargins(8, 4, 8, 4);
    m_conflictSummaryLabel = new QLabel(m_conflictBanner);
    m_conflictSummaryLabel->setObjectName(QStringLiteral("conflictSummaryLabel"));
    m_conflictSummaryLabel->setProperty("role", QStringLiteral("bodyText"));
    m_conflictSummaryLabel->setProperty("state", QStringLiteral("danger"));
    conflictBannerLayout->addWidget(m_conflictSummaryLabel);
    conflictBannerLayout->addStretch();
    m_resolveConflictsButton = new QPushButton(QStringLiteral("解决冲突"), m_conflictBanner);
    m_resolveConflictsButton->setObjectName(QStringLiteral("resolveConflictsButton"));
    m_resolveConflictsButton->setToolTip(QStringLiteral("打开只生成内存方案的冲突解决器。"));
    conflictBannerLayout->addWidget(m_resolveConflictsButton);
    m_conflictBanner->hide();
    changesPageLayout->addWidget(m_conflictBanner);

    m_changesTree = new QTreeWidget(changesPage);
    m_changesTree->setObjectName("changesTree");
    m_changesTree->setHeaderLabels({"状态", "文件"});
    m_changesTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_changesTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_changesTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_changesTree->setRootIsDecorated(false);
    m_changesTree->setMinimumHeight(120);
    m_changesTree->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    changesPageLayout->addWidget(m_changesTree, 1);

    // v4-P0-R1.3: diff viewer below the change list.
    m_diffViewer = new DiffViewerWidget(changesPage);
    m_diffViewer->setObjectName("fileDiffViewer");
    m_diffViewer->setMinimumHeight(140);
    changesPageLayout->addWidget(m_diffViewer, 1);
    m_changesTab->addTab(changesPage, QStringLiteral("文件变化"));

    m_commitHistory = new CommitHistoryWidget(m_changesTab);
    m_commitHistory->setObjectName("commitHistoryWidget");
    m_changesTab->addTab(m_commitHistory, QStringLiteral("提交历史"));
    // v4-P6-20: the workspace gets more room so the commit history is
    // never squeezed to a single row.
    contentLayout->addWidget(m_changesTab, 2);
    
    QHBoxLayout *stageLayout = new QHBoxLayout();
    m_stageButton = new QPushButton("缓存选中", this);
    m_stageButton->setObjectName("stageButton");
    m_stageButton->setEnabled(false);
    ActionButtonFeedback::configure(
        m_stageButton,
        {"#0F9F9A", "#0D8B87", "#0B7471"},
        {"正在缓存…", "缓存成功", "缓存失败"});
    stageLayout->addWidget(m_stageButton);
    m_stageAllButton = new QPushButton("全部暂存", this);
    m_stageAllButton->setObjectName("stageAllButton");
    m_stageAllButton->setEnabled(false);
    ActionButtonFeedback::configure(
        m_stageAllButton,
        {"#0F9F9A", "#0D8B87", "#0B7471"},
        {"正在暂存…", "暂存成功", "暂存失败"});
    stageLayout->addWidget(m_stageAllButton);
    m_unstageAllButton = new QPushButton("全部取消暂存", this);
    m_unstageAllButton->setObjectName("unstageAllButton");
    m_unstageAllButton->setEnabled(false);
    ActionButtonFeedback::configure(
        m_unstageAllButton,
        {"#64748B", "#475569", "#334155"},
        {"正在取消暂存…", "取消暂存成功", "取消暂存失败"});
    stageLayout->addWidget(m_unstageAllButton);
    contentLayout->addLayout(stageLayout);
    
    QLabel *commitLabel = new QLabel("提交说明：", this);
    commitLabel->setProperty("role", QStringLiteral("bodyText"));
    contentLayout->addWidget(commitLabel);
    
    m_commitMessageEdit = new QLineEdit(this);
    m_commitMessageEdit->setObjectName("commitMessageEdit");
    m_commitMessageEdit->setPlaceholderText("请输入提交说明...");
    contentLayout->addWidget(m_commitMessageEdit);
    // v4-P6-21R1 ISSUE-12A: typing a message must re-evaluate the
    // commit button (it is disabled while the message is empty).
    connect(m_commitMessageEdit, &QLineEdit::textChanged, this,
            &MainWindow::updateStageActionState);
    
    m_commitButton = new QPushButton("保存版本", this);
    m_commitButton->setObjectName("commitButton");
    ActionButtonFeedback::configure(
        m_commitButton,
        {"#1D4ED8", "#1E40AF", "#1E3A8A"},
        {"正在保存…", "保存成功", "保存失败"});
    contentLayout->addWidget(m_commitButton);
    
    // Error display
    m_errorDisplay = new QTextEdit(this);
    m_errorDisplay->setObjectName("errorDisplay");
    m_errorDisplay->setReadOnly(true);
    m_errorDisplay->setPlaceholderText("错误信息将显示在这里...");
    m_errorDisplay->setMaximumHeight(80);
    m_errorDisplay->setProperty("role", QStringLiteral("errorBox"));
    contentLayout->addWidget(m_errorDisplay);
    
    m_stack->addWidget(m_workspaceWidget);

    // v4-P6-12: dedicated no-repository guide page - shown whenever the
    // workspace is requested without a loaded repository, so the user
    // never sees a blank workspace with empty controls.
    m_noRepoWidget = new QWidget(this);
    m_noRepoWidget->setObjectName("noRepoWidget");
    auto *noRepoLayout = new QVBoxLayout(m_noRepoWidget);
    noRepoLayout->setContentsMargins(40, 40, 40, 40);
    noRepoLayout->setSpacing(14);
    auto *noRepoTop = new QHBoxLayout();
    auto *noRepoBack =
        new QPushButton(QStringLiteral("返回首页"), m_noRepoWidget);
    noRepoBack->setObjectName("noRepoBackButton");
    noRepoBack->setCursor(Qt::PointingHandCursor);
    noRepoBack->setProperty("role", QStringLiteral("secondary"));
    noRepoTop->addWidget(noRepoBack);
    noRepoTop->addStretch();
    noRepoLayout->addLayout(noRepoTop);
    connect(noRepoBack, &QPushButton::clicked, this,
            &MainWindow::onGoHome);
    auto *noRepoTip = new QLabel(
        QStringLiteral("尚未打开 Git 仓库"), m_noRepoWidget);
    noRepoTip->setProperty("role", QStringLiteral("pageTitle"));
    noRepoLayout->addWidget(noRepoTip);
    auto *noRepoHint = new QLabel(
        QStringLiteral("请选择一种方式开始：打开已有仓库、创建新仓库"
                       "或从远程克隆。"),
        m_noRepoWidget);
    noRepoHint->setWordWrap(true);
    noRepoHint->setProperty("role", QStringLiteral("bodyText"));
    noRepoLayout->addWidget(noRepoHint);
    noRepoLayout->addSpacing(6);
    auto *openRepoButton =
        new QPushButton(QStringLiteral("打开本地仓库"), m_noRepoWidget);
    openRepoButton->setObjectName("noRepoOpenButton");
    auto *initRepoButton =
        new QPushButton(QStringLiteral("初始化仓库"), m_noRepoWidget);
    initRepoButton->setObjectName("noRepoInitButton");
    auto *cloneRepoButton =
        new QPushButton(QStringLiteral("克隆仓库"), m_noRepoWidget);
    cloneRepoButton->setObjectName("noRepoCloneButton");
    for (QPushButton *b : {openRepoButton, initRepoButton,
                           cloneRepoButton}) {
        b->setCursor(Qt::PointingHandCursor);
        b->setProperty("role", QStringLiteral("primaryLarge"));
        b->setMinimumWidth(180);
        noRepoLayout->addWidget(b);
    }
    noRepoLayout->addStretch();
    connect(openRepoButton, &QPushButton::clicked,
            this, &MainWindow::onOpenProject);
    connect(initRepoButton, &QPushButton::clicked,
            this, &MainWindow::onInitProject);
    connect(cloneRepoButton, &QPushButton::clicked,
            this, &MainWindow::onCloneRepository);
    m_stack->addWidget(m_noRepoWidget);
    
    mainLayout->addWidget(m_stack);
    setCentralWidget(centralWidget);
    
    setWindowTitle("QtGitStudio");
    resize(720, 1024);
    
    // Connect WelcomeWidget signals
    connect(m_welcomeWidget, &WelcomeWidget::openProjectRequested, this, &MainWindow::onOpenProject);
    connect(m_welcomeWidget, &WelcomeWidget::initProjectRequested, this, &MainWindow::onInitProject);
    connect(m_welcomeWidget, &WelcomeWidget::cloneProjectRequested, this, &MainWindow::onCloneRepository);
    connect(m_welcomeWidget, &WelcomeWidget::projectSelected, this, &MainWindow::onProjectSelected);
    
    // Connect workspace signals
    connect(m_goHomeButton, &QPushButton::clicked, this, &MainWindow::onGoHome);
    // v4-P6-20 P1-2: single refresh pipeline - onRefreshClicked only.
    // (Previously both onRefresh and onRefreshClicked were connected,
    // so every click ran two concurrent refreshes: the second one hit
    // the view-model "already running" guard and showed 刷新失败, then
    // the first one showed 刷新成功 - the hard-coded fail->success
    // sequence users reported.)
    connect(m_refreshButton, &QPushButton::clicked,
            this, &MainWindow::onRefreshClicked);
    connect(m_switchProjectButton, &QPushButton::clicked, this, &MainWindow::onSwitchProject);
    connect(m_switchBranchButton, &QPushButton::clicked, this, &MainWindow::requestSwitchSelectedBranch);
    connect(m_createBranchButton, &QPushButton::clicked, this, &MainWindow::onCreateBranch);
    connect(m_deleteBranchButton, &QPushButton::clicked, this, &MainWindow::requestDeleteSelectedBranch);
    connect(m_branchTree, &QTreeWidget::itemDoubleClicked, this, &MainWindow::onBranchSelected);
    connect(m_branchTree, &QTreeWidget::itemSelectionChanged, this, &MainWindow::updateBranchActionState);
    connect(m_branchTree, &QTreeWidget::customContextMenuRequested, this, &MainWindow::onBranchContextMenu);
    connect(m_branchSearchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_branchTree->setFilterText(text);
        updateBranchActionState();
    });
    auto *clearBranchSearch = new QShortcut(QKeySequence(Qt::Key_Escape), m_branchSearchEdit);
    clearBranchSearch->setContext(Qt::WidgetShortcut);
    connect(clearBranchSearch, &QShortcut::activated, m_branchSearchEdit, &QLineEdit::clear);
    connect(m_stageAllButton, &QPushButton::clicked, this, &MainWindow::onStageAll);
    connect(m_unstageAllButton, &QPushButton::clicked, this, &MainWindow::onUnstageAll);
    connect(m_commitButton, &QPushButton::clicked, this, &MainWindow::onCommit);
    connect(m_resolveConflictsButton, &QPushButton::clicked, this,
            &MainWindow::onResolveConflicts);
    connect(m_controller->viewModel(), &GitViewModel::snapshotChanged, this, &MainWindow::onSnapshotChanged);
    connect(m_controller->viewModel(), &GitViewModel::loadingChanged, this, &MainWindow::onLoadingChanged);
    connect(m_controller->viewModel(), &GitViewModel::errorChanged, this, &MainWindow::onErrorChanged);
    connect(m_controller->viewModel(), &GitViewModel::branchesChanged, this, &MainWindow::onBranchesChanged);
    connect(m_controller->viewModel(), &GitViewModel::currentBranchChanged, this, &MainWindow::onCurrentBranchChanged);
    connect(m_controller->viewModel(), &GitViewModel::commitFinished, this, &MainWindow::onCommitFinished);
    connect(m_controller->viewModel(), &GitViewModel::commitError, this, &MainWindow::onCommitError);
    
    // Show welcome page
    updateBranchActionState();
    // v4-P0-R1.3 stability: drag-and-drop open with unified lifecycle.
    setAcceptDrops(true);
    m_dropResolver = new RepositoryResolver(this);
    m_dropSession = new DropOpenSession(m_dropResolver, this);
    connect(m_dropSession, &DropOpenSession::resolved,
            this, &MainWindow::dropSessionResolved);
    // v4-P1-4.1: rejected (busy or watchdog timeout) must never leave the
    // overlay visible or block future drops.
    connect(m_dropSession, &DropOpenSession::rejected,
            this, [this](const QString &reason) {
                hideDropOverlay();
                m_errorDisplay->setText(reason);
                m_errorDisplay->show();
            });
    m_dropOverlay = new QLabel(this);
    m_dropOverlay->setObjectName("dropOverlay");
    m_dropOverlay->setAlignment(Qt::AlignCenter);
    m_dropOverlay->setProperty("role", QStringLiteral("dropOverlay"));
    m_dropOverlay->setText("正在打开仓库…");
    m_dropOverlay->hide();
    connect(m_changesTree, &QTreeWidget::itemSelectionChanged,
            this, &MainWindow::updateStageActionState);
    connect(m_changesTree, &QTreeWidget::itemSelectionChanged,
            this, &MainWindow::onFileSelectionChanged);
    connect(m_stageButton, &QPushButton::clicked,
            this, &MainWindow::onStageSelected);
    // v4-P1-2 Remote buttons.
    connect(m_fetchButton, &QPushButton::clicked,
            this, &MainWindow::onFetch);
    connect(m_pullButton, &QPushButton::clicked,
            this, &MainWindow::onPull);
    connect(m_pushButton, &QPushButton::clicked,
            this, &MainWindow::onPush);
    // v4-P1-4 Merge buttons.
    connect(m_mergeButton, &QPushButton::clicked,
            this, &MainWindow::onMergeSelected);
    // v4-P6-21 ISSUE-14: refreshButton is connected exactly ONCE
    // (see constructor, onRefreshClicked). The duplicate connection
    // below was removed - two slots per click raced and produced the
    // fail->success sequence.
    // v4-P6-4: learning-mode entry restored (minimal entry only).
    connect(m_learnButton, &QPushButton::clicked,
            this, &MainWindow::onLearnModeClicked);
    // Screenshot / QA scaffolding - inert unless the variable is set.
    if (qEnvironmentVariableIsSet("QTGITSTUDIO_OPEN_PRACTICE")) {
        QTimer::singleShot(400, this, [this]() { openPracticeDialog(); });
    }
    connect(m_abortMergeButton, &QPushButton::clicked,
            this, &MainWindow::onAbortMerge);

    // v4-P2-2: optional GitHub account (never required for local use).
    m_githubAccountService = new GitHubAccountService(this);
    // v4-P6-18 P1-1: persist the account in the Windows Credential
    // Manager (no plain-text token files); restore() picks it up so a
    // restart keeps showing "已连接".
    m_githubAccountService->setCredentialStore(createPlatformCredentialStore(this));
    m_githubAccountService->restore();
    // v4-P6-18 P1-2: keep the home dashboard in sync after login/logout.
    connect(m_githubAccountService, &GitHubAccountService::stateChanged,
            this, [this]() {
                m_welcomeWidget->setGitHubStatus(
                    m_githubAccountService->isLoggedIn(),
                    m_githubAccountService->username());
            });
    // v4-P6-15: home dashboard GitHub status.
    m_welcomeWidget->setGitHubStatus(
        m_githubAccountService->isLoggedIn(),
        m_githubAccountService->username());
    connect(m_welcomeWidget, &WelcomeWidget::githubAccountRequested,
            this, &MainWindow::onGitHubAccountRequested);
    // v4-P2-1: git environment detection at startup.
    m_environmentService = new GitEnvironmentService(this);
    connect(m_welcomeWidget, &WelcomeWidget::environmentRequested,
            this, &MainWindow::onEnvironmentRequested);
    m_environmentService->detectAsync(
        {}, [this](const GitEnvironmentService::GitEnvironment &env) {
            // v4-P6-15: home dashboard status.
            m_welcomeWidget->setEnvironmentStatus(env.available,
                                                  env.version);
            if (!env.available) {
                promptGitMissing(env);
            }
        });

    // v4-P3-4: tag management service (per loaded repository).
    m_tagService = new GitTagService(this);

    // v4-P3-5: stash management service (per loaded repository).
    m_stashService = new GitStashService(this);

    // v4-P4-1: cherry-pick service (per loaded repository).
    m_cherryPickService = new GitCherryPickService(this);

    // v4-P4-2: rebase service (per loaded repository).
    m_rebaseService = new GitRebaseService(this);

    // v4-P4-3: bisect service (per loaded repository).
    m_bisectService = new GitBisectService(this);

    // v4-P6-1: unified notifications (single presentation point).
    m_notifications = new NotificationManager(this);
    connect(m_notifications, &NotificationManager::notificationShown,
            this, &MainWindow::showNotification);

    showWelcome();
}

MainWindow::~MainWindow() {}

QString MainWindow::getVersion() const {
    return version();
}

void MainWindow::onRefresh() {
    if (ActionButtonFeedback::isActive(m_refreshButton) ||
        m_controller->viewModel()->isLoading()) {
        return;
    }

    ActionButtonFeedback::showBusy(m_refreshButton, "正在刷新…");
    // v4-P6-21R2: session fence - a refresh started for repo A must not
    // update the UI after the user switched to repo B.
    const quint64 generation = m_repositoryGeneration;
    const QString repoPath = m_currentProjectPath;
    m_controller->refreshAsync(
        [this, generation, repoPath](bool success, const QString &error) {
            if (!isRepositorySessionCurrent(generation, repoPath)) {
                return;
            }
            ActionButtonFeedback::setReady(m_refreshButton, true);
            if (success) {
                ActionButtonFeedback::showSuccess(m_refreshButton, "刷新成功", 800);
                return;
            }
            const QString details = error.trimmed().isEmpty() ? "Git 未返回可用的错误详情。" : error;
            m_errorDisplay->setText("刷新失败\n\n" + details);
            m_errorDisplay->show();
            ActionButtonFeedback::showError(m_refreshButton, "刷新失败", 1300);
        });
}

void MainWindow::onSwitchProject() {
    QString dir = QFileDialog::getExistingDirectory(this, "选择项目目录", QDir::rootPath());
    if (dir.isEmpty()) {
        ActionButtonFeedback::reset(m_switchProjectButton);
        return;
    }
    ActionButtonFeedback::showBusy(m_switchProjectButton, "正在打开…");
    loadProject(dir, m_switchProjectButton, true);
}

void MainWindow::onOpenRepository() {
    QString dir = QFileDialog::getExistingDirectory(this, "选择 Git 仓库", QDir::rootPath());
    if (!dir.isEmpty()) {
        onProjectSelected(dir);
    }
}

void MainWindow::onSnapshotChanged() {
    auto snap = m_controller->viewModel()->snapshot();
    
    // Update status card
    int fileCount = snap.files.size();
    m_fileCountLabel->setText("文件变化：" + QString::number(fileCount) + " 个文件需要处理");
    m_fileCountLabel->setProperty(
        "state", fileCount > 0 ? QStringLiteral("warn") : QStringLiteral("ok"));
    restyle(m_fileCountLabel);
    
    // Update changes tree with colors
    m_changesTree->clear();
    for (const auto &file : snap.files) {
        QTreeWidgetItem *item = new QTreeWidgetItem(m_changesTree);
        
        QString status;
        QColor color;
        switch (file.status) {
            case FileStatus::Modified:
                status = "M 修改";
                color = QColor("#EA580C"); // Orange
                break;
            case FileStatus::Added:
                status = "A 新增";
                color = QColor("#16A34A"); // Green
                break;
            case FileStatus::Deleted:
                status = "D 删除";
                color = QColor("#DC2626"); // Red
                break;
            case FileStatus::Renamed:
                status = "R 重命名";
                color = QColor("#7C3AED"); // Purple
                break;
            case FileStatus::Conflict:
                status = "C 冲突";
                color = QColor("#991B1B"); // Dark red
                break;
            case FileStatus::Untracked:
                status = "? 未跟踪";
                color = QColor("#D97706"); // Orange-yellow
                break;
        }
        
        item->setText(0, status);
        item->setText(1, file.path);
        item->setData(0, Qt::UserRole, file.path);
        item->setToolTip(1, file.path);
        item->setForeground(0, color);
        item->setForeground(1, color);
    }
    
    // Update suggestion
    // v4-P6-21 ISSUE-12: distinguish Empty (no commits yet) from Clean
    // (committed, working tree has no changes).
    if (fileCount > 0) {
        m_suggestionLabel->setText("建议：审查变更并提交");
        m_suggestionLabel->setProperty("state", QStringLiteral("info"));
        restyle(m_suggestionLabel);
    } else if (m_controller->repoState()
               == GitController::RepoState::EmptyRepository) {
        // An unborn repository is the only empty state.  Detached HEAD has
        // no branch name, but it still resolves to a commit and is clean.
        m_suggestionLabel->setText(
            "建议：仓库为空，完成第一次 Commit 后这里将显示变更。");
        m_suggestionLabel->setProperty("state", QStringLiteral("warn"));
        restyle(m_suggestionLabel);
    } else {
        m_suggestionLabel->setText("✓ 工作区干净");
        m_suggestionLabel->setProperty("state", QStringLiteral("ok"));
        restyle(m_suggestionLabel);
        // v4-P6-21 ISSUE-12: the branch tree populates right after the
        // snapshot and its default directory-node selection would
        // overwrite this hint - re-assert it after the branch-tree
        // selection chain settles so the clean state stays visible.
        QTimer::singleShot(50, this, [this]() {
            if (m_controller->viewModel()->snapshot().files.isEmpty()
                && m_controller->repoState()
                       != GitController::RepoState::EmptyRepository) {
                m_suggestionLabel->setText("✓ 工作区干净");
                m_suggestionLabel->setProperty("state", QStringLiteral("ok"));
                restyle(m_suggestionLabel);
            }
        });
    }
    updateStageActionState();
    refreshConflictSummary();
}

void MainWindow::refreshConflictSummary() {
    if (!m_conflictService || m_currentProjectPath.isEmpty()) {
        if (m_conflictBanner) {
            m_conflictBanner->hide();
        }
        return;
    }
    const auto snapshot = m_controller->viewModel()->snapshot();
    const bool reportsConflict = std::any_of(
        snapshot.files.cbegin(), snapshot.files.cend(), [](const auto &file) {
            return file.status == FileStatus::Conflict;
        });
    if (!reportsConflict) {
        m_conflictSnapshot = {};
        m_conflictSnapshotGeneration = 0;
        if (m_conflictBanner) {
            m_conflictBanner->hide();
        }
        return;
    }

    const quint64 generation = m_repositoryGeneration;
    const quint64 serviceGeneration = m_conflictService->repositoryGeneration();
    const QString repository = m_currentProjectPath;
    m_conflictService->scanConflictsAsync(
        [this, generation, serviceGeneration, repository](ConflictScanResult result) {
            if (result.stale || !isRepositorySessionCurrent(generation, repository)
                || !m_conflictService
                || serviceGeneration != m_conflictService->repositoryGeneration()) {
                return;
            }
            if (result.error.hasError() || !result.snapshot.hasConflicts) {
                m_conflictSnapshot = {};
                m_conflictSnapshotGeneration = 0;
                if (m_conflictBanner) {
                    m_conflictBanner->hide();
                }
                return;
            }
            m_conflictSnapshot = std::move(result.snapshot);
            m_conflictSnapshotGeneration = generation;
            m_conflictSummaryLabel->setText(
                QStringLiteral("检测到 %1 个冲突文件").arg(m_conflictSnapshot.conflictCount));
            m_resolveConflictsButton->setEnabled(true);
            m_conflictBanner->show();
        });
}

void MainWindow::onResolveConflicts() {
    if (!m_conflictService || !m_conflictSnapshot.hasConflicts
        || m_conflictSnapshotGeneration != m_repositoryGeneration
        || m_conflictSnapshot.repositoryPath != m_currentProjectPath) {
        return;
    }
    closeConflictResolver();
    auto *dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("conflictResolverDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Git 冲突解决"));
    dialog->resize(1120, 760);
    auto *layout = new QVBoxLayout(dialog);
    auto *resolver = new ConflictResolverWidget(m_conflictService, dialog);
    resolver->setObjectName(QStringLiteral("conflictResolverWidget"));
    resolver->setResolutionService(m_conflictResolutionService);
    resolver->setSnapshot(m_conflictSnapshot);
    layout->addWidget(resolver);
    m_conflictResolverDialog = dialog;
    const quint64 generation = m_repositoryGeneration;
    const QString repository = m_currentProjectPath;
    connect(resolver, &ConflictResolverWidget::conflictSnapshotChanged, this,
            [this, generation, repository](const ConflictSnapshot &snapshot) {
                if (!isRepositorySessionCurrent(generation, repository)) {
                    return;
                }
                m_conflictSnapshot = snapshot;
                m_conflictSnapshotGeneration = snapshot.hasConflicts ? generation : 0;
                if (!snapshot.hasConflicts) {
                    m_conflictBanner->hide();
                    return;
                }
                m_conflictSummaryLabel->setText(
                    QStringLiteral("检测到 %1 个冲突文件").arg(snapshot.conflictCount));
                m_conflictBanner->show();
            });
    connect(dialog, &QObject::destroyed, this, [this, dialog]() {
        if (m_conflictResolverDialog == dialog) {
            m_conflictResolverDialog = nullptr;
        }
    });
    dialog->show();
}

void MainWindow::closeConflictResolver() {
    if (!m_conflictResolverDialog) {
        return;
    }
    QPointer<QDialog> dialog = m_conflictResolverDialog;
    m_conflictResolverDialog = nullptr;
    dialog->close();
}

void MainWindow::onLoadingChanged() {
    bool loading = m_controller->viewModel()->isLoading();
    ActionButtonFeedback::setReady(m_refreshButton, !loading);
}

void MainWindow::onErrorChanged() {
    QString error = m_controller->viewModel()->error();
    if (error.isEmpty()) {
        m_errorDisplay->clear();
        m_errorDisplay->hide();
    } else {
        m_errorDisplay->setText(error);
        m_errorDisplay->show();
    }
}

void MainWindow::onOpenProject() {
    QString dir = QFileDialog::getExistingDirectory(this, "选择项目目录", QDir::rootPath());
    if (!dir.isEmpty()) {
        onProjectSelected(dir);
    }
}

void MainWindow::onInitProject() {
    // v4-P6-18 P0-2: block re-entrant init while an operation runs.
    if (m_operationBusy) {
        return;
    }
    // v4-P6-21R2 P0: go through the injectable picker (tests never
    // open the native QFileDialog - it fast-fails when closed by a
    // timer on the visible Windows platform).
    QString dir = m_directoryPicker
        ? m_directoryPicker(this, "选择要初始化的目录", QDir::rootPath())
        : QFileDialog::getExistingDirectory(this, "选择要初始化的目录",
                                            QDir::rootPath());
    if (dir.isEmpty()) {
        return;
    }

    m_operationBusy = true;
    // v4-P6-18: the welcome dashboard uses ActionBlock (no "initButton"
    // objectName) - feedback is optional and null-safe now.
    auto *initButton = m_welcomeWidget->findChild<QPushButton *>("initButton");
    ActionButtonFeedback::showBusy(initButton, "正在初始化…");
    m_projectController->detectProjectAsync(dir, [this, dir, initButton](ProjectController::ProjectInfo info) {
        if (info.isGitRepo) {
            if (QMessageBox::question(
                    this,
                    "目录已初始化",
                    "该目录已经是 Git 仓库，是否直接打开？",
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::Yes) == QMessageBox::Yes) {
                ActionButtonFeedback::showSuccess(initButton, "初始化成功");
                m_operationBusy = false;
                onProjectSelected(dir);
            } else {
                ActionButtonFeedback::reset(initButton);
                m_operationBusy = false;
            }
            return;
        }

        m_projectController->initializeGitAsync(
            dir,
            [this, dir, initButton](bool success, const QString &error) {
                if (!success) {
                    ActionButtonFeedback::showError(initButton, "初始化失败");
                    QMessageBox::critical(this, "初始化仓库失败", error);
                    m_operationBusy = false;
                    return;
                }
                ActionButtonFeedback::showSuccess(initButton, "初始化成功");
                m_operationBusy = false;
                onProjectSelected(dir);
            });
    });
}

void MainWindow::onProjectSelected(const QString &path) {
    loadProject(path);
}

void MainWindow::loadProject(
    const QString &path,
    QPushButton *feedbackButton,
    bool requireGitRepository,
    std::function<void(bool)> onDone) {
    // v4-P6-18 P0-2: no parallel loads - a second call while a load is
    // running is dropped (the first one finishes normally).
    if (m_operationBusy) {
        if (onDone) {
            onDone(false);
        }
        return;
    }
    m_operationBusy = true;
    m_projectController->detectProjectAsync(path, [this, path, feedbackButton, requireGitRepository, onDone](ProjectController::ProjectInfo info) {
        if (info.isGitRepo) {
            const QString previousProjectPath = m_currentProjectPath;
            // v4-P6-21 ISSUE-03: unified session switch (controller +
            // all repo-bound services move together, generation++).
            applyRepositorySession(path);
            const quint64 generation = m_repositoryGeneration;
            m_controller->refreshAsync([this, path, previousProjectPath, feedbackButton, onDone, generation](bool success, const QString &error) {
                // v4-P6-21 ISSUE-03: a callback from an old session is
                // stale - never touch the current UI.
                if (!isRepositorySessionCurrent(generation, path)) {
                    if (onDone) onDone(false);
                    return;
                }
                if (!success) {
                    if (!previousProjectPath.isEmpty()) {
                        applyRepositorySession(previousProjectPath);
                        completeRepositoryTransition(true);
                    } else {
                        clearRepositorySession();
                        completeRepositoryTransition(false);
                    }
                    const QString details = error.trimmed().isEmpty()
                        ? "Git 未返回可用的错误详情。"
                        : error;
                    m_errorDisplay->setText("打开项目失败\n\n" + details);
                    m_errorDisplay->show();
                    if (feedbackButton) {
                        ActionButtonFeedback::setReady(feedbackButton, true);
                        ActionButtonFeedback::showError(feedbackButton, "打开失败", 1300);
                    }
                    hideDropOverlay();
                    finishOperation();  // v4-P6-18 P0-2
                    if (onDone) onDone(false);
                    return;
                }

                m_currentProjectPath = path;
                m_projectLabel->setText("当前项目：" + QFileInfo(path).fileName());
                m_projectController->addRecentProject(path);
                m_welcomeWidget->updateRecentProjects(m_projectController->recentProjects());
                m_gitStatusLabel->setText(QStringLiteral("Git状态：Git 仓库正常"));
                m_gitStatusLabel->setProperty("state", QStringLiteral("ok"));
                restyle(m_gitStatusLabel);
                // v4-P6-15: wire the new history + relation widgets.
                if (m_commitHistory) {
                    m_commitHistory->setRepositoryPath(path);
                    m_commitHistory->refresh();
                }
                showWorkspace();
                completeRepositoryTransition(true);
                onRefreshRemotes();
                if (feedbackButton) {
                    ActionButtonFeedback::setReady(feedbackButton, true);
                    ActionButtonFeedback::showSuccess(feedbackButton, "打开成功", 800);
                }
                hideDropOverlay();
                finishOperation();  // v4-P6-18 P0-2
                if (onDone) onDone(true);
            });
            return;
        }

        const QString details = info.error.trimmed().isEmpty()
            ? "所选目录不是有效的 Git 仓库：" + QDir::toNativeSeparators(path)
            : info.error;
        if (requireGitRepository) {
            // A failed switch is still a session switch attempt. Clear the
            // previous repository before showing the diagnostic so old
            // callbacks cannot continue acting on Repo A.
            clearRepositorySession();
            completeRepositoryTransition(false);
            m_errorDisplay->setText("打开项目失败\n\n" + details);
            m_errorDisplay->show();
            ActionButtonFeedback::setReady(feedbackButton, true);
            ActionButtonFeedback::showError(feedbackButton, "打开失败", 1300);
            hideDropOverlay();
            finishOperation();  // v4-P6-18 P0-2
            if (onDone) onDone(false);
            return;
        }

        // v4-P6-21 ISSUE-03: non-git directory - every repo-bound
        // service must be cleared so no operation can reach the
        // previous repository A.
        clearRepositorySession();
        m_projectLabel->setText("当前项目：" + QFileInfo(path).fileName());
        m_gitStatusLabel->setText("Git状态：⚠ 未初始化");
        m_gitStatusLabel->setProperty("state", QStringLiteral("danger"));
        restyle(m_gitStatusLabel);
        m_branchLabel->setText("当前分支：(无)");
        m_fileCountLabel->setText("文件变化：0 个文件需要处理");
        m_suggestionLabel->setText("建议：初始化 Git 仓库");
        m_changesTree->clear();
        m_errorDisplay->setText("此项目不是 Git 仓库。\n\nGit 可以帮助你：\n- 保存版本\n- 创建分支\n- 管理代码变更");
        m_errorDisplay->show();
        showWorkspace();
        completeRepositoryTransition(false);
        hideDropOverlay();
        finishOperation();  // v4-P6-18 P0-2
        if (onDone) onDone(true);
    });
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *event) {
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty()) {
        event->ignore();
        return;
    }
    const QString path = urls.first().toLocalFile();
    if (path.isEmpty() || !QFileInfo(path).isDir()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    if (!m_dropSession->begin(path)) {
        // A previous drop session is still active: rejected, no overlay
        // change, nothing leaks.
        return;
    }
    showDropOverlay(path);
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if (m_dropOverlay) {
        m_dropOverlay->setGeometry(rect());
    }
}

void MainWindow::showDropOverlay(const QString &path) {
    if (!m_dropOverlay) {
        return;
    }
    m_dropOverlay->setText("正在打开仓库：\n" + QFileInfo(path).fileName());
    m_dropOverlay->setGeometry(rect());
    m_dropOverlay->raise();
    m_dropOverlay->show();
}

void MainWindow::hideDropOverlay() {
    if (m_dropOverlay) {
        m_dropOverlay->hide();
    }
}

void MainWindow::dropSessionResolved(const QString &path, bool isRepo, const QString &branch) {
    Q_UNUSED(branch);
    if (!isRepo) {
        hideDropOverlay();
        m_dropSession->finish();
        m_errorDisplay->setText("打开项目失败\n\n所选目录不是有效的 Git 仓库。");
        m_errorDisplay->show();
        return;
    }
    // Session stays in Opening until loadProject reports back.
    loadProject(path, nullptr, true, [this](bool) {
        m_dropSession->finish();
    });
}

void MainWindow::applyRepositorySession(const QString &path) {
    // v4-P6-21 ISSUE-03: one atomic switch - controller and every
    // repo-bound service move to the SAME repository; generation++
    // invalidates stale async callbacks from the previous session.
    ++m_repositoryGeneration;
    ++m_diffRequestGeneration;
    m_repositorySessionReady = false;
    if (m_workspaceWidget) {
        m_workspaceWidget->setEnabled(false);
    }
    invalidateOperationFeedback();  // v4-P6-21R4 P1-02 stale feedback
    m_currentProjectPath = path;
    m_remoteAvailable = false;
    m_remoteOperationBusy = false;
    m_commitOperationBusy = false;
    m_controller->setRepositoryPath(path);
    m_conflictService->setWorkingDirectory(path);
    m_conflictResolutionService->setWorkingDirectory(path);
    m_conflictSnapshot = {};
    m_conflictSnapshotGeneration = 0;
    closeConflictResolver();
    if (m_conflictBanner) {
        m_conflictBanner->hide();
    }
    m_tagService->setWorkingDirectory(path);
    m_stashService->setWorkingDirectory(path);
    m_cherryPickService->setWorkingDirectory(path);
    m_rebaseService->setWorkingDirectory(path);
    m_bisectService->setWorkingDirectory(path);
    updateRemoteButtonsEnabled();
    updateCommitActionState();
}

void MainWindow::clearRepositorySession() {
    // v4-P6-21 ISSUE-03: entering NoRepository - no service may keep
    // pointing at the previous repository.
    ++m_repositoryGeneration;
    ++m_diffRequestGeneration;
    m_repositorySessionReady = false;
    if (m_workspaceWidget) {
        m_workspaceWidget->setEnabled(false);
    }
    invalidateOperationFeedback();  // v4-P6-21R4 P1-02 stale feedback
    m_currentProjectPath.clear();
    m_remoteAvailable = false;
    m_remoteOperationBusy = false;
    m_commitOperationBusy = false;
    m_controller->setRepositoryPath(QString());
    m_conflictService->setWorkingDirectory(QString());
    m_conflictResolutionService->setWorkingDirectory(QString());
    m_conflictSnapshot = {};
    m_conflictSnapshotGeneration = 0;
    closeConflictResolver();
    if (m_conflictBanner) {
        m_conflictBanner->hide();
    }
    m_tagService->setWorkingDirectory(QString());
    m_stashService->setWorkingDirectory(QString());
    m_cherryPickService->setWorkingDirectory(QString());
    m_rebaseService->setWorkingDirectory(QString());
    m_bisectService->setWorkingDirectory(QString());
    updateRemoteButtonsEnabled();
    updateCommitActionState();
}

void MainWindow::completeRepositoryTransition(bool repositoryAvailable) {
    m_repositorySessionReady = repositoryAvailable;
    if (m_workspaceWidget) {
        m_workspaceWidget->setEnabled(true);
    }
    for (QWidget *action : {static_cast<QWidget *>(m_graphButton),
                            static_cast<QWidget *>(m_tagButton),
                            static_cast<QWidget *>(m_stashButton),
                            static_cast<QWidget *>(m_bisectButton),
                            static_cast<QWidget *>(m_releaseButton),
                            static_cast<QWidget *>(m_workflowButton)}) {
        if (action) {
            action->setEnabled(repositoryAvailable);
        }
    }
    updateRemoteButtonsEnabled();
    updateStageActionState();
    updateBranchActionState();
}

void MainWindow::invalidateOperationFeedback() {
    // v4-P6-21R4 P1-02: reset every operation button so feedback
    // timers from the previous repository session are invalidated
    // (feedbackToken bumps -> late timers no-op) and the new session
    // starts from the derived state reducers.
    for (QPushButton *b : {m_refreshButton, m_fetchButton, m_pullButton,
                           m_pushButton, m_commitButton, m_stageAllButton,
                           m_unstageAllButton, m_stageButton,
                           // v4-P6-21R5 P1-02: branch actions must reset
                           // too - a late create/delete/merge/switch
                           // completion (or its timer) from repo A must
                           // never touch repo B's buttons.
                           m_switchBranchButton, m_createBranchButton,
                           m_deleteBranchButton, m_mergeButton}) {
        ActionButtonFeedback::reset(b);
    }
    m_notifications->clearAllBusy();  // v4-P6-21R6 P1-04
}

void MainWindow::updateRemoteButtonsEnabled() {
    // v4-P6-21R2: single derived state - a button is enabled only when
    // a repository is loaded, remotes exist AND no remote operation is
    // running. No other callback may set these buttons directly.
    const bool repoAvailable =
        m_repositorySessionReady && !m_currentProjectPath.isEmpty();
    const bool remoteReady =
        repoAvailable && m_remoteAvailable && !m_remoteOperationBusy;
    if (m_fetchButton) {
        ActionButtonFeedback::setReady(m_fetchButton, remoteReady);
    }
    if (m_pullButton) {
        ActionButtonFeedback::setReady(m_pullButton, remoteReady);
    }
    if (m_pushButton) {
        ActionButtonFeedback::setReady(m_pushButton, remoteReady);
    }
}

void MainWindow::onRefreshRemotes() {
    if (!m_repositorySessionReady) {
        return;
    }
    // v4-P6-21R2: session fence - remote results from a previous repo
    // must never update the current repo's UI.
    const quint64 generation = m_repositoryGeneration;
    const QString repoPath = m_currentProjectPath;
    m_controller->remotesAsync(
        [this, generation, repoPath](GitController::RemoteResult result) {
            if (!isRepositorySessionCurrent(generation, repoPath)) {
                qWarning("onRefreshRemotes fence dropped: gen=%llu/%llu path=%s vs %s",
                         static_cast<unsigned long long>(generation),
                         static_cast<unsigned long long>(m_repositoryGeneration),
                         qPrintable(repoPath), qPrintable(m_currentProjectPath));
                return;
            }
        // v4-P6-21 ISSUE-06: this callback only decides the remote
        // availability part; it must never re-enable buttons while a
        // fetch/pull/push is running.
        if (!result.success) {
            m_remoteAvailable = false;
            m_remotesLabel->setText(
                QStringLiteral("远程仓库读取失败：%1").arg(result.error));
            updateRemoteButtonsEnabled();
            return;
        }
        if (result.remotes.isEmpty()) {
            // v4-P6-1: empty state with reason + next step.
            // v4-P6-21R2: no remotes -> fetch/pull/push disabled.
            m_remoteAvailable = false;
            m_remotesLabel->setText(
                NotificationManager::emptyStateText(
                    QStringLiteral("未配置远程仓库"),
                    QStringLiteral("使用“克隆仓库”或 git remote add 添加远程。")));
            updateRemoteButtonsEnabled();
            return;
        }
        m_remoteAvailable = true;
        QStringList lines;
        for (const GitRemote &r : result.remotes) {
            lines << QStringLiteral("%1  fetch: %2  push: %3")
                         .arg(r.name, r.fetchUrl, r.pushUrl);
        }
        m_remotesLabel->setText(lines.join('\n'));
        updateRemoteButtonsEnabled();
    });
}

void MainWindow::onFetch() {
    if (!m_repositorySessionReady) {
        return;
    }
    // v4-P6-1: uniform busy state (no double clicks while running).
    // v4-P6-21 ISSUE-06: hard gate - a busy remote operation must not
    // start another command, not even with UI feedback.
    if (m_remoteOperationBusy) {
        return;
    }
    m_remoteOperationBusy = true;
    updateRemoteButtonsEnabled();
    m_notifications->beginBusy(m_fetchButton, QStringLiteral("正在Fetch…"));
    const quint64 repositoryGeneration = m_repositoryGeneration;
    const QString repositoryPath = m_currentProjectPath;
    m_controller->fetchAsync([this, repositoryGeneration, repositoryPath](
                                  GitRemoteService::OperationResult result) {
        if (!isRepositorySessionCurrent(repositoryGeneration, repositoryPath)) {
            return;
        }
        m_remoteOperationBusy = false;
        m_notifications->endBusy(m_fetchButton);
        updateRemoteButtonsEnabled();
        if (!result.success) {
            // v4-P1-2.1 error classification, v4-P6-1 user language.
            QString reason;
            switch (result.errorKind) {
            case GitRemoteService::ErrorKind::NoRemote:
                reason = "未配置远程仓库，无法 Fetch。";
                break;
            case GitRemoteService::ErrorKind::Network:
                reason = "网络连接失败，无法访问远程仓库。";
                break;
            case GitRemoteService::ErrorKind::Permission:
                reason = "权限被拒绝，请检查远程仓库的访问权限。";
                break;
            default:
                reason = result.error;
                break;
            }
            ActionButtonFeedback::showError(m_fetchButton, "Fetch失败");
            const UserFacingError ufe =
                UserFacingError::fromGit(reason, QStringLiteral("Fetch"));
            m_errorDisplay->setText(ufe.toString());
            m_errorDisplay->show();
            return;
        }
        ActionButtonFeedback::showSuccess(m_fetchButton, "Fetch成功");
        m_errorDisplay->clear();
        m_errorDisplay->hide();
        // v4-P1-2.1: fetch success refreshes branches + remote state.
        m_controller->refreshAsync({});
        onRefreshRemotes();
        // v4-P1-2 teaching hint: fetch never touches the working tree.
        m_suggestionLabel->setText(
            "教学：Fetch 只是把远程分支引用下载到本地，不会修改你的工作区文件。");
    });
}

void MainWindow::onPull() {
    if (!m_repositorySessionReady) {
        return;
    }
    // v4-P6-21 ISSUE-06: hard gate + derived button state.
    if (m_remoteOperationBusy) {
        return;
    }
    m_remoteOperationBusy = true;
    updateRemoteButtonsEnabled();
    m_notifications->beginBusy(m_pullButton, QStringLiteral("正在Pull…"));
    const quint64 repositoryGeneration = m_repositoryGeneration;
    const QString repositoryPath = m_currentProjectPath;
    m_controller->pullAsync([this, repositoryGeneration, repositoryPath](
                                 GitRemoteService::OperationResult result) {
        if (!isRepositorySessionCurrent(repositoryGeneration, repositoryPath)) {
            return;
        }
        m_remoteOperationBusy = false;
        m_notifications->endBusy(m_pullButton);
        updateRemoteButtonsEnabled();
        if (result.conflict) {
            // Never auto-resolve conflicts - surface them verbatim.
            ActionButtonFeedback::showError(m_pullButton, "Pull冲突");
            const UserFacingError ufe =
                UserFacingError::fromGit(result.error, QStringLiteral("Pull"));
            m_errorDisplay->setText(ufe.toString());
            m_errorDisplay->show();
            return;
        }
        if (!result.success) {
            ActionButtonFeedback::showError(m_pullButton, "Pull失败");
            const UserFacingError ufe =
                UserFacingError::fromGit(result.error, QStringLiteral("Pull"));
            m_errorDisplay->setText(ufe.toString());
            m_errorDisplay->show();
            return;
        }
        ActionButtonFeedback::showSuccess(
            m_pullButton,
            result.alreadyUpToDate ? "已是最新" : "Pull成功");
        m_errorDisplay->clear();
        m_errorDisplay->hide();
        m_controller->refreshAsync({});
    });
}

void MainWindow::onPush() {
    if (!m_repositorySessionReady) {
        return;
    }
    // v4-P6-21 ISSUE-06: hard gate + derived button state.
    if (m_remoteOperationBusy) {
        return;
    }
    m_remoteOperationBusy = true;
    updateRemoteButtonsEnabled();
    m_notifications->beginBusy(m_pushButton, QStringLiteral("正在Push…"));
    const quint64 repositoryGeneration = m_repositoryGeneration;
    const QString repositoryPath = m_currentProjectPath;
    m_controller->pushAsync([this, repositoryGeneration, repositoryPath](
                                 GitRemoteService::OperationResult result) {
        if (!isRepositorySessionCurrent(repositoryGeneration, repositoryPath)) {
            return;
        }
        m_remoteOperationBusy = false;
        m_notifications->endBusy(m_pushButton);
        updateRemoteButtonsEnabled();
        if (!result.success) {
            ActionButtonFeedback::showError(m_pushButton, "Push失败");
            const UserFacingError ufe = UserFacingError::fromGit(
                result.hasRemote ? result.error
                                 : QStringLiteral("未配置远程仓库，无法 Push。"),
                QStringLiteral("Push"));
            m_errorDisplay->setText(ufe.toString());
            m_errorDisplay->show();
            return;
        }
        ActionButtonFeedback::showSuccess(
            m_pushButton,
            result.alreadyUpToDate ? "已是最新" : "Push成功");
        m_errorDisplay->clear();
        m_errorDisplay->hide();
    });
}

void MainWindow::onFileSelectionChanged() {
    if (!m_repositorySessionReady) {
        return;
    }
    if (!m_diffViewer) {
        return;
    }
    QTreeWidgetItem *item = m_changesTree->currentItem();
    if (!item) {
        m_diffViewer->showMessage("请选择一个文件查看差异");
        return;
    }
    const QString path = item->data(0, Qt::UserRole).toString();
    if (path.isEmpty()) {
        m_diffViewer->showMessage("请选择一个文件查看差异");
        return;
    }
    const quint64 repositoryGeneration = m_repositoryGeneration;
    const QString repositoryPath = m_currentProjectPath;
    const quint64 requestGeneration = ++m_diffRequestGeneration;
    m_controller->fileDiffAsync(path, [this, path, repositoryGeneration,
                                       repositoryPath, requestGeneration](
                                      GitController::FileDiffResult result) {
        if (!isRepositorySessionCurrent(repositoryGeneration, repositoryPath)
            || requestGeneration != m_diffRequestGeneration) {
            return;
        }
        QTreeWidgetItem *currentItem = m_changesTree->currentItem();
        if (!currentItem
            || currentItem->data(0, Qt::UserRole).toString() != path) {
            return;
        }
        if (!result.success) {
            m_diffViewer->showMessage("差异加载失败：\n" + result.error);
            return;
        }
        if (!result.binary) {
            m_diffViewer->showTextDiff(
                result.text.isEmpty() ? "（无文本差异）" : result.text);
            return;
        }
        // Git says binary: explain WHY using the working-tree file bytes
        // (read-only; the file itself is never modified).
        EncodingDiagnostic diag;
        const QString repoRoot = m_controller->workingDirectory();
        const QString full =
            QDir::cleanPath((repoRoot.isEmpty() ? m_currentProjectPath
                                                : repoRoot)
                            + "/" + path);
        QFile file(full);
        if (file.open(QIODevice::ReadOnly)) {
            diag = diagnoseEncoding(file.read(8000));
        }
        m_diffViewer->showBinaryDiagnosis(path, diag);
    });
}

void MainWindow::onStageSelected() {
    if (!m_repositorySessionReady) {
        return;
    }
    QTreeWidgetItem *item = m_changesTree->currentItem();
    if (!item) {
        return;
    }
    const QString path = item->data(0, Qt::UserRole).toString();
    if (path.isEmpty()) {
        return;
    }
    // v4-P0-R1.3 stability: same write-operation mutex as Stage All.
    const auto busyNow = [](QPushButton *b) {
        return b && b->text().contains("正在");
    };
    if (busyNow(m_stageButton) || busyNow(m_stageAllButton)
        || busyNow(m_unstageAllButton) || busyNow(m_commitButton)) {
        return;
    }
    ActionButtonFeedback::showBusy(m_stageButton, "正在缓存…");
    const quint64 sessionGeneration = m_repositoryGeneration;
    const QString sessionPath = m_currentProjectPath;
    m_controller->stageFilesAsync(
        {path}, [this, sessionGeneration, sessionPath](
                    GitController::OperationResult result) {
        if (!isRepositorySessionCurrent(sessionGeneration, sessionPath)) {
            return;
        }
        if (!result.success) {
            const bool lockConflict =
                result.error.contains("index.lock", Qt::CaseInsensitive);
            ActionButtonFeedback::showError(
                m_stageButton,
                lockConflict ? "另一个 Git 操作正在进行，请稍后重试"
                             : "缓存失败");
            m_errorDisplay->setText(
                (lockConflict ? "缓存失败：另一个 Git 操作正在进行\n\n"
                              : "缓存失败\n\n")
                + result.error);
            m_errorDisplay->show();
            return;
        }
        ActionButtonFeedback::showSuccess(m_stageButton, "缓存成功");
        m_errorDisplay->clear();
        m_errorDisplay->hide();
    });
}

void MainWindow::onCreateBranch() {
    if (!m_repositorySessionReady) {
        return;
    }
    const quint64 sessionGeneration = m_repositoryGeneration;
    const QString sessionPath = m_currentProjectPath;
    CreateBranchDialog dialog(m_controller, this, selectedBranch());
    connect(&dialog, &CreateBranchDialog::creationStarted, this,
            [this, sessionGeneration, sessionPath](const QString &branchName) {
        if (!isRepositorySessionCurrent(sessionGeneration, sessionPath)) {
            return;
        }
        m_pendingBranchSelection = branchName;
        ActionButtonFeedback::showBusy(m_createBranchButton, "正在创建…");
    });
    connect(
        &dialog,
        &CreateBranchDialog::branchCreated,
        this,
        [this, sessionGeneration, sessionPath](const QString &, bool) {
            if (!isRepositorySessionCurrent(sessionGeneration, sessionPath)) {
                return;
            }
            m_pendingBranchSelection.clear();
            ActionButtonFeedback::showSuccess(m_createBranchButton, "创建分支成功");
            m_errorDisplay->clear();
            m_errorDisplay->hide();
            // v4-P1-1 teaching hint.
            m_suggestionLabel->setText(
                "教学：分支只是提交历史的一条引用。创建分支不会复制文件。");
        });
    connect(
        &dialog,
        &CreateBranchDialog::branchCreationFailed,
        this,
        [this, sessionGeneration, sessionPath](const QString &error) {
            if (!isRepositorySessionCurrent(sessionGeneration, sessionPath)) {
                return;
            }
            m_pendingBranchSelection.clear();
            ActionButtonFeedback::showError(m_createBranchButton, "创建失败");
            m_errorDisplay->setText("创建分支失败\n\n" + error);
            m_errorDisplay->show();
        });
    dialog.exec();
}

QString MainWindow::selectedBranch() const {
    return m_branchTree->selectedBranch();
}

// v4-P6-15: the branch relation graph reuses the list-based operations
// by syncing the list selection first.
bool MainWindow::selectBranchInTree(const QString &branch) {
    if (!m_branchTree) {
        return false;
    }
    if (QTreeWidgetItem *item = m_branchTree->branchItem(branch)) {
        m_branchTree->setCurrentItem(item);
        m_branchTree->scrollToItem(item);
        return true;
    }
    return false;
}

void MainWindow::onSwitchBranch() {
    requestSwitchSelectedBranch();
}

void MainWindow::onDeleteBranch() {
    requestDeleteSelectedBranch();
}

void MainWindow::requestSwitchSelectedBranch() {
    if (!m_repositorySessionReady) {
        return;
    }
    const QString branch = selectedBranch();
    if (branch.isEmpty() || branch == m_controller->viewModel()->currentBranch()) {
        updateBranchActionState();
        return;
    }

    ActionButtonFeedback::showBusy(m_switchBranchButton, "正在切换…");
    const quint64 sessionGeneration = m_repositoryGeneration;
    const QString sessionPath = m_currentProjectPath;
    m_controller->workspaceStatusAsync(
        [this, branch, sessionGeneration, sessionPath](
            GitController::WorkspaceStatus status) {
        if (!isRepositorySessionCurrent(sessionGeneration, sessionPath)) {
            return;
        }
        const QString risk = workspaceRiskText(status);
        if (!risk.isEmpty()) {
            m_errorDisplay->setText(risk);
            m_errorDisplay->show();
            m_suggestionLabel->setText(risk);
        }

        m_controller->checkoutBranchAsync(
            branch,
            [this, branch, sessionGeneration, sessionPath](
                GitController::OperationResult result) {
            if (!isRepositorySessionCurrent(sessionGeneration, sessionPath)) {
                return;
            }
            if (!result.success) {
                ActionButtonFeedback::showError(m_switchBranchButton, "切换失败");
                if (BranchSwitchErrorDialog::isOverwriteProtectionError(result.error)) {
                    m_errorDisplay->setText(
                        BranchSwitchErrorDialog::diagnosticText(branch, result.error));
                    auto *dialog = new BranchSwitchErrorDialog(branch, result.error, this);
                    dialog->open();
                } else {
                    m_errorDisplay->setText("切换分支失败\n\n" + result.error);
                }
                m_errorDisplay->show();
                return;
            }
            ActionButtonFeedback::showSuccess(m_switchBranchButton, "切换成功");
            m_errorDisplay->clear();
            m_errorDisplay->hide();
            // v4-P1-1 teaching hint.
            m_suggestionLabel->setText(
                "已切换到分支：" + branch
                + "（教学：切换分支只是移动 HEAD 引用，不修改工作区文件。）");
            updateBranchActionState();
        });
    });
}

void MainWindow::onEnvironmentRequested() {
    const QString previousGitPath = m_environmentService->gitPath();
    GitEnvironmentDialog dialog(m_environmentService, this);
    // v4-P6-21R2: only a CONFIRMED selection changes the app-wide git
    // executable; Cancel must not clear the configured path.
    dialog.exec();
    // The dialog owns the single accepted/rejected + validated selection
    // gate, which also makes the exact UI decision independently testable.
    dialog.commitSelection(previousGitPath);
}

void MainWindow::onGitHubAccountRequested() {
    GitHubAccountDialog dialog(m_githubAccountService, m_projectController, this);
    dialog.exec();
}

void MainWindow::onCommitGraphRequested() {
    if (!m_repositorySessionReady) {
        return;
    }
    // v4-P6-15: the entry opens the commit history tab (the graph is
    // no longer user-visible; its source stays for rollback).
    if (m_commitHistory) {
        m_commitHistory->refresh();
        m_changesTab->setCurrentWidget(m_commitHistory);
    }
}

void MainWindow::performCherryPick(const QString &hash) {
    // v4-P4-1: confirm -> git cherry-pick -> success / conflict / error.
    if (!m_cherryPickService) return;
    const QMessageBox::StandardButton choice = QMessageBox::question(
        this, QStringLiteral("确认 Cherry-pick"),
        QStringLiteral("将提交 %1 应用（cherry-pick）到当前分支，继续吗？")
            .arg(hash.left(7)));
    if (choice != QMessageBox::Yes) return;

    const quint64 sessionGeneration = m_repositoryGeneration;
    const QString sessionPath = m_currentProjectPath;
    m_cherryPickService->cherryPickAsync(
        hash, [this, hash, sessionGeneration, sessionPath](const CherryPickResult &result) {
            if (!isRepositorySessionCurrent(sessionGeneration, sessionPath)) {
                return;
            }
            if (result.success) {
                QMessageBox::information(
                    this, QStringLiteral("Cherry-pick 成功"),
                    QStringLiteral("提交 %1 已应用到当前分支。")
                        .arg(hash.left(7)));
                // v4-P4-1: refresh through the existing pipeline.
                m_controller->refreshAsync();
                return;
            }
            if (result.conflict) {
                // Surface the conflicted files and offer Abort.
                QMessageBox box(this);
                box.setIcon(QMessageBox::Warning);
                box.setWindowTitle(QStringLiteral("Cherry-pick 冲突"));
                QString text = QStringLiteral(
                    "提交 %1 与当前分支产生冲突。\n请手动解决冲突后提交，"
                    "或放弃本次 Cherry-pick。")
                                   .arg(hash.left(7));
                if (!result.conflictedFiles.isEmpty()) {
                    text += QStringLiteral("\n\n冲突文件：\n%1")
                                .arg(result.conflictedFiles.join('\n'));
                } else {
                    text += QStringLiteral("\n\n（未检测到冲突文件列表）");
                }
                box.setText(text);
                QPushButton *abortButton = box.addButton(
                    QStringLiteral("放弃 (Abort)"), QMessageBox::DestructiveRole);
                box.addButton(QMessageBox::Close);
                box.exec();
                if (box.clickedButton() == abortButton) {
                    m_cherryPickService->abortAsync(
                        [this, sessionGeneration, sessionPath](bool ok, const QString &error) {
                            if (!isRepositorySessionCurrent(sessionGeneration, sessionPath)) {
                                return;
                            }
                            if (ok) {
                                m_controller->refreshAsync();
                            } else {
                                QMessageBox::warning(
                                    this, QStringLiteral("Abort 失败"),
                                    QStringLiteral("无法放弃 Cherry-pick：%1")
                                        .arg(error));
                            }
                        });
                }
                return;
            }
            QMessageBox::warning(
                this, QStringLiteral("Cherry-pick 失败"),
                QStringLiteral("无法应用提交 %1：\n%2")
                    .arg(hash.left(7),
                         result.message.isEmpty()
                             ? QStringLiteral("未知错误")
                             : result.message));
        });
}

void MainWindow::onTagManagerRequested() {
    if (!m_repositorySessionReady) {
        return;
    }
    // v4-P3-4: tag management dialog (list / create / delete).
    if (m_currentProjectPath.isEmpty() || !m_tagService) {
        return;
    }
    TagManagerDialog dialog(m_tagService, this);
    dialog.exec();
}

void MainWindow::onStashRequested() {
    if (!m_repositorySessionReady) {
        return;
    }
    // v4-P3-5: stash management dialog (list / create / apply / pop / drop).
    if (m_currentProjectPath.isEmpty() || !m_stashService) {
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Stash 管理"));
    dialog.setMinimumSize(620, 420);
    auto *layout = new QVBoxLayout(&dialog);
    auto *stashWidget = new StashWidget(m_stashService, m_controller, &dialog);
    layout->addWidget(stashWidget);
    dialog.exec();
}

void MainWindow::onBisectRequested() {
    if (!m_repositorySessionReady) {
        return;
    }
    // v4-P4-3: bisect management dialog (start / good / bad / reset).
    if (m_currentProjectPath.isEmpty() || !m_bisectService) {
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("二分查找 (Bisect)"));
    dialog.setMinimumSize(560, 400);
    auto *layout = new QVBoxLayout(&dialog);
    auto *bisectWidget = new BisectWidget(m_bisectService, m_controller,
                                          &dialog);
    layout->addWidget(bisectWidget);
    dialog.exec();
}

void MainWindow::onReleaseRequested() {
    if (!m_repositorySessionReady) {
        return;
    }
    // v4-P4-4: GitHub release management dialog.
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("GitHub Release 管理"));
    dialog.setMinimumSize(640, 520);
    auto *layout = new QVBoxLayout(&dialog);
    auto *releaseWidget = new ReleaseWidget(m_githubAccountService,
                                            m_tagService, &dialog);
    layout->addWidget(releaseWidget);
    dialog.exec();
}

void MainWindow::performRebase(const QString &targetBranch) {
    // v4-P4-2: explicit confirmation naming current branch and target.
    if (!m_rebaseService) return;
    const QString current = m_controller->viewModel()->currentBranch();
    if (current.isEmpty()) return;

    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QStringLiteral("确认 Rebase"));
    box.setText(QStringLiteral("Rebase 当前分支？\n\n"
                               "当前分支：%1\n"
                               "目标基线：%2\n\n"
                               "将执行：git rebase %2\n\n"
                               "结果说明：%1 的独有提交会被重新应用到 "
                               "%2 最新提交之后。")
                     .arg(current, targetBranch));
    QPushButton *confirmButton = box.addButton(
        QStringLiteral("确认 Rebase"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != confirmButton) return;

    const quint64 sessionGeneration = m_repositoryGeneration;
    const QString sessionPath = m_currentProjectPath;
    m_rebaseService->rebaseAsync(
        targetBranch, [this, sessionGeneration, sessionPath](const RebaseResult &result) {
            if (!isRepositorySessionCurrent(sessionGeneration, sessionPath)) {
                return;
            }
            switch (result.outcome) {
            case RebaseOutcome::Success:
                QMessageBox::information(
                    this, QStringLiteral("Rebase 成功"),
                    QStringLiteral(
                        "当前分支已成功 Rebase 到目标基线。\n\n"
                        "教学说明：Rebase 会重写当前分支独有提交的父子关系。"
                        "原提交会生成新的提交 Hash，目标分支本身不会被修改。"));
                m_controller->refreshAsync();
                break;
            case RebaseOutcome::UpToDate:
                QMessageBox::information(
                    this, QStringLiteral("无需更新"),
                    QStringLiteral("当前分支已基于目标分支最新提交，"
                                   "无需 Rebase。"));
                m_controller->refreshAsync();
                break;
            case RebaseOutcome::Conflict:
                showRebaseConflict(result);
                break;
            case RebaseOutcome::InProgress:
                QMessageBox::warning(
                    this, QStringLiteral("Rebase 正在进行"),
                    QStringLiteral("已有 Rebase 正在进行，请先继续或放弃。"));
                break;
            case RebaseOutcome::Rejected:
                QMessageBox::warning(this, QStringLiteral("无法执行 Rebase"),
                                     result.message);
                break;
            case RebaseOutcome::Failed:
                QMessageBox::warning(
                    this, QStringLiteral("Rebase 失败"),
                    result.message.isEmpty()
                        ? QStringLiteral("未知错误")
                        : result.message);
                break;
            }
        });
}

void MainWindow::showRebaseConflict(const RebaseResult &result) {
    // v4-P4-2: paused rebase - list conflicted files, offer Continue/Abort.
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("Rebase 冲突"));
    QString text = QStringLiteral(
        "Rebase 已暂停。\n\n"
        "教学说明：Rebase 正在逐个重新应用提交。当前提交与目标基线"
        "修改了相同内容，Git 无法自动决定最终结果。\n\n"
        "处理步骤：\n"
        "1. 编辑冲突文件\n"
        "2. 暂存已解决文件\n"
        "3. 点击“继续 Rebase”\n\n"
        "或者点击“放弃 Rebase”恢复操作前状态。");
    if (!result.conflictedFiles.isEmpty()) {
        text += QStringLiteral("\n\n冲突文件：\n%1")
                    .arg(result.conflictedFiles.join('\n'));
    }
    box.setText(text);
    QPushButton *continueButton = box.addButton(
        QStringLiteral("继续 Rebase"), QMessageBox::AcceptRole);
    QPushButton *abortButton = box.addButton(
        QStringLiteral("放弃 Rebase"), QMessageBox::DestructiveRole);
    box.exec();
    if (box.clickedButton() == continueButton) {
        onRebaseContinue();
    } else if (box.clickedButton() == abortButton) {
        onRebaseAbort();
    }
}

void MainWindow::onRebaseContinue() {
    // v4-P4-2: git rebase --continue (non-interactive).
    if (!m_rebaseService) return;
    const quint64 sessionGeneration = m_repositoryGeneration;
    const QString sessionPath = m_currentProjectPath;
    m_rebaseService->continueAsync([this, sessionGeneration, sessionPath](const RebaseResult &result) {
        if (!isRepositorySessionCurrent(sessionGeneration, sessionPath)) {
            return;
        }
        switch (result.outcome) {
        case RebaseOutcome::Success:
            QMessageBox::information(
                this, QStringLiteral("Rebase 完成"),
                QStringLiteral("冲突已解决，Rebase 成功完成。"));
            m_controller->refreshAsync();
            break;
        case RebaseOutcome::Conflict:
            showRebaseConflict(result);
            break;
        case RebaseOutcome::UpToDate:
        case RebaseOutcome::InProgress:
        case RebaseOutcome::Rejected:
        case RebaseOutcome::Failed:
            QMessageBox::warning(
                this, QStringLiteral("继续 Rebase 失败"),
                result.message.isEmpty()
                    ? QStringLiteral("未知错误")
                    : result.message);
            break;
        }
    });
}

void MainWindow::onRebaseAbort() {
    // v4-P4-2: git rebase --abort.
    if (!m_rebaseService) return;
    const quint64 sessionGeneration = m_repositoryGeneration;
    const QString sessionPath = m_currentProjectPath;
    m_rebaseService->abortAsync([this, sessionGeneration, sessionPath](bool ok, const QString &error) {
        if (!isRepositorySessionCurrent(sessionGeneration, sessionPath)) {
            return;
        }
        if (ok) {
            QMessageBox::information(
                this, QStringLiteral("Rebase 已放弃"),
                QStringLiteral(
                    "Rebase 已取消。\n\n"
                    "教学说明：当前分支已恢复到 Rebase 开始前状态。"));
            m_controller->refreshAsync();
        } else {
            QMessageBox::warning(this, QStringLiteral("放弃失败"),
                                 QStringLiteral("无法放弃 Rebase：%1")
                                     .arg(error));
        }
    });
}

void MainWindow::showNotification(NotificationManager::Level level,
                                   const QString &title,
                                   const QString &message) {
    // v4-P6-1: THE single presentation point for notifications - no
    // other module pops its own style of message box.
    QMessageBox box(this);
    switch (level) {
    case NotificationManager::Level::Success:
        box.setIcon(QMessageBox::Information);
        break;
    case NotificationManager::Level::Info:
        box.setIcon(QMessageBox::Information);
        break;
    case NotificationManager::Level::Warning:
        box.setIcon(QMessageBox::Warning);
        break;
    case NotificationManager::Level::Error:
        box.setIcon(QMessageBox::Critical);
        break;
    }
    box.setWindowTitle(title);
    box.setText(message);
    box.exec();
}

void MainWindow::onRefreshClicked() {
    // v4-P6-4: the refresh button previously had no connection; wire it
    // to the existing refresh pipeline (no new refresh system).
    if (!m_repositorySessionReady || m_currentProjectPath.isEmpty()) {
        return;
    }
    // v4-P6-20 P1-2: re-entrancy guard - a click while a refresh (or
    // project load) is still running is ignored instead of racing it.
    if (ActionButtonFeedback::isActive(m_refreshButton)
        || m_controller->viewModel()->isLoading()) {
        return;
    }
    ++m_refreshInvocationCount;
    ActionButtonFeedback::showBusy(m_refreshButton, QStringLiteral("正在刷新…"));
    const quint64 generation = m_repositoryGeneration;
    const QString repoPath = m_currentProjectPath;
    m_controller->refreshAsync([this, generation, repoPath](bool success,
                                                              const QString &error) {
        if (!isRepositorySessionCurrent(generation, repoPath)) {
            return;
        }
        if (success) {
            ActionButtonFeedback::showSuccess(m_refreshButton,
                                              QStringLiteral("刷新成功"), 800);
            onRefreshRemotes();
        } else {
            ActionButtonFeedback::showError(m_refreshButton,
                                            QStringLiteral("刷新失败"));
            m_errorDisplay->setText(QStringLiteral("刷新失败\n\n%1").arg(error));
            m_errorDisplay->show();
        }
    });
}

void MainWindow::onLearnModeClicked() {
    // v4-P6-5: real learning mode - offline desktop widget with the
    // three topic groups (Git基础 / 日常工作流 / 高级操作).
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("学习模式"));
    dialog.setMinimumSize(820, 520);
    auto *layout = new QVBoxLayout(&dialog);

    // v4-P6-27: practices live next to the reading material. Picking one
    // closes the learning window and opens the generated sandbox in the
    // normal workspace, so the student operates the real UI rather than a
    // teaching-only imitation of it.
    auto *practiceRow = new QHBoxLayout();
    auto *practiceButton = new QPushButton(QStringLiteral("动手练习"), &dialog);
    practiceButton->setObjectName(QStringLiteral("openPracticeButton"));
    practiceButton->setProperty("role", QStringLiteral("primary"));
    practiceButton->setCursor(Qt::PointingHandCursor);
    practiceButton->setToolTip(
        QStringLiteral("在一次性仓库里练习提交、分支和冲突解决。"));
    practiceRow->addWidget(practiceButton);
    practiceRow->addStretch(1);
    layout->addLayout(practiceRow);

    auto *learning = new LearningWidget(&dialog);
    layout->addWidget(learning);

    connect(practiceButton, &QPushButton::clicked, this, [this, &dialog]() {
        PracticeDialog practice(this);
        connect(&practice, &PracticeDialog::repositoryRequested, this,
                [this, &dialog](const QString &repoPath) {
                    dialog.accept();  // step out of the way, show the repo
                    onProjectSelected(repoPath);
                });
        practice.exec();
    });

    dialog.exec();
}

void MainWindow::openPracticeDialog() {
    PracticeDialog practice(this);
    connect(&practice, &PracticeDialog::repositoryRequested, this,
            [this](const QString &repoPath) { onProjectSelected(repoPath); });
    practice.exec();
}

void MainWindow::promptGitMissing(const GitEnvironmentService::GitEnvironment &env) {
    // v4-P2-1: git not installed - offer download / set path / continue.
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("Git 未安装"));
    box.setText(QStringLiteral("未检测到 Git。\n\n"
                               "QtGitStudio 需要 Git 才能管理仓库。"));
    box.setInformativeText(
        QStringLiteral("错误：%1").arg(
            env.error.isEmpty() ? QStringLiteral("git 命令不可用") : env.error));
    QPushButton *downloadButton = box.addButton(
        QStringLiteral("下载 Git"), QMessageBox::ActionRole);
    QPushButton *pathButton = box.addButton(
        QStringLiteral("设置路径"), QMessageBox::ActionRole);
    box.addButton(QStringLiteral("继续体验"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == downloadButton) {
        QDesktopServices::openUrl(
            QUrl(QStringLiteral("https://git-scm.com/download/win")));
        promptGitMissing(env);  // re-prompt after returning from browser
    } else if (box.clickedButton() == pathButton) {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("选择 Git 可执行文件"), QString(),
            QStringLiteral("git.exe;;所有文件 (*)"));
        if (!path.isEmpty()) {
            const GitEnvironmentService::GitEnvironment re =
                m_environmentService->detectSync(path);
            if (re.available) {
                GitCommandExecutor::setDefaultGitExecutable(re.gitPath);
                m_errorDisplay->setText(
                    QStringLiteral("Git 环境已配置：%1（版本 %2）")
                        .arg(re.gitPath, re.version));
                m_errorDisplay->show();
                return;
            }
            m_errorDisplay->setText(
                QStringLiteral("所选文件不是可用的 Git：%1").arg(re.error));
            m_errorDisplay->show();
            promptGitMissing(re);
        }
    }
    // "继续体验" (RejectRole) -> fall through, no further prompting.
}

void MainWindow::onMergeSelected() {
    if (!m_repositorySessionReady) {
        return;
    }
    const QString branch = selectedBranch();
    if (branch.isEmpty()) {
        updateBranchActionState();
        return;
    }
    const QString current = m_controller->viewModel()->currentBranch();
    if (branch == current) {
        m_errorDisplay->setText("不能合并当前所在的分支，请选择其他分支。");
        m_errorDisplay->show();
        return;
    }
    if (QMessageBox::question(
            this,
            "确认合并",
            "确定要将分支 \"" + branch + "\" 合并到当前分支吗？",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    ActionButtonFeedback::showBusy(m_mergeButton, "正在合并…");
    m_notifications->beginBusy(m_mergeButton, QStringLiteral("正在合并…"));
    const quint64 sessionGeneration = m_repositoryGeneration;
    const QString sessionPath = m_currentProjectPath;
    m_controller->mergeAsync(
        branch,
        [this, branch, sessionGeneration, sessionPath](
            GitMergeService::MergeResult result) {
        if (!isRepositorySessionCurrent(sessionGeneration, sessionPath)) {
            return;
        }
        m_notifications->endBusy(m_mergeButton);
        if (result.conflict) {
            // v4-P1-4: conflicts are NEVER auto-resolved.
            ActionButtonFeedback::showError(m_mergeButton, "合并冲突");
            m_errorDisplay->setText(
                "合并产生冲突，需要人工处理。\n\n请解决冲突文件（UU 状态）后提交，"
                "或点击\"放弃合并\"恢复。\n\n" + result.error);
            m_errorDisplay->show();
            m_abortMergeButton->setEnabled(true);
            m_controller->refreshAsync({});
            m_suggestionLabel->setText(
                "教学：冲突意味着双方修改了同一处内容，Git 无法替你决定保留哪一份。"
                "手动编辑冲突标记（<<<<<<< / ======= / >>>>>>>）后提交即可。");
            return;
        }
        if (!result.success) {
            ActionButtonFeedback::showError(m_mergeButton, "合并失败");
            m_errorDisplay->setText("合并失败\n\n" + result.error);
            m_errorDisplay->show();
            return;
        }
        m_abortMergeButton->setEnabled(false);
        ActionButtonFeedback::showSuccess(
            m_mergeButton, result.fastForward ? "快进合并成功" : "合并提交成功");
        m_errorDisplay->clear();
        m_errorDisplay->hide();
        m_controller->refreshAsync({});
        m_suggestionLabel->setText(
            result.fastForward
                ? "教学：Fast-forward 合并只是把当前分支的指针向前移动，"
                  "不需要产生新的提交。"
                : "教学：Merge 会把两个分支的历史汇合，生成一个合并提交；"
                  "Merge 不会丢失任何一方的提交。");
    });
}

void MainWindow::onAbortMerge() {
    if (!m_repositorySessionReady) {
        return;
    }
    const quint64 sessionGeneration = m_repositoryGeneration;
    const QString sessionPath = m_currentProjectPath;
    m_controller->abortMergeAsync(
        [this, sessionGeneration, sessionPath](bool ok, const QString &error) {
        if (!isRepositorySessionCurrent(sessionGeneration, sessionPath)) {
            return;
        }
        if (!ok) {
            m_errorDisplay->setText("放弃合并失败\n\n" + error);
            m_errorDisplay->show();
            return;
        }
        m_abortMergeButton->setEnabled(false);
        ActionButtonFeedback::reset(m_mergeButton);
        m_errorDisplay->setText("已放弃合并，工作区已恢复。");
        m_errorDisplay->show();
        m_controller->refreshAsync({});
        m_suggestionLabel->setText(
            "教学：git merge --abort 会放弃本次合并，把工作区恢复到合并前的状态。");
    });
}

void MainWindow::requestDeleteSelectedBranch() {
    if (!m_repositorySessionReady) {
        return;
    }
    const QString branch = selectedBranch();
    const QString current = m_controller->viewModel()->currentBranch();
    if (branch.isEmpty()) {
        updateBranchActionState();
        return;
    }
    if (branch == current) {
        const QString message = "当前分支不能删除，请先切换到其他分支";
        m_errorDisplay->setText(message);
        m_errorDisplay->show();
        updateBranchActionState();
        return;
    }

    if (QMessageBox::question(
            this,
            "确认删除",
            "确定要安全删除分支 \"" + branch + "\" 吗？",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    performDeleteBranch(branch, false);
}

void MainWindow::performDeleteBranch(const QString &branch, bool force) {
    ActionButtonFeedback::showBusy(m_deleteBranchButton, "正在删除…");
    const quint64 sessionGeneration = m_repositoryGeneration;
    const QString sessionPath = m_currentProjectPath;
    m_controller->deleteBranchAsync(
        branch, force,
        [this, branch, force, sessionGeneration, sessionPath](
            GitController::OperationResult result) {
        if (!isRepositorySessionCurrent(sessionGeneration, sessionPath)) {
            return;
        }
        if (result.success) {
            ActionButtonFeedback::showSuccess(m_deleteBranchButton, "删除成功");
            m_errorDisplay->clear();
            m_errorDisplay->hide();
            // v4-P1-1 teaching hint.
            m_suggestionLabel->setText(
                (force ? "已强制删除分支：" : "已删除分支：") + branch
                + "（教学：删除分支只是移除引用，提交对象仍在仓库中。）");
            updateBranchActionState();
            return;
        }

        ActionButtonFeedback::showError(m_deleteBranchButton, "删除失败");
        m_errorDisplay->setText((force ? "强制删除分支失败\n\n" : "删除分支失败\n\n") + result.error);
        m_errorDisplay->show();
        if (force || !result.rejectedAsUnmerged) {
            return;
        }

        QMessageBox forcePrompt(
            QMessageBox::Warning,
            "安全删除被拒绝",
            "该分支包含尚未合并的提交，安全删除已被 Git 拒绝。",
            QMessageBox::Cancel,
            this);
        QAbstractButton *forceButton = forcePrompt.addButton("强制删除", QMessageBox::DestructiveRole);
        forcePrompt.setDetailedText(result.error);
        forcePrompt.exec();
        if (forcePrompt.clickedButton() != forceButton) {
            return;
        }

        const QString warning =
            "强制删除分支 \"" + branch + "\"？\n\n"
            "该分支中的未合并提交可能丢失，此操作不可撤销。";
        if (QMessageBox::warning(
                this,
                "高风险操作",
                warning,
                QMessageBox::Yes | QMessageBox::Cancel,
                QMessageBox::Cancel) == QMessageBox::Yes) {
            performDeleteBranch(branch, true);
        }
    });
}

void MainWindow::onBranchSelected(QTreeWidgetItem *item, int column) {
    Q_UNUSED(column);
    m_branchTree->setCurrentItem(item);
    if (selectedBranch().isEmpty()) {
        updateBranchActionState();
        return;
    }
    requestSwitchSelectedBranch();
}

void MainWindow::onBranchContextMenu(const QPoint &pos) {
    QTreeWidgetItem *item = m_branchTree->itemAt(pos);
    if (!item) return;

    m_branchTree->setCurrentItem(item);
    updateBranchActionState();
    const QString branch = selectedBranch();

    if (branch.isEmpty()) {
        QMenu menu(this);
        QAction *unavailable = menu.addAction("目录节点不能执行分支操作");
        unavailable->setEnabled(false);
        menu.exec(m_branchTree->viewport()->mapToGlobal(pos));
        return;
    }

    QMenu menu(this);
    QAction *checkoutAction = menu.addAction("切换到 " + branch);
    QAction *deleteAction = menu.addAction("删除 " + branch);
    checkoutAction->setEnabled(m_switchBranchButton->isEnabled());
    deleteAction->setEnabled(m_deleteBranchButton->isEnabled());
    // v4-P4-2: rebase the current branch onto the selected branch.
    const QString current = m_controller->viewModel()->currentBranch();
    const bool rebasePossible =
        !branch.isEmpty() && !current.isEmpty() && branch != current;
    QAction *rebaseAction = nullptr;
    if (rebasePossible) {
        rebaseAction = menu.addAction("将当前分支 Rebase 到此分支");
    }

    QAction *selectedAction = menu.exec(m_branchTree->viewport()->mapToGlobal(pos));
    if (selectedAction == checkoutAction) {
        requestSwitchSelectedBranch();
    } else if (selectedAction == deleteAction) {
        requestDeleteSelectedBranch();
    } else if (selectedAction == rebaseAction) {
        performRebase(branch);
    }
}

void MainWindow::onBranchesChanged() {
    m_branchTree->setBranchData(
        m_controller->viewModel()->branches(),
        m_controller->viewModel()->currentBranch(),
        m_pendingBranchSelection);
    updateBranchActionState();
}

void MainWindow::onCurrentBranchChanged() {
    QString current = m_controller->viewModel()->currentBranch();
    m_branchLabel->setText("当前分支：" + current);
    m_branchLabel->setProperty("state", QStringLiteral("strong"));
    restyle(m_branchLabel);

    m_branchTree->setCurrentBranch(current, m_pendingBranchSelection);
    m_pendingBranchSelection.clear();

    updateBranchActionState();
}

void MainWindow::updateBranchActionState() {
    if (!m_repositorySessionReady) {
        ActionButtonFeedback::setReady(m_switchBranchButton, false);
        ActionButtonFeedback::setReady(m_createBranchButton, false);
        ActionButtonFeedback::setReady(m_deleteBranchButton, false);
        if (m_mergeButton) {
            m_mergeButton->setEnabled(false);
        }
        return;
    }
    // v4-P2-1.1: empty repositories (no first commit) must still allow
    // branch creation; switching/deleting stay disabled.
    const GitController::RepoState repoState = m_controller->repoState();
    const bool emptyRepo = repoState == GitController::RepoState::EmptyRepository;

    const QString branch = selectedBranch();
    const QString current = m_controller->viewModel()->currentBranch();
    const bool hasBranch = !branch.isEmpty();
    const bool canCreate =
        emptyRepo || !m_controller->viewModel()->branches().isEmpty();
    const bool isCurrent = hasBranch && branch == current;

    ActionButtonFeedback::setReady(m_switchBranchButton, hasBranch && !isCurrent);
    ActionButtonFeedback::setReady(m_createBranchButton, canCreate);
    ActionButtonFeedback::setReady(m_deleteBranchButton, hasBranch && !isCurrent);

    // v4-P6-21 ISSUE-12: with a clean working tree the branch-action
    // hints must NOT overwrite the "✓ 工作区干净" state - the tree's
    // default directory-node selection would otherwise hide it. The
    // tooltips above still follow the selection; only the suggestion
    // is re-asserted at the end.
    const bool cleanWorkspace =
        m_controller->viewModel()->snapshot().files.isEmpty()
        && !m_controller->viewModel()->currentBranch().isEmpty();

    if (emptyRepo) {
        m_switchBranchButton->setToolTip("空仓库没有可切换的分支");
        m_createBranchButton->setToolTip("为空仓库创建第一个分支");
        m_deleteBranchButton->setToolTip("空仓库没有可删除的分支");
        m_suggestionLabel->setText(
            "当前仓库还没有提交。下一步：创建文件 → Stage → Commit");
        return;
    }

    if (!m_branchTree->currentItem()) {
        m_switchBranchButton->setToolTip("请先选择一个本地分支");
        m_createBranchButton->setToolTip(canCreate ? "选择起点并创建新分支" : "当前没有可用的起点分支");
        m_deleteBranchButton->setToolTip("请先选择一个本地分支");
        m_suggestionLabel->setText(canCreate ? "可以创建新分支，或选择一个本地分支" : "当前没有可用的本地分支");
    } else if (!hasBranch) {
        m_switchBranchButton->setToolTip("目录节点不能切换");
        m_createBranchButton->setToolTip(canCreate ? "选择起点并创建新分支" : "当前没有可用的起点分支");
        m_deleteBranchButton->setToolTip("目录节点不能删除");
        if (!cleanWorkspace) {
            m_suggestionLabel->setText("目录节点不能切换或删除，但可以创建新分支");
        }
    } else if (isCurrent) {
        m_switchBranchButton->setToolTip("已经是当前分支");
        m_createBranchButton->setToolTip(QString());
        m_deleteBranchButton->setToolTip("当前分支不能删除，请先切换到其他分支");
        if (!cleanWorkspace) {
            m_suggestionLabel->setText("当前分支不能删除，请先切换到其他分支");
        }
    } else {
        m_switchBranchButton->setToolTip("切换到 " + branch);
        m_createBranchButton->setToolTip(QString());
        m_deleteBranchButton->setToolTip("安全删除 " + branch);
        m_suggestionLabel->setText("可切换或安全删除分支：" + branch);
    }

    // v4-P6-21 ISSUE-12: re-assert the clean hint last so a branch
    // selection (directory node etc.) can never hide it.
    if (cleanWorkspace) {
        m_suggestionLabel->setText("✓ 工作区干净");
        m_suggestionLabel->setProperty("state", QStringLiteral("ok"));
        restyle(m_suggestionLabel);
    }

}

void MainWindow::onStageAll() {
    if (!m_repositorySessionReady) {
        return;
    }
    // v4-P0-R1.3 stability fix: write-operation mutex. Concurrent git index
    // writes (Stage All while a previous Stage/Unstage/Commit refresh chain
    // is still running) collide on .git/index.lock and fail with a bogus
    // "暂存失败". Refuse while any write button shows a busy state ("正在…")
    // — not while it shows success/error feedback (which is harmless).
    const auto busyNow = [](QPushButton *b) {
        return b && b->text().contains("正在");
    };
    if (busyNow(m_stageAllButton) || busyNow(m_unstageAllButton)
        || busyNow(m_commitButton)) {
        return;
    }
    auto snap = m_controller->viewModel()->snapshot();
    QStringList files;
    for (const auto &file : snap.files) {
        files.append(file.path);
    }
    if (files.isEmpty()) {
        updateStageActionState();
        return;
    }

    ActionButtonFeedback::showBusy(m_stageAllButton, "正在暂存…");
    const quint64 sessionGeneration = m_repositoryGeneration;
    const QString sessionPath = m_currentProjectPath;
    m_controller->stageFilesAsync(
        files, [this, sessionGeneration, sessionPath](
                   GitController::OperationResult result) {
        if (!isRepositorySessionCurrent(sessionGeneration, sessionPath)) {
            return;
        }
        if (!result.success) {
            // v4-P0-R1.3: explain index.lock conflicts instead of a bare
            // failure (another git process is holding the index).
            const bool lockConflict =
                result.error.contains("index.lock", Qt::CaseInsensitive);
            ActionButtonFeedback::showError(
                m_stageAllButton,
                lockConflict ? "另一个 Git 操作正在进行，请稍后重试"
                             : "暂存失败");
            m_errorDisplay->setText(
                (lockConflict ? "暂存失败：另一个 Git 操作正在进行\n\n"
                              : "暂存失败\n\n")
                + result.error);
            m_errorDisplay->show();
            return;
        }
        ActionButtonFeedback::showSuccess(m_stageAllButton, "暂存成功");
        m_errorDisplay->clear();
        m_errorDisplay->hide();
    });
}

void MainWindow::onUnstageAll() {
    if (!m_repositorySessionReady) {
        return;
    }
    // v4-P0-R1.3 stability fix: same write-operation mutex as onStageAll.
    const auto busyNow = [](QPushButton *b) {
        return b && b->text().contains("正在");
    };
    if (busyNow(m_unstageAllButton) || busyNow(m_stageAllButton)
        || busyNow(m_commitButton)) {
        return;
    }
    if (!m_unstageAllButton->property("readyWhenIdle").toBool()) {
        updateStageActionState();
        return;
    }

    ActionButtonFeedback::showBusy(m_unstageAllButton, "正在取消暂存…");
    const quint64 sessionGeneration = m_repositoryGeneration;
    const QString sessionPath = m_currentProjectPath;
    m_controller->unstageAllAsync(
        [this, sessionGeneration, sessionPath](
            GitController::OperationResult result) {
        if (!isRepositorySessionCurrent(sessionGeneration, sessionPath)) {
            return;
        }
        if (!result.success) {
            const bool lockConflict =
                result.error.contains("index.lock", Qt::CaseInsensitive);
            ActionButtonFeedback::showError(
                m_unstageAllButton,
                lockConflict ? "另一个 Git 操作正在进行，请稍后重试"
                             : "取消暂存失败");
            m_errorDisplay->setText(
                (lockConflict ? "取消暂存失败：另一个 Git 操作正在进行\n\n"
                              : "取消暂存失败\n\n")
                + result.error);
            m_errorDisplay->show();
            return;
        }
        ActionButtonFeedback::showSuccess(m_unstageAllButton, "取消暂存成功");
        m_errorDisplay->clear();
        m_errorDisplay->hide();
    });
}

void MainWindow::updateStageActionState() {
    const auto snapshot = m_controller->viewModel()->snapshot();
    bool hasStageableFiles = false;
    bool hasStagedFiles = false;
    for (const auto &file : snapshot.files) {
        if (file.status == FileStatus::Untracked || (file.xy[1] != 0 && file.xy[1] != '.')) {
            hasStageableFiles = true;
        }
        if (file.xy[0] != 0 && file.xy[0] != '.' && file.xy[0] != '?') {
            hasStagedFiles = true;
        }
    }

    hasStageableFiles = m_repositorySessionReady && hasStageableFiles;
    hasStagedFiles = m_repositorySessionReady && hasStagedFiles;
    ActionButtonFeedback::setReady(m_stageAllButton, hasStageableFiles);
    ActionButtonFeedback::setReady(m_unstageAllButton, hasStagedFiles);
    m_stageAllButton->setToolTip(hasStageableFiles ? QString() : "没有可暂存的文件");
    m_unstageAllButton->setToolTip(hasStagedFiles ? QString() : "没有已暂存的文件");

    // v4-P1-4: merge button follows branch selection.
    const QString branch = selectedBranch();
    const QString current = m_controller->viewModel()->currentBranch();
    const bool canMerge =
        m_repositorySessionReady && !branch.isEmpty() && branch != current;
    m_mergeButton->setEnabled(canMerge);

    // v4-P0-R1.3: single-file stage button follows the selected change.
    const QTreeWidgetItem *item = m_changesTree->currentItem();
    bool canStageSelected = false;
    if (item) {
        const QString path = item->data(0, Qt::UserRole).toString();
        for (const auto &file : snapshot.files) {
            if (file.path == path
                && (file.status == FileStatus::Untracked
                    || (file.xy[1] != 0 && file.xy[1] != '.'))) {
                canStageSelected = true;
                break;
            }
        }
    }
    ActionButtonFeedback::setReady(
        m_stageButton, m_repositorySessionReady && canStageSelected);

    // v4-P6-21R2: commit enabled state is derived in ONE reducer.
    updateCommitActionState();
}

void MainWindow::updateCommitActionState() {
    // v4-P6-21R2: single source of truth for the commit action.
    const auto snapshot = m_controller->viewModel()->snapshot();
    bool hasStagedChanges = false;
    for (const auto &file : snapshot.files) {
        if (file.xy[0] != 0 && file.xy[0] != '.' && file.xy[0] != '?') {
            hasStagedChanges = true;
            break;
        }
    }
    const bool repoAvailable =
        m_repositorySessionReady && !m_currentProjectPath.isEmpty()
        && !m_controller->viewModel()->currentBranch().isEmpty();
    const bool messageOk =
        m_commitMessageEdit
        && !m_commitMessageEdit->text().trimmed().isEmpty();
    const bool enabled = repoAvailable && hasStagedChanges && messageOk
                         && !m_commitOperationBusy;
    if (m_commitButton) {
        ActionButtonFeedback::setReady(m_commitButton, enabled);
    }
    if (m_commitMessageEdit) {
        m_commitMessageEdit->setEnabled(
            m_repositorySessionReady && !m_currentProjectPath.isEmpty()
            && hasStagedChanges
            && !m_commitOperationBusy);
    }
}

void MainWindow::onCommit() {
    if (!m_repositorySessionReady) {
        return;
    }
    // v4-P6-21R2: hard business gate - a busy commit cannot be
    // re-entered even if the button were somehow enabled.
    if (m_commitOperationBusy) {
        return;
    }
    QString message = m_commitMessageEdit->text().trimmed();
    if (message.isEmpty()) {
        m_errorDisplay->setText("请输入提交说明");
        m_errorDisplay->show();
        return;
    }
    m_commitOperationBusy = true;
    updateCommitActionState();
    ActionButtonFeedback::showBusy(m_commitButton, "正在保存…");
    const quint64 sessionGeneration = m_repositoryGeneration;
    const QString sessionPath = m_currentProjectPath;
    m_controller->createCommitAsync(message, [this, sessionGeneration, sessionPath](GitController::OperationResult result) {
        if (!isRepositorySessionCurrent(sessionGeneration, sessionPath)) {
            return;
        }
        m_commitOperationBusy = false;
        updateCommitActionState();
        if (!result.success) {
            ActionButtonFeedback::showError(m_commitButton, "保存失败");
            m_errorDisplay->setText("提交失败：" + result.error);
            m_errorDisplay->show();
            return;
        }
        ActionButtonFeedback::showSuccess(m_commitButton, "保存成功");
        m_commitMessageEdit->clear();
        m_errorDisplay->clear();
        m_errorDisplay->hide();
        onCommitFinished();
    });
}

void MainWindow::onCommitFinished() {
    m_errorDisplay->clear();
    m_errorDisplay->hide();
    m_suggestionLabel->setText("建议：提交成功！");
    m_suggestionLabel->setProperty("state", QStringLiteral("ok"));
    restyle(m_suggestionLabel);
}

void MainWindow::onCommitError() {
    m_errorDisplay->setText("提交失败：" + m_controller->viewModel()->error());
    m_errorDisplay->show();
}

void MainWindow::showWorkspace() {
    // v4-P6-12: without a loaded repository the workspace would be a
    // wall of empty panels - route to the dedicated guide page instead.
    if (m_currentProjectPath.isEmpty()) {
        showNoRepository();
        return;
    }
    m_stack->setCurrentIndex(1);
}

void MainWindow::showNoRepository() {
    m_stack->setCurrentWidget(m_noRepoWidget);
}

void MainWindow::onGoHome() {
    if (ActionButtonFeedback::isActive(m_goHomeButton)) {
        return;
    }
    ActionButtonFeedback::showBusy(m_goHomeButton, "正在返回…");
    m_welcomeWidget->updateRecentProjects(m_projectController->recentProjects());
    m_stack->setCurrentIndex(0);
    if (m_stack->currentIndex() == 0) {
        ActionButtonFeedback::setReady(m_goHomeButton, true);
        ActionButtonFeedback::showSuccess(m_goHomeButton, "已返回首页", 700);
        return;
    }
    ActionButtonFeedback::setReady(m_goHomeButton, true);
    ActionButtonFeedback::showError(m_goHomeButton, "返回失败", 1300);
}

void MainWindow::showWelcome() {
    m_welcomeWidget->updateRecentProjects(m_projectController->recentProjects());
    m_stack->setCurrentIndex(0);
}

void MainWindow::onCloneRepository() {
    auto *cloneButton = m_welcomeWidget->findChild<QPushButton *>("cloneButton");
    CloneDialog dialog(m_projectController, this);
    if (dialog.exec() == QDialog::Accepted) {
        ActionButtonFeedback::showSuccess(cloneButton, "克隆成功");
        onProjectSelected(dialog.targetPath());
    } else {
        ActionButtonFeedback::reset(cloneButton);
    }
}
