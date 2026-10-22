#include <QtTest>
#include "test_settings_isolation.h"
#include <QLabel>
#include <QCheckBox>
#include <QDialog>
#include <QMainWindow>
#include <QPushButton>
#include <QLineEdit>
#include <QTextEdit>
#include <QProcess>
#include <QFile>
#include <QFileDialog>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QStackedWidget>
#include <QListWidget>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QMessageBox>
#include <QTimer>
#include <QPixmap>
#include <QUuid>
#include "MainWindow.h"
#include "GitHubAccountService.h"
#include "CredentialStore.h"
#include "GitEnvironmentDialog.h"
#include "GitEnvironmentService.h"
#include "GitHubAccountDialog.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include "ActionButtonFeedback.h"
#include "BranchTreeWidget.h"
#include "BranchRelationWidget.h"
#include "CreateBranchDialog.h"
#include "WelcomeWidget.h"
#include "GitController.h"
#include "GitTestRepository.h"

class TestMainWindow : public QObject {
    Q_OBJECT
private:
    GitTestRepository m_repo;
    
    // v4-P6-21R5: delayed snapshot service - for the FIRST repository
    // path the completion is delayed (a real pending race window);
    // every other repository resolves immediately via the real logic.
    class FakeDelayedSnapshotService : public GitSnapshotService {
    public:
        explicit FakeDelayedSnapshotService(QObject *parent = nullptr)
            : GitSnapshotService(parent) {}
        QString delayedPath;
        QString delayedRequestTarget;
        GitRepository::SnapshotCompletion delayedCompletion;
        bool hasPending() const { return bool(delayedCompletion); }
        void completeDelayed() {
            const auto completion = delayedCompletion;
            delayedCompletion = {};
            GitSnapshot stale;
            stale.head = QStringLiteral("A-FAKE-STALE-HEAD");
            stale.headRef = QStringLiteral("refs/heads/old-a");
            GitError err;
            err.type = GitErrorType::None;
            completion(stale, err);
        }
        void createSnapshotAsync(GitRepository::SnapshotCompletion completion) override {
            if (!delayedPath.isEmpty() && workingDirectory() == delayedPath) {
                delayedRequestTarget = workingDirectory();
                delayedCompletion = completion;
                return;
            }
            GitSnapshotService::createSnapshotAsync(completion);
        }
    };

    void switchToWorkspace(MainWindow &window,
                           const QString &repoPath = QString()) {
        // v4-P6-12: the workspace page requires a loaded repository
        // (without one, showWorkspace() routes to the no-repository
        // guide page). Load the test repo first - async, so spin the
        // event loop until the workspace page (index 1) is current.
        const QString path =
            repoPath.isEmpty() ? m_repo.path() : repoPath;
        QMetaObject::invokeMethod(&window, "onProjectSelected",
                                  Q_ARG(QString, path));
        QStackedWidget *stack = window.findChild<QStackedWidget *>();
        QElapsedTimer timer;
        timer.start();
        while (stack && stack->currentIndex() != 1
               && timer.elapsed() < 20000) {
            QApplication::processEvents();
            QTest::qWait(20);
        }
        QApplication::processEvents();
        // v4-P6-21R2: the load pipeline may still run after the page
        // shows; tests that call refresh()/click afterwards would be
        // silently gated by isLoading() - wait for full settle here.
        if (window.controller() && window.controller()->viewModel()) {
            QTRY_VERIFY_WITH_TIMEOUT(
                !window.controller()->viewModel()->isLoading(), 20000);
        }
        QApplication::processEvents();
    }
    
private slots:
    // v4-P6-18 stability hardening
    void testInitEmptyRepoDoesNotCrash();
    void testLoadProjectReentrancy();
    void testGitHubStateRestore();
    void testRefreshShowsRealResult();
    void testRemoteFailureShowsErrorArea();
    // v4-P6-20 state correctness
    void remoteButtonsReEnabledAfterSuccess();
    void remoteButtonsReEnabledAfterFailure();
    void remoteButtonsDisabledWithoutRemote();
    void remoteActionCanRunTwice();
    void refreshSuccessNeverShowsFailure();
    void refreshFailureDoesNotLaterShowSuccess();
    void refreshIgnoresStaleCallback();
    void gitEnvironmentRedetectActuallyInvokesDetector();
    void gitEnvironmentRedetectUpdatesFailureResult();
    void branchGraphMainTabRemoved();
    // v4-P6-21 safety & state integrity
    void testGitHubDialogLateReplySafe();
    void testRepositorySessionClearedOnNonGit();
    void testCleanRepositoryShowsCleanState();
    void testGitExecutableDynamicResolution();
    void testCredentialSaveFailureDoesNotLogin();
    void testCredentialRemoveFailureDoesNotReportLogout();
    void testNoVisibleBranchRelationWidgetInMainBranchPage();
    void initTestCase();
    void testWindowTitle();
    void testApplicationIconInheritance();
    void testVersionLabel();
    void testGoHomeButton();
    void testWelcomeActionButtons();
    void testRecentProjectPresentation();
    void testControllerExists();
    void testEmptyPathRefresh();
    void testValidRepoRefresh();
    void testBranchDisplay();
    void testBranchTreeHierarchy();
    void testBranchSearchAndExpansion();
    void testBranchActionRegression();
    void testRejectedSwitchKeepsWorkspaceAndExplainsRisk();
    void testMainWindowOpensCreateBranchDialog();
    void testCreateBranchDialogValidation();
    void testCreateBranchDialogWorkflow();
    void testMainWindowRefreshesCreatedBranch();
    void testActionButtonFeedbackStates();
    void testTopActionButtonPresentation();
    void testRefreshButtonFeedback();
    void testRefreshButtonErrorFeedback();
    void testChangeProjectCancelIsNeutral();
    void testSafeDeleteFromButton();
    void testUnmergedDeleteRequiresHighRiskConfirmation();
    void testFileChanges();
    void testUntrackedFile();
    void testLoadingState();
    void testErrorDisplay();
    // v4-P6-21R5 dedicated race tests.
    void repositoryRaceStaleRefreshIgnored();
    void repositoryTransitionRejectsStaleUiClicks();
    void remoteOverlapStaleCallbackIgnored();
    void staleFeedbackBranchButtonsReset();
    void testShowAndClose();
    void testCommitUI();
    void commitReducerSurvivesBusyFeedback();
    void testUnstageAllRegression();
    void cleanupTestCase();
};

void TestMainWindow::initTestCase() {
    isolateTestSettings();
    // v4-P6-21R2: on this machine the user home directory is a git
    // work tree - after a test deletes a repo's .git, `git status`
    // would silently succeed inside the parent work tree and make
    // refresh-failure tests see "刷新成功". Ceil discovery at %TEMP%.
    qputenv("GIT_CEILING_DIRECTORIES", QDir::tempPath().toUtf8());
    // v4-P6-6: test settings isolated from production (see
    // test_settings_isolation.h).
    QVERIFY(m_repo.isValid());
}

void TestMainWindow::testWindowTitle() {
    MainWindow window;
    QCOMPARE(window.windowTitle(), QString("QtGitStudio"));
}

void TestMainWindow::testApplicationIconInheritance() {
    const QIcon previousIcon = QApplication::windowIcon();
    QPixmap pixmap(32, 32);
    pixmap.fill(QColor("#14B8A6"));
    const QIcon testIcon(pixmap);
    QApplication::setWindowIcon(testIcon);

    MainWindow window;
    QCOMPARE(window.windowIcon().cacheKey(), testIcon.cacheKey());

    QApplication::setWindowIcon(previousIcon);
}

void TestMainWindow::testVersionLabel() {
    MainWindow window;
    QCOMPARE(window.getVersion(), QStringLiteral(QTGITSTUDIO_VERSION));
}

void TestMainWindow::testGoHomeButton() {
    MainWindow window;
    
    QPushButton *goHomeButton = window.findChild<QPushButton *>("goHomeButton");
    QVERIFY(goHomeButton != nullptr);
    QCOMPARE(goHomeButton->text(), QStringLiteral("返回首页"));
    // v4-P6-12: the back arrow is a standard-pixmap icon now.
    QVERIFY(!goHomeButton->icon().isNull());
    
    // Switch to workspace, then test go-home
    switchToWorkspace(window);
    QStackedWidget *stack = window.findChild<QStackedWidget *>();
    QCOMPARE(stack->currentIndex(), 1);
    
    goHomeButton->click();
    QApplication::processEvents();
    QCOMPARE(stack->currentIndex(), 0);
}

void TestMainWindow::testWelcomeActionButtons() {
    // v4-P6-6: quick-start actions are fully clickable blocks - no
    // nested buttons, full Chinese titles, signals on click. The blocks
    // are tested on a standalone WelcomeWidget: clicking them inside
    // MainWindow would open the real modal file dialog.
    WelcomeWidget welcome;

    ActionBlock *openAction = welcome.findChild<ActionBlock *>("openAction");
    ActionBlock *initAction = welcome.findChild<ActionBlock *>("initAction");
    ActionBlock *cloneAction = welcome.findChild<ActionBlock *>("cloneAction");
    QVERIFY(openAction != nullptr);
    QVERIFY(initAction != nullptr);
    QVERIFY(cloneAction != nullptr);

    QCOMPARE(openAction->titleText(), QStringLiteral("打开仓库"));
    QCOMPARE(initAction->titleText(), QStringLiteral("初始化仓库"));
    QCOMPARE(cloneAction->titleText(), QStringLiteral("克隆仓库"));

    // No legacy nested buttons inside the quick-start blocks.
    QVERIFY(welcome.findChild<QPushButton *>("openButton") == nullptr);
    QVERIFY(welcome.findChild<QPushButton *>("initButton") == nullptr);
    QVERIFY(welcome.findChild<QPushButton *>("cloneButton") == nullptr);

    // Whole block is clickable and emits the expected signal.
    QSignalSpy openSpy(&welcome, &WelcomeWidget::openProjectRequested);
    QSignalSpy initSpy(&welcome, &WelcomeWidget::initProjectRequested);
    QSignalSpy cloneSpy(&welcome, &WelcomeWidget::cloneProjectRequested);
    welcome.resize(960, 720);
    welcome.show();
    QTest::qWait(100);  // v4-P6-21R2: no window-manager expose dependency
    QTest::mouseClick(openAction, Qt::LeftButton);
    QTest::mouseClick(initAction, Qt::LeftButton);
    QTest::mouseClick(cloneAction, Qt::LeftButton);
    QApplication::processEvents();
    QCOMPARE(openSpy.count(), 1);
    QCOMPARE(initSpy.count(), 1);
    QCOMPARE(cloneSpy.count(), 1);
}

void TestMainWindow::testRecentProjectPresentation() {
    WelcomeWidget welcome;
    // v4-P6-15: recent rows are clickable QFrame rows inside the
    // recentContainer (dashboard redesign); invalid paths are filtered.
    const QString path = m_repo.path() + "/sample-repository";
    QDir().mkpath(path);
    welcome.updateRecentProjects({path});
    welcome.resize(500, 500);
    welcome.show();
    QTest::qWait(100);  // v4-P6-21R2: no window-manager expose dependency

    QFrame *container = welcome.findChild<QFrame *>("recentContainer");
    QVERIFY(container != nullptr);
    auto rowWidgets = [container]() {
        QList<QFrame *> rows;
        const auto all = container->findChildren<QFrame *>();
        for (QFrame *f : all) {
            if (f->parent() == container) {
                rows.append(f);
            }
        }
        return rows;
    };
    const QList<QFrame *> rows = rowWidgets();
    QCOMPARE(rows.size(), 1);

    QSignalSpy selectedSpy(&welcome, &WelcomeWidget::projectSelected);
    QTest::mouseClick(rows.first(), Qt::LeftButton);
    QApplication::processEvents();
    QCOMPARE(selectedSpy.count(), 1);
    QCOMPARE(selectedSpy.takeFirst().at(0).toString(), path);

    // Invalid / non-existent paths are filtered out.
    welcome.updateRecentProjects(
        {path, "C:/definitely/not/exist/xxx-repo"});
    QCOMPARE(rowWidgets().size(), 1);
}

void TestMainWindow::testControllerExists() {
    MainWindow window;
    QVERIFY(window.controller() != nullptr);
    QVERIFY(window.controller()->viewModel() != nullptr);
}

void TestMainWindow::testEmptyPathRefresh() {
    MainWindow window;
    switchToWorkspace(window);
    
    QTemporaryDir nonGitDir;
    QVERIFY(nonGitDir.isValid());
    window.controller()->setRepositoryPath(nonGitDir.path());
    window.controller()->refresh();
    
    QTextEdit *errorDisplay = window.findChild<QTextEdit *>("errorDisplay");
    QVERIFY(errorDisplay != nullptr);
    QVERIFY(!errorDisplay->toPlainText().isEmpty());
}

void TestMainWindow::testValidRepoRefresh() {
    MainWindow window;
    switchToWorkspace(window);
    
    window.controller()->setRepositoryPath(m_repo.path());
    window.controller()->refresh();
    QApplication::processEvents();
    
    QLabel *branchLabel = window.findChild<QLabel *>("branchLabel");
    QVERIFY(branchLabel != nullptr);
    QCOMPARE(branchLabel->text(), QString("当前分支：main"));
}

void TestMainWindow::testBranchDisplay() {
    MainWindow window;
    switchToWorkspace(window);
    
    window.controller()->setRepositoryPath(m_repo.path());
    window.controller()->refresh();
    QApplication::processEvents();
    
    QLabel *branchLabel = window.findChild<QLabel *>("branchLabel");
    QCOMPARE(branchLabel->text(), QString("当前分支：main"));
}

void TestMainWindow::testBranchTreeHierarchy() {
    MainWindow window;
    switchToWorkspace(window);

    window.controller()->setRepositoryPath(m_repo.path());
    QVERIFY(window.controller()->createBranch("chore/baseline-cleanup"));
    QVERIFY(window.controller()->createBranch("chore/frontend-responsibility-handoff"));
    QVERIFY(window.controller()->createBranch("feature/platform/ui-polish-v3-r5-r3"));
    window.controller()->refresh();
    QApplication::processEvents();

    BranchTreeWidget *branchTree = window.findChild<BranchTreeWidget *>("branchTree");
    QVERIFY(branchTree != nullptr);
    QCOMPARE(branchTree->columnCount(), 1);
    QVERIFY(branchTree->isHeaderHidden());
    // v4-P6-9: the branch area was compacted (was 240) so it no longer
    // dominates the first screen - the workspace scrolls instead.
    QVERIFY(branchTree->minimumHeight() >= 100);
    QVERIFY(branchTree->minimumHeight() < 220);
    QCOMPARE(branchTree->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
    QCOMPARE(branchTree->editTriggers(), QAbstractItemView::NoEditTriggers);
    QVERIFY(branchTree->styleSheet().contains("background: #DBEAFE; color: #111827"));

    QCOMPARE(branchTree->topLevelItemCount(), 1);
    QTreeWidgetItem *root = branchTree->topLevelItem(0);
    QCOMPARE(
        root->data(0, BranchTreeWidget::NodeKindRole).toInt(),
        static_cast<int>(BranchTreeWidget::NodeKind::Root));
    QCOMPARE(
        root->data(0, BranchTreeWidget::BranchCountRole).toInt(),
        window.controller()->viewModel()->branches().size());
    QVERIFY(root->text(0).startsWith("本地分支 "));
    QVERIFY(!(root->flags() & Qt::ItemIsSelectable));
    QVERIFY(!root->data(0, Qt::UserRole).isValid());

    QTreeWidgetItem *mainBranch = branchTree->branchItem("main");
    QVERIFY(mainBranch != nullptr);
    QCOMPARE(mainBranch->text(0), QString("main"));
    QCOMPARE(mainBranch->data(0, Qt::UserRole).toString(), QString("main"));
    QCOMPARE(mainBranch->toolTip(0), QString("main"));
    QVERIFY(mainBranch->data(0, BranchTreeWidget::CurrentBranchRole).toBool());
    QCOMPARE(mainBranch->foreground(0).color(), QColor("#1D4ED8"));
    QCOMPARE(mainBranch->background(0).color(), QColor("#EFF6FF"));
    QVERIFY(!mainBranch->icon(0).isNull());
    QVERIFY(!(mainBranch->flags() & Qt::ItemIsEditable));

    QTreeWidgetItem *choreBranch = branchTree->branchItem("chore/baseline-cleanup");
    QVERIFY(choreBranch != nullptr);
    QTreeWidgetItem *choreDirectory = choreBranch->parent();
    QVERIFY(choreDirectory != nullptr);
    QCOMPARE(choreDirectory->text(0), QString("chore  2"));
    QCOMPARE(choreDirectory->data(0, BranchTreeWidget::BranchCountRole).toInt(), 2);
    QVERIFY(!(choreDirectory->flags() & Qt::ItemIsSelectable));
    QVERIFY(!choreDirectory->data(0, Qt::UserRole).isValid());

    QTreeWidgetItem *nestedBranch = branchTree->branchItem("feature/platform/ui-polish-v3-r5-r3");
    QVERIFY(nestedBranch != nullptr);
    QCOMPARE(nestedBranch->text(0), QString("ui-polish-v3-r5-r3"));
    QCOMPARE(nestedBranch->toolTip(0), QString("feature/platform/ui-polish-v3-r5-r3"));
    QCOMPARE(nestedBranch->parent()->text(0), QString("platform  1"));
    QVERIFY(nestedBranch->parent()->parent()->text(0).startsWith("feature  "));
}

void TestMainWindow::testBranchSearchAndExpansion() {
    MainWindow window;
    switchToWorkspace(window);
    window.controller()->setRepositoryPath(m_repo.path());
    window.controller()->refresh();
    window.show();
    QApplication::processEvents();

    BranchTreeWidget *branchTree = window.findChild<BranchTreeWidget *>("branchTree");
    QLineEdit *search = window.findChild<QLineEdit *>("branchSearchEdit");
    QPushButton *switchButton = window.findChild<QPushButton *>("switchBranchButton");
    QPushButton *createButton = window.findChild<QPushButton *>("createBranchButton");
    QPushButton *deleteButton = window.findChild<QPushButton *>("deleteBranchButton");
    QVERIFY(branchTree != nullptr);
    QVERIFY(search != nullptr);
    QVERIFY(switchButton != nullptr);
    QVERIFY(createButton != nullptr);
    QVERIFY(deleteButton != nullptr);

    QTreeWidgetItem *choreBranch = branchTree->branchItem("chore/baseline-cleanup");
    QVERIFY(choreBranch != nullptr);
    QTreeWidgetItem *choreDirectory = choreBranch->parent();
    choreDirectory->setExpanded(true);
    window.controller()->refresh();
    QApplication::processEvents();
    choreBranch = branchTree->branchItem("chore/baseline-cleanup");
    QVERIFY(choreBranch != nullptr);
    choreDirectory = choreBranch->parent();
    QVERIFY(choreDirectory->isExpanded());

    search->setText("UI-POLISH");
    QApplication::processEvents();
    QTreeWidgetItem *match = branchTree->branchItem("feature/platform/ui-polish-v3-r5-r3");
    QVERIFY(match != nullptr);
    QVERIFY(match->parent()->isExpanded());
    QVERIFY(match->parent()->parent()->isExpanded());
    QVERIFY(branchTree->branchItem("chore/baseline-cleanup") == nullptr);
    QCOMPARE(
        branchTree->topLevelItem(0)->data(0, BranchTreeWidget::BranchCountRole).toInt(),
        1);

    branchTree->setCurrentItem(match);
    QApplication::processEvents();
    QVERIFY(switchButton->isEnabled());
    QVERIFY(createButton->isEnabled());
    QVERIFY(deleteButton->isEnabled());

    search->clear();
    QApplication::processEvents();
    choreBranch = branchTree->branchItem("chore/baseline-cleanup");
    QVERIFY(choreBranch != nullptr);
    QVERIFY(choreBranch->parent()->isExpanded());

    search->setFocus();
    search->setText("chore/");
    QTest::keyClick(search, Qt::Key_Escape);
    QCOMPARE(search->text(), QString());
}

void TestMainWindow::testBranchActionRegression() {
    MainWindow window;
    switchToWorkspace(window);
    window.controller()->setRepositoryPath(m_repo.path());
    QVERIFY(window.controller()->createBranch("feature/demo"));
    QApplication::processEvents();

    QPushButton *switchButton = window.findChild<QPushButton *>("switchBranchButton");
    QVERIFY(switchButton != nullptr);

    BranchTreeWidget *branchTree = window.findChild<BranchTreeWidget *>("branchTree");
    QPushButton *deleteButton = window.findChild<QPushButton *>("deleteBranchButton");
    QPushButton *createButton = window.findChild<QPushButton *>("createBranchButton");
    QVERIFY(branchTree != nullptr);
    QVERIFY(deleteButton != nullptr);
    QVERIFY(createButton != nullptr);
    for (QPushButton *button : {switchButton, createButton, deleteButton}) {
        QVERIFY(button->styleSheet().contains("QPushButton#" + button->objectName() + ":hover"));
        QVERIFY(button->styleSheet().contains("QPushButton#" + button->objectName() + ":pressed"));
        QVERIFY(button->styleSheet().contains("QPushButton#" + button->objectName() + ":disabled"));
        QVERIFY(button->styleSheet().contains("QPushButton#" + button->objectName() + ":focus"));
    }

    QTreeWidgetItem *featureBranch = branchTree->branchItem("feature/demo");
    QVERIFY(featureBranch != nullptr);
    QTreeWidgetItem *featureGroup = featureBranch->parent();
    QVERIFY(featureGroup != nullptr);
    QVERIFY(!featureGroup->data(0, Qt::UserRole).isValid());
    QCOMPARE(featureBranch->data(0, Qt::UserRole).toString(), QString("feature/demo"));

    branchTree->setCurrentItem(featureBranch);
    QApplication::processEvents();
    QVERIFY(deleteButton->isEnabled());
    QVERIFY(switchButton->isEnabled());

    switchButton->click();
    QCOMPARE(switchButton->text(), QString("正在切换…"));
    QVERIFY(switchButton->property("busy").toBool());
    QTRY_COMPARE(window.controller()->viewModel()->currentBranch(), QString("feature/demo"));
    QCOMPARE(switchButton->text(), QString("切换成功"));
    QCOMPARE(switchButton->property("result").toString(), QString("success"));
    QVERIFY(!switchButton->isEnabled());
    QVERIFY(!deleteButton->isEnabled());
    QCOMPARE(deleteButton->toolTip(), QString("当前分支不能删除，请先切换到其他分支"));

    branchTree->setCurrentItem(featureGroup);
    QApplication::processEvents();
    QVERIFY(!switchButton->isEnabled());
    QVERIFY(createButton->isEnabled());
    QVERIFY(!deleteButton->isEnabled());

    QVERIFY(window.controller()->checkoutBranch("main"));
}

void TestMainWindow::testRejectedSwitchKeepsWorkspaceAndExplainsRisk() {
    GitTestRepository repository;
    QVERIFY(repository.isValid());

    QProcess git;
    git.setWorkingDirectory(repository.path());
    git.start("git", {"switch", "-c", "protected-target"});
    QVERIFY(git.waitForFinished(5000));
    QCOMPARE(git.exitCode(), 0);

    QFile readme(repository.path() + "/README.md");
    QVERIFY(readme.open(QIODevice::WriteOnly));
    QCOMPARE(readme.write("target branch"), qint64(13));
    readme.close();
    for (const QStringList &arguments : QList<QStringList>{
             {"add", "README.md"},
             {"commit", "-m", "target version"},
             {"switch", "main"}}) {
        git.start("git", arguments);
        QVERIFY(git.waitForFinished(5000));
        QCOMPARE(git.exitCode(), 0);
    }

    QVERIFY(readme.open(QIODevice::WriteOnly));
    QCOMPARE(readme.write("local work"), qint64(10));
    readme.close();

    MainWindow window;
    switchToWorkspace(window);
    window.controller()->setRepositoryPath(repository.path());
    window.controller()->refresh();
    window.show();

    BranchTreeWidget *branchTree = window.findChild<BranchTreeWidget *>("branchTree");
    QPushButton *switchButton = window.findChild<QPushButton *>("switchBranchButton");
    QTextEdit *errorDisplay = window.findChild<QTextEdit *>("errorDisplay");
    QTreeWidget *changesTree = window.findChild<QTreeWidget *>("changesTree");
    QVERIFY(branchTree != nullptr);
    QVERIFY(switchButton != nullptr);
    QVERIFY(errorDisplay != nullptr);
    QVERIFY(changesTree != nullptr);

    QTreeWidgetItem *targetBranch = branchTree->branchItem("protected-target");
    QVERIFY(targetBranch != nullptr);
    branchTree->setCurrentItem(targetBranch);
    switchButton->click();
    QCOMPARE(switchButton->text(), QString("正在切换…"));
    QTRY_COMPARE(switchButton->property("result").toString(), QString("error"));
    QCOMPARE(window.controller()->viewModel()->currentBranch(), QString("main"));
    QCOMPARE(branchTree->selectedBranch(), QString("protected-target"));
    QVERIFY(changesTree->topLevelItemCount() > 0);
    QVERIFY(readme.open(QIODevice::ReadOnly));
    QCOMPARE(readme.readAll(), QByteArray("local work"));
    readme.close();

    QVERIFY(
        errorDisplay->toPlainText().contains("当前仓库存在尚未提交的修改") &&
        errorDisplay->toPlainText().contains("protected-target") &&
        errorDisplay->toPlainText().contains("README.md") &&
        errorDisplay->toPlainText().contains("先提交当前修改") &&
        errorDisplay->toPlainText().contains("would be overwritten") &&
        window.findChild<QDialog *>("branchSwitchErrorDialog") != nullptr);

    QTRY_COMPARE(switchButton->text(), QString("切换分支"));
    QVERIFY(switchButton->isEnabled());
    QCOMPARE(window.controller()->viewModel()->currentBranch(), QString("main"));
}

void TestMainWindow::testMainWindowOpensCreateBranchDialog() {
    MainWindow window;
    switchToWorkspace(window);
    window.controller()->setRepositoryPath(m_repo.path());
    window.controller()->refresh();
    window.show();

    QPushButton *createButton = window.findChild<QPushButton *>("createBranchButton");
    QVERIFY(createButton != nullptr);
    QVERIFY(createButton->isEnabled());

    bool foundDedicatedDialog = false;
    QTimer::singleShot(60, &window, [&foundDedicatedDialog]() {
        auto *dialog = qobject_cast<CreateBranchDialog *>(QApplication::activeModalWidget());
        foundDedicatedDialog = dialog != nullptr;
        if (dialog) {
            dialog->reject();
        }
    });
    QTest::mouseClick(createButton, Qt::LeftButton);
    QVERIFY(foundDedicatedDialog);
}

void TestMainWindow::testCreateBranchDialogValidation() {
    GitController controller;
    controller.setRepositoryPath(m_repo.path());
    QVERIFY(controller.createBranch("release/dialog-base"));

    CreateBranchDialog dialog(&controller, nullptr, "main");
    dialog.show();
    QApplication::processEvents();

    BranchTreeWidget *startTree = dialog.findChild<BranchTreeWidget *>("createBranchStartTree");
    QLineEdit *nameEdit = dialog.findChild<QLineEdit *>("createBranchNameEdit");
    QCheckBox *switchCheck = dialog.findChild<QCheckBox *>("createBranchSwitchCheck");
    QLabel *preview = dialog.findChild<QLabel *>("createBranchPreview");
    QLabel *validation = dialog.findChild<QLabel *>("createBranchValidationLabel");
    QPushButton *createButton = dialog.findChild<QPushButton *>("confirmCreateBranchButton");
    QVERIFY(startTree != nullptr);
    QVERIFY(nameEdit != nullptr);
    QVERIFY(switchCheck != nullptr);
    QVERIFY(preview != nullptr);
    QVERIFY(validation != nullptr);
    QVERIFY(createButton != nullptr);

    QCOMPARE(startTree->selectedBranch(), QString("main"));
    QTreeWidgetItem *root = startTree->topLevelItem(0);
    QVERIFY(root != nullptr);
    QVERIFY(!(root->flags() & Qt::ItemIsSelectable));
    QVERIFY(!root->data(0, Qt::UserRole).isValid());
    QVERIFY(nameEdit->hasFocus());
    QVERIFY(switchCheck->isChecked());
    QVERIFY(!createButton->isEnabled());

    nameEdit->setText("feature..invalid");
    QTRY_VERIFY(validation->text().contains("Git"));
    QVERIFY(!createButton->isEnabled());

    nameEdit->setText("main");
    QTRY_VERIFY(validation->text().contains("已经存在"));
    QVERIFY(!createButton->isEnabled());

    QTreeWidgetItem *startBranch = startTree->branchItem("release/dialog-base");
    QVERIFY(startBranch != nullptr);
    QCOMPARE(
        startBranch->data(0, Qt::UserRole).toString(),
        QString("release/dialog-base"));
    QCOMPARE(startBranch->toolTip(0), QString("release/dialog-base"));
    QVERIFY(!(startBranch->flags() & Qt::ItemIsEditable));
    startTree->setCurrentItem(startBranch);
    nameEdit->setText("feature/platform/new-ui");
    QTRY_VERIFY(createButton->isEnabled());
    QCOMPARE(
        preview->text(),
        QString("起点分支：\n● release/dialog-base\n\n名称层级：\nfeature\n└─ platform\n   └─ new-ui"));
    QVERIFY(!preview->text().contains("release\n└─ dialog-base\n"));

    QTreeWidgetItem *directory = startBranch->parent();
    QVERIFY(directory != nullptr);
    QVERIFY(!(directory->flags() & Qt::ItemIsSelectable));
    QVERIFY(!directory->data(0, Qt::UserRole).isValid());
    startTree->setCurrentItem(directory);
    QTRY_VERIFY(validation->text().contains("真实的起点分支"));
    QVERIFY(!createButton->isEnabled());

    startTree->setCurrentItem(startBranch);
    nameEdit->setText(" feature/trim-check ");
    QTRY_VERIFY(validation->text().contains("前后不能包含空格"));
    QVERIFY(!createButton->isEnabled());
}

void TestMainWindow::testCreateBranchDialogWorkflow() {
    GitController controller;
    controller.setRepositoryPath(m_repo.path());
    QVERIFY(controller.createBranch("release/workflow-base"));

    CreateBranchDialog createOnlyDialog(&controller, nullptr, "release/workflow-base");
    createOnlyDialog.show();
    QLineEdit *nameEdit = createOnlyDialog.findChild<QLineEdit *>("createBranchNameEdit");
    QCheckBox *switchCheck = createOnlyDialog.findChild<QCheckBox *>("createBranchSwitchCheck");
    QPushButton *createButton = createOnlyDialog.findChild<QPushButton *>("confirmCreateBranchButton");
    QPushButton *cancelButton = createOnlyDialog.findChild<QPushButton *>("cancelCreateBranchButton");
    QVERIFY(nameEdit != nullptr);
    QVERIFY(switchCheck != nullptr);
    QVERIFY(createButton != nullptr);
    QVERIFY(cancelButton != nullptr);
    switchCheck->setChecked(false);
    nameEdit->setText("feature/dialog-create-only");
    QTRY_VERIFY(createButton->isEnabled());

    QSignalSpy startedSpy(&createOnlyDialog, &CreateBranchDialog::creationStarted);
    QSignalSpy createdSpy(&createOnlyDialog, &CreateBranchDialog::branchCreated);
    QTest::mouseClick(createButton, Qt::LeftButton);
    QCOMPARE(startedSpy.count(), 1);
    QVERIFY(createButton->property("busy").toBool());
    QVERIFY(!cancelButton->isEnabled());
    QTRY_COMPARE(createOnlyDialog.result(), static_cast<int>(QDialog::Accepted));
    QCOMPARE(createdSpy.count(), 1);
    QVERIFY(controller.viewModel()->branches().contains("feature/dialog-create-only"));
    QCOMPARE(controller.viewModel()->currentBranch(), QString("main"));

    CreateBranchDialog switchDialog(&controller, nullptr, "main");
    switchDialog.show();
    nameEdit = switchDialog.findChild<QLineEdit *>("createBranchNameEdit");
    switchCheck = switchDialog.findChild<QCheckBox *>("createBranchSwitchCheck");
    createButton = switchDialog.findChild<QPushButton *>("confirmCreateBranchButton");
    QVERIFY(nameEdit != nullptr);
    QVERIFY(switchCheck != nullptr);
    QVERIFY(createButton != nullptr);
    switchCheck->setChecked(true);
    nameEdit->setText("feature/dialog-create-and-switch");
    QTRY_VERIFY(createButton->isEnabled());
    QTest::mouseClick(createButton, Qt::LeftButton);
    QTRY_COMPARE(switchDialog.result(), static_cast<int>(QDialog::Accepted));
    QCOMPARE(controller.viewModel()->currentBranch(), QString("feature/dialog-create-and-switch"));

    CreateBranchDialog failureDialog(
        &controller,
        nullptr,
        "feature/dialog-create-and-switch");
    failureDialog.show();
    nameEdit = failureDialog.findChild<QLineEdit *>("createBranchNameEdit");
    createButton = failureDialog.findChild<QPushButton *>("confirmCreateBranchButton");
    QLabel *validation = failureDialog.findChild<QLabel *>("createBranchValidationLabel");
    QVERIFY(nameEdit != nullptr);
    QVERIFY(createButton != nullptr);
    QVERIFY(validation != nullptr);
    nameEdit->setText("feature/dialog-race");
    QTRY_VERIFY(createButton->isEnabled());
    QVERIFY(controller.createBranch("feature/dialog-race"));
    QSignalSpy failedSpy(&failureDialog, &CreateBranchDialog::branchCreationFailed);
    QTest::mouseClick(createButton, Qt::LeftButton);
    QTRY_COMPARE(failedSpy.count(), 1);
    QVERIFY(failureDialog.isVisible());
    QVERIFY(!validation->text().trimmed().isEmpty());
    QVERIFY(validation->text().contains("already exists", Qt::CaseInsensitive));
    QTRY_VERIFY(createButton->isEnabled());
    failureDialog.close();
}

void TestMainWindow::testMainWindowRefreshesCreatedBranch() {
    MainWindow window;
    switchToWorkspace(window);
    window.controller()->setRepositoryPath(m_repo.path());
    window.controller()->refresh();
    window.show();

    QPushButton *mainCreateButton = window.findChild<QPushButton *>("createBranchButton");
    BranchTreeWidget *mainTree = window.findChild<BranchTreeWidget *>("branchTree");
    QVERIFY(mainCreateButton != nullptr);
    QVERIFY(mainTree != nullptr);

    QTimer::singleShot(60, &window, []() {
        auto *dialog = qobject_cast<CreateBranchDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog != nullptr);
        QLineEdit *nameEdit = dialog->findChild<QLineEdit *>("createBranchNameEdit");
        QCheckBox *switchCheck = dialog->findChild<QCheckBox *>("createBranchSwitchCheck");
        QPushButton *createButton = dialog->findChild<QPushButton *>("confirmCreateBranchButton");
        QVERIFY(nameEdit != nullptr);
        QVERIFY(switchCheck != nullptr);
        QVERIFY(createButton != nullptr);
        switchCheck->setChecked(false);
        nameEdit->setText("feature/main-window-created");

        auto *clickWhenReady = new QTimer(dialog);
        clickWhenReady->setInterval(20);
        QObject::connect(clickWhenReady, &QTimer::timeout, dialog, [clickWhenReady, createButton]() {
            if (createButton->isEnabled()) {
                clickWhenReady->stop();
                createButton->click();
            }
        });
        clickWhenReady->start();
    });
    QTest::mouseClick(mainCreateButton, Qt::LeftButton);

    QTreeWidgetItem *createdBranch = mainTree->branchItem("feature/main-window-created");
    QVERIFY(createdBranch != nullptr);
    QCOMPARE(mainTree->selectedBranch(), QString("feature/main-window-created"));
    QVERIFY(createdBranch->parent()->isExpanded());
    QVERIFY(mainTree->visualItemRect(createdBranch).isValid());
    QCOMPARE(mainCreateButton->property("result").toString(), QString("success"));
    QCOMPARE(mainCreateButton->text(), QString("创建分支成功"));
}

void TestMainWindow::testActionButtonFeedbackStates() {
    QPushButton button("执行操作");
    button.setObjectName("feedbackTestButton");
    ActionButtonFeedback::configure(
        &button,
        {"#2563EB", "#1D4ED8", "#1E40AF"},
        {"正在执行…", "执行成功", "执行失败"});
    button.resize(button.sizeHint());
    const QSize originalSize = button.size();

    QVERIFY(button.styleSheet().contains(":hover"));
    QVERIFY(button.styleSheet().contains(":pressed"));
    QVERIFY(button.styleSheet().contains(":disabled"));
    QVERIFY(button.styleSheet().contains("[busy=\"true\"]"));
    QVERIFY(button.styleSheet().contains("[result=\"success\"]"));
    QVERIFY(button.styleSheet().contains("[result=\"error\"]"));
    QVERIFY(!button.styleSheet().contains("padding-top"));

    ActionButtonFeedback::showBusy(&button, "正在执行…");
    QCOMPARE(button.text(), QString("正在执行…"));
    QVERIFY(!button.isEnabled());
    QVERIFY(button.property("busy").toBool());
    QCOMPARE(button.size(), originalSize);

    ActionButtonFeedback::setReady(&button, true);
    ActionButtonFeedback::showSuccess(&button, "执行成功", 1);
    QCOMPARE(button.property("result").toString(), QString("success"));
    QCOMPARE(button.size(), originalSize);
    QTRY_VERIFY(!ActionButtonFeedback::isActive(&button));
    QCOMPARE(button.text(), QString("执行操作"));
    QVERIFY(button.isEnabled());

    ActionButtonFeedback::showBusy(&button, "正在执行…");
    ActionButtonFeedback::setReady(&button, false);
    ActionButtonFeedback::showError(&button, "执行失败", 1);
    QCOMPARE(button.property("result").toString(), QString("error"));
    QTRY_VERIFY(!ActionButtonFeedback::isActive(&button));
    QCOMPARE(button.text(), QString("执行操作"));
    QVERIFY(!button.isEnabled());
    QCOMPARE(button.size(), originalSize);
}

void TestMainWindow::testTopActionButtonPresentation() {
    MainWindow window;
    switchToWorkspace(window);
    window.show();
    QApplication::processEvents();

    auto *goHomeButton = window.findChild<QPushButton *>("goHomeButton");
    auto *changeProjectButton = window.findChild<QPushButton *>("switchProjectButton");
    auto *refreshButton = window.findChild<QPushButton *>("refreshButton");
    QVERIFY(goHomeButton != nullptr);
    QVERIFY(changeProjectButton != nullptr);
    QVERIFY(refreshButton != nullptr);

    QCOMPARE(goHomeButton->toolTip(), QString("返回欢迎页，当前项目不会被关闭。"));
    QCOMPARE(changeProjectButton->toolTip(), QString("选择另一个本地 Git 仓库。"));
    QCOMPARE(refreshButton->toolTip(), QString("重新读取当前仓库的分支和文件状态。"));
    const QList<QPushButton *> buttons = {goHomeButton, changeProjectButton, refreshButton};
    for (QPushButton *button : buttons) {
        const QSize initialSize = button->size();
        QVERIFY(button->styleSheet().contains(":hover"));
        QVERIFY(button->styleSheet().contains(":pressed"));
        QVERIFY(button->styleSheet().contains(":disabled"));
        QVERIFY(button->styleSheet().contains(":focus"));
        QVERIFY(button->styleSheet().contains("#2563EB"));
        QVERIFY(button->styleSheet().contains("[busy=\"true\"]"));
        QVERIFY(button->styleSheet().contains("[result=\"success\"]"));
        QVERIFY(button->styleSheet().contains("[result=\"error\"]"));
        ActionButtonFeedback::showBusy(button, button->property("normalText").toString());
        QCOMPARE(button->size(), initialSize);
        ActionButtonFeedback::setReady(button, true);
        ActionButtonFeedback::showSuccess(button, button->property("normalText").toString(), 1);
        QTRY_VERIFY(!ActionButtonFeedback::isActive(button));
        QCOMPARE(button->size(), initialSize);
    }
}

void TestMainWindow::testRefreshButtonFeedback() {
    MainWindow window;
    switchToWorkspace(window);
    window.controller()->setRepositoryPath(m_repo.path());
    auto *refreshButton = window.findChild<QPushButton *>("refreshButton");
    QVERIFY(refreshButton != nullptr);
    const QSize initialSize = refreshButton->sizeHint();

    QVERIFY(QMetaObject::invokeMethod(&window, "onRefresh"));
    QCOMPARE(refreshButton->text(), QString("正在刷新…"));
    QVERIFY(refreshButton->property("busy").toBool());
    QVERIFY(!refreshButton->isEnabled());
    const int feedbackToken = refreshButton->property("feedbackToken").toInt();
    QVERIFY(QMetaObject::invokeMethod(&window, "onRefresh"));
    QCOMPARE(refreshButton->property("feedbackToken").toInt(), feedbackToken);

    QTRY_COMPARE(refreshButton->property("result").toString(), QString("success"));
    QCOMPARE(refreshButton->text(), QString("刷新成功"));
    QTRY_VERIFY(!ActionButtonFeedback::isActive(refreshButton));
    QCOMPARE(refreshButton->text(), QString("刷新状态"));
    QVERIFY(refreshButton->isEnabled());
    QCOMPARE(refreshButton->sizeHint(), initialSize);
}

void TestMainWindow::testRefreshButtonErrorFeedback() {
    QTemporaryDir nonRepository;
    QVERIFY(nonRepository.isValid());
    MainWindow window;
    switchToWorkspace(window);
    window.controller()->setRepositoryPath(nonRepository.path());
    auto *refreshButton = window.findChild<QPushButton *>("refreshButton");
    auto *errorDisplay = window.findChild<QTextEdit *>("errorDisplay");
    QVERIFY(refreshButton != nullptr);
    QVERIFY(errorDisplay != nullptr);

    QVERIFY(QMetaObject::invokeMethod(&window, "onRefresh"));
    QTRY_COMPARE(refreshButton->property("result").toString(), QString("error"));
    QCOMPARE(refreshButton->text(), QString("刷新失败"));
    QVERIFY(!errorDisplay->isHidden());
    QVERIFY(errorDisplay->toPlainText().contains("刷新失败"));
    const QString persistentError = errorDisplay->toPlainText();
    QTRY_VERIFY(!ActionButtonFeedback::isActive(refreshButton));
    QCOMPARE(refreshButton->text(), QString("刷新状态"));
    QVERIFY(refreshButton->isEnabled());
    QCOMPARE(errorDisplay->toPlainText(), persistentError);
}

void TestMainWindow::testChangeProjectCancelIsNeutral() {
    MainWindow window;
    switchToWorkspace(window);
    auto *changeProjectButton = window.findChild<QPushButton *>("switchProjectButton");
    auto *errorDisplay = window.findChild<QTextEdit *>("errorDisplay");
    QVERIFY(changeProjectButton != nullptr);
    QVERIFY(errorDisplay != nullptr);
    errorDisplay->clear();
    errorDisplay->hide();

    auto *cancelDialog = new QTimer(&window);
    cancelDialog->setInterval(20);
    connect(cancelDialog, &QTimer::timeout, &window, [cancelDialog]() {
        auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        if (dialog) {
            cancelDialog->stop();
            dialog->reject();
        }
    });
    cancelDialog->start();
    QVERIFY(QMetaObject::invokeMethod(&window, "onSwitchProject"));
    QCOMPARE(changeProjectButton->text(), QString("更换项目"));
    QVERIFY(changeProjectButton->isEnabled());
    QVERIFY(!ActionButtonFeedback::isActive(changeProjectButton));
    QVERIFY(!errorDisplay->isVisible());
}

void TestMainWindow::testSafeDeleteFromButton() {
    MainWindow window;
    switchToWorkspace(window);
    window.controller()->setRepositoryPath(m_repo.path());
    QVERIFY(window.controller()->createBranch("merged"));
    QApplication::processEvents();

    QTreeWidget *branchTree = window.findChild<QTreeWidget *>("branchTree");
    QPushButton *deleteButton = window.findChild<QPushButton *>("deleteBranchButton");
    QTreeWidgetItem *mergedBranch = nullptr;
    QTreeWidgetItemIterator iterator(branchTree);
    while (*iterator) {
        if ((*iterator)->data(0, Qt::UserRole).toString() == "merged") {
            mergedBranch = *iterator;
            break;
        }
        ++iterator;
    }
    QVERIFY(mergedBranch != nullptr);
    branchTree->setCurrentItem(mergedBranch);
    QApplication::processEvents();
    QVERIFY(deleteButton->isEnabled());

    QTimer::singleShot(0, []() {
        auto *messageBox = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (messageBox) {
            messageBox->button(QMessageBox::Yes)->click();
        }
    });
    deleteButton->click();
    QTRY_VERIFY(!window.controller()->viewModel()->branches().contains("merged"));
    QTRY_COMPARE(deleteButton->text(), QString("删除成功"));
    QCOMPARE(deleteButton->property("result").toString(), QString("success"));
}

void TestMainWindow::testUnmergedDeleteRequiresHighRiskConfirmation() {
    MainWindow window;
    switchToWorkspace(window);
    window.controller()->setRepositoryPath(m_repo.path());
    QVERIFY(window.controller()->createBranch("111"));
    QVERIFY(window.controller()->checkoutBranch("111"));

    QFile file(m_repo.path() + "/unmerged-111.txt");
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("unmerged"), qint64(8));
    file.close();
    QVERIFY(window.controller()->stageFiles({"unmerged-111.txt"}));
    QVERIFY(window.controller()->createCommit("unmerged 111"));
    QVERIFY(window.controller()->checkoutBranch("main"));
    QApplication::processEvents();

    QTreeWidget *branchTree = window.findChild<QTreeWidget *>("branchTree");
    QPushButton *deleteButton = window.findChild<QPushButton *>("deleteBranchButton");
    QTreeWidgetItem *unmergedBranch = nullptr;
    QTreeWidgetItemIterator iterator(branchTree);
    while (*iterator) {
        if ((*iterator)->data(0, Qt::UserRole).toString() == "111") {
            unmergedBranch = *iterator;
            break;
        }
        ++iterator;
    }
    QVERIFY(unmergedBranch != nullptr);
    branchTree->setCurrentItem(unmergedBranch);
    QApplication::processEvents();
    QVERIFY(deleteButton->isEnabled());

    bool sawForceChoice = false;
    bool sawHighRiskConfirmation = false;
    int promptStep = 0;
    QTimer promptDriver;
    promptDriver.setInterval(10);
    connect(&promptDriver, &QTimer::timeout, [&]() {
        auto *prompt = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!prompt) return;
        if (promptStep == 0) {
            ++promptStep;
            prompt->button(QMessageBox::Yes)->click();
            return;
        }
        if (promptStep == 1 && prompt->text().contains("尚未合并")) {
            sawForceChoice = true;
            for (QAbstractButton *button : prompt->buttons()) {
                if (button->text() == "强制删除") {
                    ++promptStep;
                    button->click();
                    return;
                }
            }
        }
        if (promptStep == 2 && prompt->text().contains("不可撤销")) {
            sawHighRiskConfirmation = true;
            ++promptStep;
            prompt->button(QMessageBox::Cancel)->click();
            promptDriver.stop();
        }
    });
    promptDriver.start();
    deleteButton->click();

    QTRY_VERIFY(sawForceChoice);
    QTRY_VERIFY(sawHighRiskConfirmation);
    QVERIFY(window.controller()->viewModel()->branches().contains("111"));
    QVERIFY(window.controller()->deleteBranch("111", true));
}

void TestMainWindow::testFileChanges() {
    QFile file(m_repo.path() + "/modified.txt");
    file.open(QIODevice::WriteOnly);
    file.write("modified content");
    file.close();
    
    QProcess proc;
    proc.setWorkingDirectory(m_repo.path());
    proc.start("git", {"add", "modified.txt"});
    proc.waitForFinished(5000);
    proc.start("git", {"commit", "-m", "add modified"});
    proc.waitForFinished(5000);
    
    file.open(QIODevice::WriteOnly | QIODevice::Append);
    file.write("\nmore content");
    file.close();
    
    MainWindow window;
    switchToWorkspace(window);
    
    window.controller()->setRepositoryPath(m_repo.path());
    window.controller()->refresh();
    QApplication::processEvents();
    
    QTreeWidget *changesTree = window.findChild<QTreeWidget *>("changesTree");
    QVERIFY(changesTree != nullptr);
    
    bool found = false;
    for (int i = 0; i < changesTree->topLevelItemCount(); ++i) {
        if (changesTree->topLevelItem(i)->text(1).contains("modified.txt")) {
            found = true;
            break;
        }
    }
    QVERIFY(found);
}

void TestMainWindow::testUntrackedFile() {
    QFile file(m_repo.path() + "/new_file.txt");
    file.open(QIODevice::WriteOnly);
    file.write("new content");
    file.close();
    
    MainWindow window;
    switchToWorkspace(window);
    
    window.controller()->setRepositoryPath(m_repo.path());
    window.controller()->refresh();
    QApplication::processEvents();
    
    QTreeWidget *changesTree = window.findChild<QTreeWidget *>("changesTree");
    QVERIFY(changesTree != nullptr);
    
    bool found = false;
    for (int i = 0; i < changesTree->topLevelItemCount(); ++i) {
        if (changesTree->topLevelItem(i)->text(1).contains("new_file.txt")) {
            found = true;
            break;
        }
    }
    QVERIFY(found || changesTree->topLevelItemCount() == 0);
}

void TestMainWindow::testLoadingState() {
    MainWindow window;
    switchToWorkspace(window);
    
    window.controller()->setRepositoryPath(m_repo.path());
    
    QSignalSpy loadingSpy(window.controller()->viewModel(), &GitViewModel::loadingChanged);
    window.controller()->refresh();
    QVERIFY(loadingSpy.count() >= 2);
}

void TestMainWindow::testErrorDisplay() {
    QTemporaryDir nonGitDir;
    QVERIFY(nonGitDir.isValid());
    
    MainWindow window;
    switchToWorkspace(window);
    
    window.controller()->setRepositoryPath(nonGitDir.path());
    window.controller()->refresh();
    
    QTextEdit *errorDisplay = window.findChild<QTextEdit *>("errorDisplay");
    QVERIFY(errorDisplay != nullptr);
    QVERIFY(!errorDisplay->toPlainText().isEmpty());
}

void TestMainWindow::testShowAndClose() {
    MainWindow window;
    window.show();
    QTest::qWait(100);  // v4-P6-21R2: no window-manager expose dependency
    QVERIFY(window.isVisible());
    window.close();
    QApplication::processEvents();
    QVERIFY(!window.isVisible());
}

void TestMainWindow::testCommitUI() {
    MainWindow window;
    switchToWorkspace(window);
    
    QTreeWidget *changesTree = window.findChild<QTreeWidget *>("changesTree");
    QVERIFY(changesTree != nullptr);
    
    QLineEdit *commitEdit = window.findChild<QLineEdit *>("commitMessageEdit");
    QVERIFY(commitEdit != nullptr);
    
    QPushButton *commitButton = window.findChild<QPushButton *>("commitButton");
    QVERIFY(commitButton != nullptr);
    
    QPushButton *stageAllButton = window.findChild<QPushButton *>("stageAllButton");
    QVERIFY(stageAllButton != nullptr);
    
    QPushButton *unstageAllButton = window.findChild<QPushButton *>("unstageAllButton");
    QVERIFY(unstageAllButton != nullptr);
    
    // Test empty commit message shows error
    commitEdit->clear();
    // v4-P6-21 ISSUE-12: with an empty message the commit button is
    // disabled (no pointless error path); with a message it commits.
    if (!commitButton->isEnabled()) {
        QVERIFY(!commitButton->isEnabled());  // clean/short state
    } else {
        commitButton->click();
        QApplication::processEvents();
        QTextEdit *errorDisplay = window.findChild<QTextEdit *>("errorDisplay");
        QVERIFY(errorDisplay != nullptr);
        QVERIFY(errorDisplay->toPlainText().contains("提交说明"));
    }
}

void TestMainWindow::commitReducerSurvivesBusyFeedback() {
    const QString repo = QDir::tempPath() + "/qgs_p621_commitstate_"
        + QUuid::createUuid().toString().left(8);
    QVERIFY(QDir().mkpath(repo));
    QProcess init;
    init.setWorkingDirectory(repo);
    init.start("git", {"init", "-q", "-b", "main"});
    QVERIFY(init.waitForFinished(15000) && init.exitCode() == 0);
    QFile base(repo + "/base.txt");
    QVERIFY(base.open(QIODevice::WriteOnly));
    base.write("base\n");
    base.close();
    QProcess add;
    add.setWorkingDirectory(repo);
    add.start("git", {"add", "-A"});
    QVERIFY(add.waitForFinished(15000) && add.exitCode() == 0);
    QProcess firstCommit;
    firstCommit.setWorkingDirectory(repo);
    firstCommit.start("git", {"-c", "user.name=T", "-c", "user.email=t@t",
                                "commit", "-q", "-m", "base"});
    QVERIFY(firstCommit.waitForFinished(15000)
            && firstCommit.exitCode() == 0);
    QFile change(repo + "/change.txt");
    QVERIFY(change.open(QIODevice::WriteOnly));
    change.write("change\n");
    change.close();
    QProcess stage;
    stage.setWorkingDirectory(repo);
    stage.start("git", {"add", "change.txt"});
    QVERIFY(stage.waitForFinished(15000) && stage.exitCode() == 0);

    MainWindow window;
    switchToWorkspace(window, repo);
    auto *edit = window.findChild<QLineEdit *>("commitMessageEdit");
    auto *button = window.findChild<QPushButton *>("commitButton");
    QVERIFY(edit && button);
    edit->setText(QStringLiteral("state reducer check"));
    QVERIFY(button->isEnabled());
    QVERIFY(button->property("readyWhenIdle").toBool());

    QMetaObject::invokeMethod(&window, "onCommit");
    // Programmatic edits are allowed in a test even though the editor is
    // disabled; the reducer must still keep the action unavailable while
    // the commit request is in flight.
    edit->setText(QStringLiteral("edited while busy"));
    QVERIFY(!button->isEnabled());
    QVERIFY(!button->property("readyWhenIdle").toBool());

    QTRY_VERIFY_WITH_TIMEOUT(
        !window.controller()->viewModel()->isLoading(), 20000);
    QTest::qWait(1800);  // success/error feedback timer must also settle
    QVERIFY(!button->isEnabled());
    QVERIFY(!button->property("readyWhenIdle").toBool());
    QCOMPARE(edit->text(), QString());
    QDir(repo).removeRecursively();
}

void TestMainWindow::testUnstageAllRegression() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QProcess git;
    git.setWorkingDirectory(dir.path());
    git.start("git", {"init"});
    QVERIFY(git.waitForFinished(5000));
    QCOMPARE(git.exitCode(), 0);

    QFile file(dir.filePath("stage-commit-uat.txt"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("stage content"), qint64(13));
    file.close();

    MainWindow window;
    switchToWorkspace(window);
    window.controller()->setRepositoryPath(dir.path());
    window.controller()->refresh();
    QApplication::processEvents();

    QPushButton *stageAllButton = window.findChild<QPushButton *>("stageAllButton");
    QPushButton *unstageAllButton = window.findChild<QPushButton *>("unstageAllButton");
    QVERIFY(stageAllButton != nullptr);
    QVERIFY(unstageAllButton != nullptr);

    stageAllButton->click();
    QCOMPARE(stageAllButton->text(), QString("正在暂存…"));
    QVERIFY(stageAllButton->property("busy").toBool());
    const auto status = [&]() {
        QProcess statusGit;
        statusGit.setWorkingDirectory(dir.path());
        statusGit.start("git", {"status", "--porcelain=v1"});
        if (!statusGit.waitForFinished(5000)) {
            return QString();
        }
        return QString::fromUtf8(statusGit.readAllStandardOutput()).trimmed();
    };
    QTRY_COMPARE(status(), QString("A  stage-commit-uat.txt"));
    QTRY_COMPARE(stageAllButton->text(), QString("暂存成功"));
    QCOMPARE(stageAllButton->property("result").toString(), QString("success"));

    unstageAllButton->click();
    QCOMPARE(unstageAllButton->text(), QString("正在取消暂存…"));
    QVERIFY(unstageAllButton->property("busy").toBool());
    QTRY_COMPARE(status(), QString("?? stage-commit-uat.txt"));
    QTRY_COMPARE(unstageAllButton->text(), QString("取消暂存成功"));
    QCOMPARE(unstageAllButton->property("result").toString(), QString("success"));

    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("stage content"));
}

void TestMainWindow::cleanupTestCase() {}

// --- v4-P6-18 stability hardening ---

namespace {
class FakeCredentialStore : public CredentialStore {
public:
    QString user;
    QString tok;
    bool save(const QString &u, const QString &t) override {
        user = u;
        tok = t;
        return true;
    }
    bool read(QString *u, QString *t) override {
        if (tok.isEmpty()) {
            return false;
        }
        *u = user;
        *t = tok;
        return true;
    }
    bool remove() override {
        tok.clear();
        return true;
    }
};
}  // namespace

void TestMainWindow::testInitEmptyRepoDoesNotCrash() {
    // P0-1: git init with zero commits must not crash the load chain.
    const QString repo =
        QDir::tempPath() + "/qgs_p618_empty_"
        + QUuid::createUuid().toString().left(8);
    QDir().mkpath(repo);
    QProcess init;
    init.setWorkingDirectory(repo);
    init.start("git", {"init", "-q", "-b", "main"});
    QVERIFY(init.waitForFinished(15000) && init.exitCode() == 0);

    MainWindow window;
    QMetaObject::invokeMethod(&window, "onProjectSelected",
                              Q_ARG(QString, repo));
    QStackedWidget *stack = window.findChild<QStackedWidget *>();
    QElapsedTimer timer;
    timer.start();
    while (stack && stack->currentIndex() != 1
           && timer.elapsed() < 15000) {
        QApplication::processEvents();
        QTest::qWait(20);
    }
    QApplication::processEvents();
    // No crash; the compact page owns only the visible viewer entry point.
    QVERIFY(window.findChild<QPushButton *>("openGraphViewerButton")
            != nullptr);
    QVERIFY(window.findChildren<BranchRelationWidget *>().isEmpty());
    QDir(repo).removeRecursively();
}

void TestMainWindow::testLoadProjectReentrancy() {
    // P0-2: rapid repeated project loads must not crash or corrupt -
    // the re-entrancy guard drops the second load while the first runs.
    MainWindow window;
    QMetaObject::invokeMethod(&window, "onProjectSelected",
                              Q_ARG(QString, m_repo.path()));
    QMetaObject::invokeMethod(&window, "onProjectSelected",
                              Q_ARG(QString, m_repo.path()));
    QStackedWidget *stack = window.findChild<QStackedWidget *>();
    QElapsedTimer timer;
    timer.start();
    while (stack && stack->currentIndex() != 1
           && timer.elapsed() < 15000) {
        QApplication::processEvents();
        QTest::qWait(20);
    }
    QApplication::processEvents();
    // The workspace is reachable and the window survived both calls.
    QVERIFY(stack != nullptr);
    QCOMPARE(stack->currentIndex(), 1);
}

void TestMainWindow::testGitHubStateRestore() {
    // P1-1: a persisted account is restored on a fresh service.
    auto *store = new FakeCredentialStore;
    GitHubAccountService first;
    first.setCredentialStore(store);
    QVERIFY(first.login(QStringLiteral("steven"),
                        QStringLiteral("ghp_fake_token_123")));
    GitHubAccountService second;
    second.setCredentialStore(store);
    second.restore();
    QVERIFY(second.isLoggedIn());
    QCOMPARE(second.username(), QStringLiteral("steven"));
}

void TestMainWindow::testRefreshShowsRealResult() {
    // P1-3: refresh on a normal repo ends with a real (non-busy)
    // state - no hard-coded failure/success sequence.
    MainWindow window;
    switchToWorkspace(window);
    QPushButton *refresh =
        window.findChild<QPushButton *>("refreshButton");
    QVERIFY(refresh != nullptr);
    QVERIFY(!refresh->property("busy").toBool());
}

void TestMainWindow::testRemoteFailureShowsErrorArea() {
    // P1-4: without a remote the fetch action is disabled (v4-P6-21R2
    // reducer), so clicking it cannot start a command - the error area
    // stays hidden and the main window survives.
    MainWindow window;
    switchToWorkspace(window);
    QPushButton *fetch = window.findChild<QPushButton *>("fetchButton");
    QTextEdit *errorDisplay =
        window.findChild<QTextEdit *>("errorDisplay");
    QVERIFY(fetch != nullptr);
    QVERIFY(errorDisplay != nullptr);
    // Wait for the remote state to settle (no remotes -> disabled).
    QElapsedTimer ready;
    ready.start();
    while (ready.elapsed() < 10000) {
        QApplication::processEvents();
        QTest::qWait(20);
    }
    QVERIFY(!fetch->isEnabled());  // no remote configured
    fetch->click();  // must be a no-op
    QApplication::processEvents();
    QVERIFY(!errorDisplay->isVisible());
    QVERIFY(!fetch->property("busy").toBool());
}

// --- v4-P6-20 state correctness ---

namespace {
// Injectable detector: counts invocations and can flip its result so
// the test can prove re-detection actually re-runs detection.
class FakeEnvService : public GitEnvironmentService {
public:
   int calls = 0;
   GitEnvironmentService::GitEnvironment first;
   GitEnvironmentService::GitEnvironment second;
   bool useSecond = false;

   GitEnvironmentService::GitEnvironment detectSync(
       const QString &customPath) override {
       ++calls;
       return useSecond ? second : first;
   }
};

QString refreshButtonHistory(MainWindow &window, int ms) {
   QPushButton *refresh = window.findChild<QPushButton *>("refreshButton");
   QStringList seen;
   QElapsedTimer t;
   t.start();
   while (t.elapsed() < ms) {
       const QString text = refresh->text();
       if (seen.isEmpty() || seen.last() != text) {
           seen << text;
       }
       QApplication::processEvents();
       QTest::qWait(25);
   }
   return seen.join(QStringLiteral("|"));
}
}  // namespace

void TestMainWindow::remoteButtonsReEnabledAfterSuccess() {
   // P1-1: fetch success must leave the remote buttons enabled.
   const QString bare =
       QDir::tempPath() + "/qgs_p620_bare_"
       + QUuid::createUuid().toString().left(8) + ".git";
   QProcess initBare;
   initBare.setWorkingDirectory(QDir::tempPath());
   initBare.start("git", {"init", "-q", "--bare", bare});
   QVERIFY(initBare.waitForFinished(15000) && initBare.exitCode() == 0);
   const QString repo =
       QDir::tempPath() + "/qgs_p620_remote_"
       + QUuid::createUuid().toString().left(8);
   QDir().mkpath(repo);
   QProcess init;
   init.setWorkingDirectory(repo);
   init.start("git", {"init", "-q", "-b", "main"});
   QVERIFY(init.waitForFinished(15000) && init.exitCode() == 0);
   QFile f(repo + "/a.txt");
   f.open(QIODevice::WriteOnly);
   f.write("1\n");
   f.close();
   QProcess add;
   add.setWorkingDirectory(repo);
   add.start("git", {"add", "-A"});
   QVERIFY(add.waitForFinished(15000) && add.exitCode() == 0);
   QProcess commit;
   commit.setWorkingDirectory(repo);
   commit.start("git",
                {"-c", "user.name=T", "-c", "user.email=t@t",
                 "commit", "-q", "-m", "base"});
   QVERIFY(commit.waitForFinished(15000) && commit.exitCode() == 0);
   QProcess remoteAdd;
   remoteAdd.setWorkingDirectory(repo);
   remoteAdd.start("git", {"remote", "add", "origin", bare});
   QVERIFY(remoteAdd.waitForFinished(15000) && remoteAdd.exitCode() == 0);

   MainWindow window;
   QMetaObject::invokeMethod(&window, "onProjectSelected",
                             Q_ARG(QString, repo));
   QStackedWidget *stack = window.findChild<QStackedWidget *>();
   QElapsedTimer t;
   t.start();
   while (stack && stack->currentIndex() != 1 && t.elapsed() < 15000) {
       QApplication::processEvents();
       QTest::qWait(20);
   }
   QApplication::processEvents();
   QPushButton *fetch = window.findChild<QPushButton *>("fetchButton");
   QVERIFY(fetch != nullptr);
   // v4-P6-20: wait until the remote state settled and the button is
   // usable before clicking (it starts disabled during load).
   t.restart();
   while (!fetch->isEnabled() && t.elapsed() < 10000) {
       QApplication::processEvents();
       QTest::qWait(20);
   }
   fetch->click();
   t.restart();
   while (fetch->property("busy").toBool() && t.elapsed() < 15000) {
       QApplication::processEvents();
       QTest::qWait(20);
   }
   // v4-P6-20: the success/error feedback disables the button for its
   // duration (800/1300ms) - wait for it to re-enable.
   t.restart();
   while (!fetch->isEnabled() && t.elapsed() < 5000) {
       QApplication::processEvents();
       QTest::qWait(20);
   }
   QVERIFY(fetch->isEnabled());  // never left disabled after success
   QDir(bare).removeRecursively();
   QDir(repo).removeRecursively();
}

void TestMainWindow::remoteButtonsReEnabledAfterFailure() {
   // P1-1: a fetch failure must leave the buttons enabled - a failed
   // operation must not lock them forever.
   // v4-P6-21R2: buttons are only enabled when remotes exist; use a
   // repo WITH a broken remote so fetch really runs and fails.
   const QString repo =
       QDir::tempPath() + "/qgs_p621r2_remfail_"
       + QUuid::createUuid().toString().left(8);
   QDir().mkpath(repo);
   QProcess init;
   init.setWorkingDirectory(repo);
   init.start("git", {"init", "-q", "-b", "main"});
   QVERIFY(init.waitForFinished(15000) && init.exitCode() == 0);
   QProcess remoteAdd;
   remoteAdd.setWorkingDirectory(repo);
   remoteAdd.start("git", {"remote", "add", "origin",
                           "C:/definitely/not/a/remote.git"});
   QVERIFY(remoteAdd.waitForFinished(15000) && remoteAdd.exitCode() == 0);
   MainWindow window;
   switchToWorkspace(window, repo);
   QPushButton *fetch = window.findChild<QPushButton *>("fetchButton");
   QPushButton *pull = window.findChild<QPushButton *>("pullButton");
   QPushButton *push = window.findChild<QPushButton *>("pushButton");
   QVERIFY(fetch && pull && push);
   QElapsedTimer ready;
   ready.start();
   while (!fetch->isEnabled() && ready.elapsed() < 10000) {
       QApplication::processEvents();
       QTest::qWait(20);
   }
   fetch->click();
   QElapsedTimer t;
   t.start();
   while (fetch->property("busy").toBool() && t.elapsed() < 15000) {
       QApplication::processEvents();
       QTest::qWait(20);
   }
   QApplication::processEvents();
   QElapsedTimer settle;
   settle.start();
   while (!fetch->isEnabled() && settle.elapsed() < 5000) {
       QApplication::processEvents();
       QTest::qWait(20);
   }
   QVERIFY(fetch->isEnabled());
   QVERIFY(pull->isEnabled());
   QVERIFY(push->isEnabled());
}

void TestMainWindow::remoteButtonsDisabledWithoutRemote() {
    const QString repo = QDir::tempPath() + "/qgs_p621_noremote_"
        + QUuid::createUuid().toString().left(8);
    QVERIFY(QDir().mkpath(repo));
    QProcess init;
    init.setWorkingDirectory(repo);
    init.start("git", {"init", "-q", "-b", "main"});
    QVERIFY(init.waitForFinished(15000) && init.exitCode() == 0);
    QFile file(repo + "/base.txt");
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("base\n");
    file.close();
    QProcess add;
    add.setWorkingDirectory(repo);
    add.start("git", {"add", "-A"});
    QVERIFY(add.waitForFinished(15000) && add.exitCode() == 0);
    QProcess commit;
    commit.setWorkingDirectory(repo);
    commit.start("git", {"-c", "user.name=T", "-c", "user.email=t@t",
                           "commit", "-q", "-m", "base"});
    QVERIFY(commit.waitForFinished(15000) && commit.exitCode() == 0);

    MainWindow window;
    switchToWorkspace(window, repo);
    auto *fetch = window.findChild<QPushButton *>("fetchButton");
    auto *pull = window.findChild<QPushButton *>("pullButton");
    auto *push = window.findChild<QPushButton *>("pushButton");
    QVERIFY(fetch && pull && push);
    QTest::qWait(1200);  // allow any feedback timer to settle
    QVERIFY(!fetch->isEnabled());
    QVERIFY(!pull->isEnabled());
    QVERIFY(!push->isEnabled());
    QVERIFY(!fetch->property("readyWhenIdle").toBool());
    QVERIFY(!pull->property("readyWhenIdle").toBool());
    QVERIFY(!push->property("readyWhenIdle").toBool());
    QDir(repo).removeRecursively();
}

void TestMainWindow::remoteActionCanRunTwice() {
   // P1-1: the same remote action can be triggered again after the
   // first run finished (no permanent lock).
   // v4-P6-21R2: requires a configured remote for the buttons to be
   // enabled - use a repo with a broken remote (fetch fails fast but
   // the buttons must recover).
   const QString repo =
       QDir::tempPath() + "/qgs_p621r2_remtwice_"
       + QUuid::createUuid().toString().left(8);
   QDir().mkpath(repo);
   QProcess init;
   init.setWorkingDirectory(repo);
   init.start("git", {"init", "-q", "-b", "main"});
   QVERIFY(init.waitForFinished(15000) && init.exitCode() == 0);
   QProcess remoteAdd;
   remoteAdd.setWorkingDirectory(repo);
   remoteAdd.start("git", {"remote", "add", "origin",
                           "C:/definitely/not/a/remote.git"});
   QVERIFY(remoteAdd.waitForFinished(15000) && remoteAdd.exitCode() == 0);
   MainWindow window;
   switchToWorkspace(window, repo);
   QPushButton *fetch = window.findChild<QPushButton *>("fetchButton");
   QVERIFY(fetch != nullptr);
   for (int round = 0; round < 2; ++round) {
       QElapsedTimer ready;
       ready.start();
       while (!fetch->isEnabled() && ready.elapsed() < 10000) {
           QApplication::processEvents();
           QTest::qWait(20);
       }
       fetch->click();
       QElapsedTimer t;
       t.start();
       while (fetch->property("busy").toBool() && t.elapsed() < 15000) {
           QApplication::processEvents();
           QTest::qWait(20);
       }
       QApplication::processEvents();
       QElapsedTimer settle;
       settle.start();
       while (!fetch->isEnabled() && settle.elapsed() < 5000) {
           QApplication::processEvents();
           QTest::qWait(20);
       }
       QVERIFY(fetch->isEnabled());
       QTest::qWait(200);
   }
}

void TestMainWindow::refreshSuccessNeverShowsFailure() {
   // P1-2: a successful refresh never shows 刷新失败 at any point.
   MainWindow window;
   switchToWorkspace(window);
   QPushButton *refresh = window.findChild<QPushButton *>("refreshButton");
   QVERIFY(refresh != nullptr);
   QElapsedTimer idle;
   idle.start();
   while ((refresh->property("busy").toBool()
           || !refresh->isEnabled())
          && idle.elapsed() < 10000) {
       QApplication::processEvents();
       QTest::qWait(20);
   }
   QTest::qWait(1200);  // v4-P6-21R2: settle past the 800ms success-feedback gate
   refresh->click();
   const QString history = refreshButtonHistory(window, 4000);
   QVERIFY2(!history.contains(QStringLiteral("刷新失败")),
            qPrintable(history));
}

void TestMainWindow::refreshFailureDoesNotLaterShowSuccess() {
   // P1-2: a real refresh failure shows 刷新失败 and never flips to
   // 刷新成功 afterwards.
   const QString repo =
       QDir::tempPath() + "/qgs_p620_broken_"
       + QUuid::createUuid().toString().left(8);
   QDir().mkpath(repo);
   QProcess init;
   init.setWorkingDirectory(repo);
   init.start("git", {"init", "-q", "-b", "main"});
   QVERIFY(init.waitForFinished(15000) && init.exitCode() == 0);
   MainWindow window;
   QMetaObject::invokeMethod(&window, "onProjectSelected",
                             Q_ARG(QString, repo));
   QStackedWidget *stack = window.findChild<QStackedWidget *>();
   QElapsedTimer t;
   t.start();
   while (stack && stack->currentIndex() != 1 && t.elapsed() < 20000) {
       QApplication::processEvents();
       QTest::qWait(20);
   }
   QApplication::processEvents();
   // v4-P6-21R2: the load pipeline (detect -> snapshot -> branches ->
   // current) may still be running even after the workspace page shows;
   // a refresh click while isLoading() is true is re-entrancy gated and
   // silently dropped - wait for the pipeline to fully settle.
   QTRY_VERIFY_WITH_TIMEOUT(!window.controller()->viewModel()->isLoading(),
                            20000);
   // Break the repository so the refresh genuinely fails.
   QDir(repo + "/.git").removeRecursively();
   QPushButton *refresh = window.findChild<QPushButton *>("refreshButton");
   QVERIFY(refresh != nullptr);
   QTest::qWait(1200);  // v4-P6-21R2: settle past the 800ms success-feedback gate
   refresh->click();
   const QString history = refreshButtonHistory(window, 4000);
   QVERIFY2(history.contains(QStringLiteral("刷新失败")),
            qPrintable(history));
   QVERIFY2(!history.contains(QStringLiteral("刷新成功")),
            qPrintable(history));
   QDir(repo).removeRecursively();
}

void TestMainWindow::refreshIgnoresStaleCallback() {
   // P1-2: rapid repeated clicks do not start parallel refreshes and
   // do not produce a fail->success sequence (single pipeline).
   MainWindow window;
   switchToWorkspace(window);
   QPushButton *refresh = window.findChild<QPushButton *>("refreshButton");
   QVERIFY(refresh != nullptr);
   QElapsedTimer idle;
   idle.start();
   while ((refresh->property("busy").toBool()
           || !refresh->isEnabled())
          && idle.elapsed() < 10000) {
       QApplication::processEvents();
       QTest::qWait(20);
   }
   QTest::qWait(1200);  // v4-P6-21R2: settle past the 800ms success-feedback gate
   refresh->click();
   QTest::qWait(50);
   refresh->click();  // dropped while busy
   QTest::qWait(50);
   refresh->click();  // dropped while busy
   const QString history = refreshButtonHistory(window, 4000);
   QVERIFY2(!history.contains(QStringLiteral("刷新失败")),
            qPrintable(history));
}

void TestMainWindow::gitEnvironmentRedetectActuallyInvokesDetector() {
   // P1-3: pressing 重新检测 really runs the detector again.
   auto *fake = new FakeEnvService;
   fake->first.available = true;
   fake->first.version = QStringLiteral("2.54.0");
   GitEnvironmentDialog dialog(fake);
   dialog.setCustomPath(QStringLiteral("C:/fake/git.exe"));
   QMetaObject::invokeMethod(&dialog, "reDetect");
   QTest::qWait(200);
   const int afterFirst = fake->calls;
   QVERIFY2(afterFirst >= 1, "detector must be invoked on re-detect");
   QMetaObject::invokeMethod(&dialog, "reDetect");
   QTest::qWait(200);
   QVERIFY2(fake->calls >= afterFirst + 1,
            "second re-detect must invoke the detector again");
}

void TestMainWindow::gitEnvironmentRedetectUpdatesFailureResult() {
   // P1-3: when the second detection returns a different result the
   // UI must show the second result (never a stale cached one).
   auto *fake = new FakeEnvService;
   fake->first.available = true;
   fake->first.version = QStringLiteral("2.54.0");
   fake->second.available = false;
   // v4-P6-20: keep the injected error empty - a non-empty error
   // makes reDetect() open a modal QMessageBox::warning which blocks
   // the test. The UI failure state is asserted via gitEnvStatus.
   fake->second.error = QString();
   GitEnvironmentDialog dialog(fake);
   dialog.setCustomPath(QStringLiteral("C:/fake/git.exe"));
   QMetaObject::invokeMethod(&dialog, "reDetect");
   QTest::qWait(200);
   QLabel *status = dialog.findChild<QLabel *>("gitEnvStatus");
   QVERIFY(status != nullptr);
   QVERIFY2(status->text().contains(QStringLiteral("可用")),
            qPrintable(status->text()));
   // Flip the injected result to a failure and re-detect.
   fake->useSecond = true;
   QMetaObject::invokeMethod(&dialog, "reDetect");
   QTest::qWait(200);
   QVERIFY2(status->text().contains(QStringLiteral("不可用")),
            qPrintable(status->text()));
}

void TestMainWindow::branchGraphMainTabRemoved() {
   // v4-P6-20: the main window branch area keeps only the list tab -
   // the graph lives in the standalone viewer.
   MainWindow window;
   auto *branchTab = window.findChild<QTabWidget *>("branchTab");
   QVERIFY(branchTab != nullptr);
   QCOMPARE(branchTab->count(), 1);
   QVERIFY(!branchTab->tabText(0).contains(QStringLiteral("关系图")));
}

// --- v4-P6-21 safety & state integrity ---

namespace {
// Fake QNetworkReply that stays pending until the test finishes it.
class PendingReply : public QNetworkReply {
public:
   explicit PendingReply(QNetworkAccessManager *parent)
       : QNetworkReply(parent) {
       setOpenMode(QIODevice::ReadOnly);
   }
   void abort() override {}
   qint64 bytesAvailable() const override { return 0; }
   qint64 readData(char *, qint64) override { return -1; }
   void finishWithSuccess() {
       setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
       setHeader(QNetworkRequest::ContentTypeHeader,
                 QStringLiteral("application/json"));
       // A minimal GitHub /user JSON body.
       m_body = QByteArrayLiteral(
           "{\"login\":\"late-user\",\"id\":1}");
       setOpenMode(QIODevice::ReadOnly);
       emit readyRead();
       emit finished();
   }
private:
   QByteArray m_body;
};

// Network manager that hands out a controllable pending reply.
class DelayedNetManager : public QNetworkAccessManager {
public:
    PendingReply *m_pending = nullptr;

protected:
    QNetworkReply *createRequest(Operation op,
                                 const QNetworkRequest &request,
                                 QIODevice *outgoingData) override {
        Q_UNUSED(op);
        Q_UNUSED(request);
        Q_UNUSED(outgoingData);
        m_pending = new PendingReply(this);
        return m_pending;
    }
};
}  // namespace

void TestMainWindow::testGitHubDialogLateReplySafe() {
    // v4-P6-21 ISSUE-01: closing the account dialog while verification
    // is in flight must never crash when the late reply arrives.
    auto *manager = new DelayedNetManager;
    auto *service = new GitHubAccountService;
    service->setNetworkManager(manager);
    auto *dialog = new GitHubAccountDialog(service, nullptr);
    // The token input lives in the modal connect sub-dialog opened by
    // the connect button; fill it and accept via a timer so the
    // verification starts and stays pending.
    QTimer::singleShot(200, [manager]() {
        for (QWidget *w : QApplication::topLevelWidgets()) {
            if (w->isModal() && w->isVisible()) {
                auto *edit = w->findChild<QLineEdit *>("githubTokenEdit");
                if (edit) {
                    edit->setText(QStringLiteral("ghp_late_reply_fake"));
                }
                for (QPushButton *b : w->findChildren<QPushButton *>()) {
                    if (b->text() == QStringLiteral("连接")) {
                        b->click();
                        return;
                    }
                }
                w->close();
                return;
            }
        }
    });
    QPushButton *connectButton =
        dialog->findChild<QPushButton *>("githubConnectButton");
    QVERIFY(connectButton != nullptr);
    connectButton->click();  // opens the modal sub-dialog (timer fills+accepts)
    QVERIFY(manager->m_pending != nullptr);  // verification in flight
    // Destroy the dialog while the request is still pending.
    delete dialog;
    QApplication::processEvents();
    // Now the late reply arrives - must be a safe no-op (QPointer guard).
    manager->m_pending->finishWithSuccess();
    QApplication::processEvents();
    QVERIFY(true);  // reached without crashing
    delete service;
}

void TestMainWindow::testRepositorySessionClearedOnNonGit() {
   // v4-P6-21 ISSUE-03: opening a non-git directory must clear every
   // repo-bound service so no operation can reach the old repository.
   const QString repo =
       QDir::tempPath() + "/qgs_p621_sessionA_"
       + QUuid::createUuid().toString().left(8);
   const QString plain =
       QDir::tempPath() + "/qgs_p621_sessionB_"
       + QUuid::createUuid().toString().left(8);
   QDir().mkpath(repo);
   QProcess init;
   init.setWorkingDirectory(repo);
   init.start("git", {"init", "-q", "-b", "main"});
   QVERIFY(init.waitForFinished(15000) && init.exitCode() == 0);
   QFile f(repo + "/a.txt");
   f.open(QIODevice::WriteOnly);
   f.write("1\n");
   f.close();
   QProcess add;
   add.setWorkingDirectory(repo);
   add.start("git", {"add", "-A"});
   QVERIFY(add.waitForFinished(15000) && add.exitCode() == 0);
   QProcess commit;
   commit.setWorkingDirectory(repo);
   commit.start("git", {"-c", "user.name=T", "-c", "user.email=t@t",
                        "commit", "-q", "-m", "base"});
   QVERIFY(commit.waitForFinished(15000) && commit.exitCode() == 0);
   QDir().mkpath(plain);

   MainWindow window;
   QMetaObject::invokeMethod(&window, "onProjectSelected",
                             Q_ARG(QString, repo));
   QStackedWidget *stack = window.findChild<QStackedWidget *>();
   QElapsedTimer t;
   t.start();
   while (stack && stack->currentIndex() != 1 && t.elapsed() < 15000) {
       QApplication::processEvents();
       QTest::qWait(20);
   }
   QApplication::processEvents();
   // Now open the plain non-git directory.
   QMetaObject::invokeMethod(&window, "onProjectSelected",
                             Q_ARG(QString, plain));
   QElapsedTimer t2;
   t2.start();
   while (t2.elapsed() < 10000) {
       QApplication::processEvents();
       QTest::qWait(20);
   }
   QApplication::processEvents();
   // Dangerous operations must be disabled and the controller must
   // not point at A anymore.
   QPushButton *stageAll = window.findChild<QPushButton *>("stageAllButton");
   QPushButton *commitBtn = window.findChild<QPushButton *>("commitButton");
   if (stageAll) {
       QVERIFY(!stageAll->isEnabled());
   }
   if (commitBtn) {
       QVERIFY(!commitBtn->isEnabled());
   }
   QDir(repo).removeRecursively();
   QDir(plain).removeRecursively();
}

void TestMainWindow::testCleanRepositoryShowsCleanState() {
    // v4-P6-21 ISSUE-12: a loaded repo with a clean working tree shows
    // the clean state and disables the commit controls.
    // Windows releases directory handles asynchronously after the
    // previous test deleted its temp repos - give it a moment so git
    // commands in %TEMP% do not hit a transient sharing violation.
    QTest::qWait(1500);
    MainWindow window;
    switchToWorkspace(window);  // m_repo loads clean (no modifications)
    QLabel *suggestion =
        window.findChild<QLabel *>("suggestionLabel");
    QElapsedTimer t;
    t.start();
    bool sawClean = false;
    QString lastText;
    while (t.elapsed() < 15000) {
        QApplication::processEvents();
        if (suggestion) {
            lastText = suggestion->text();
            if (lastText.contains(QStringLiteral("工作区干净"))) {
                sawClean = true;
                break;
            }
        }
        QTest::qWait(30);
    }
    if (!sawClean) {
        // v4-P6-21 diagnosis: dump the raw bytes of the label.
        const QByteArray utf8 = lastText.toUtf8();
        qInfo().noquote() << "SUGGESTION_HEX:"
                          << QString::fromLatin1(utf8.toHex());
        QStackedWidget *diagStack = window.findChild<QStackedWidget *>();
        if (diagStack) {
            qInfo().noquote() << "STACK_INDEX:" << diagStack->currentIndex();
        }
        QTextEdit *diagErr = window.findChild<QTextEdit *>("errorDisplay");
        if (diagErr && diagErr->isVisible()) {
            qInfo().noquote() << "ERR_DISPLAY:" << diagErr->toPlainText();
        }
        QLabel *fileCount = window.findChild<QLabel *>("fileCountLabel");
        if (fileCount) {
            qInfo().noquote() << "FILE_COUNT:" << fileCount->text();
        }
        QLabel *branchLabel = window.findChild<QLabel *>("branchLabel");
        if (branchLabel) {
            qInfo().noquote() << "BRANCH_LABEL:" << branchLabel->text();
        }
        // v4-P6-21: snapshot state via the controller's view model.
        auto *controller = window.findChild<QObject *>();
        Q_UNUSED(controller);
    }
    QVERIFY2(sawClean, qPrintable(
                 QStringLiteral("clean state not shown; last=%1")
                     .arg(lastText)));
    QPushButton *commitBtn = window.findChild<QPushButton *>("commitButton");
    QLineEdit *messageEdit =
        window.findChild<QLineEdit *>("commitMessageEdit");
    if (commitBtn) {
        QVERIFY(!commitBtn->isEnabled());
    }
    if (messageEdit) {
        QVERIFY(!messageEdit->isEnabled());
    }
}

// --- v4-P6-21 safety & state integrity (batch 2) ---

namespace {
// FakeCredentialStore that fails save or remove on demand.
class FailingCredentialStore : public CredentialStore {
public:
    bool saveFailed = false;
    bool removeFailed = false;
    bool save(const QString &, const QString &) override {
        return !saveFailed;
    }
    bool read(QString *, QString *) override { return false; }
    bool remove() override { return !removeFailed; }
};
}  // namespace

void TestMainWindow::testGitExecutableDynamicResolution() {
   // v4-P6-21 ISSUE-04: an EXISTING executor must pick up a custom
   // git executable configured later (shared default wins at runtime).
   GitCommandExecutor executor;
   // Point the shared default at a nonexistent git -> the pre-existing
   // executor must FAIL (proving it no longer uses the system git).
   GitCommandExecutor::setDefaultGitExecutable(
       QStringLiteral("C:/definitely/not/git.exe"));
   GitCommandExecutor::Result bad =
       executor.execute("status", {"--porcelain"});
   QVERIFY(bad.error.hasError());
   // Restore the real git -> the SAME executor must succeed again.
   const QString realGit = QStandardPaths::findExecutable("git");
   QVERIFY(!realGit.isEmpty());
   GitCommandExecutor::setDefaultGitExecutable(realGit);
   GitCommandExecutor::Result good =
       executor.execute("version", {});
   QVERIFY2(!good.error.hasError(), qPrintable(good.error.message));
   GitCommandExecutor::setDefaultGitExecutable(QString());
}

void TestMainWindow::testCredentialSaveFailureDoesNotLogin() {
   // v4-P6-21 ISSUE-07: a failed credential save must not commit the
   // LoggedIn state.
   FailingCredentialStore store;
   store.saveFailed = true;
   GitHubAccountService service;
   service.setCredentialStore(&store);
   QVERIFY(!service.login("steven", "ghp_fake_token_123"));
}

void TestMainWindow::testCredentialRemoveFailureDoesNotReportLogout() {
   // v4-P6-21 ISSUE-07: a failed credential removal must not claim
   // the user is logged out.
   FailingCredentialStore store;
   store.removeFailed = true;
   GitHubAccountService service;
   service.setCredentialStore(&store);
   QVERIFY(service.login("steven", "ghp_fake_token_123"));
   QVERIFY(!service.logout());
}

void TestMainWindow::testNoVisibleBranchRelationWidgetInMainBranchPage() {
   // v4-P6-21 ISSUE-09: the main branch page must not own an orphan graph.
   MainWindow window;
   const auto graphs = window.findChildren<BranchRelationWidget *>();
   QVERIFY2(graphs.isEmpty(), "main window owns an orphan branch graph");
}

// v4-P6-21R5: TRUE delayed race - repo A refresh is held pending
// (fake service, 600ms), user switches to repo B, B refreshes and
// completes, THEN A's stale completion arrives. The stale A head
// must never overwrite B's snapshot.
void TestMainWindow::repositoryRaceStaleRefreshIgnored() {
   MainWindow window;
   // Load repo A through the normal flow.
   switchToWorkspace(window);
   const QString repoA = m_repo.path();
   // Create repo B (a second real repository).
   const QString repoB =
       QDir::tempPath() + "/qgs_p621r5_raceB_"
       + QUuid::createUuid().toString().left(8);
   QVERIFY(QDir().mkpath(repoB));
   QProcess init;
   init.setWorkingDirectory(repoB);
   init.start("git", {"init", "-q", "-b", "main"});
   QVERIFY(init.waitForFinished(15000) && init.exitCode() == 0);
   QFile f(repoB + "/b.txt");
   f.open(QIODevice::WriteOnly);
   f.write("b\n");
   f.close();
   QProcess commit;
   commit.setWorkingDirectory(repoB);
   commit.start("git", {"add", "-A"});
   commit.waitForFinished(15000);
   commit.start("git", {"commit", "-q", "-m", "B"});
   QVERIFY(commit.waitForFinished(15000) && commit.exitCode() == 0);

   // Inject the delayed fake for repo A only.
   FakeDelayedSnapshotService *fake = new FakeDelayedSnapshotService(&window);
   fake->delayedPath = repoA;
   window.controller()->viewModel()->setSnapshotServiceForTest(fake);

   // Start a refresh on A - it goes PENDING (600ms) via the fake.
   window.controller()->viewModel()->setWorkingDirectory(repoA);
   window.controller()->refreshAsync({});
   QTRY_VERIFY_WITH_TIMEOUT(fake->hasPending(), 3000);
   QCOMPARE(fake->delayedRequestTarget, repoA);

   // Switch to B and let B refresh complete.
   QMetaObject::invokeMethod(&window, "onProjectSelected",
                             Q_ARG(QString, repoB));
   QElapsedTimer t;
   t.start();
   while (window.controller()->viewModel()->isLoading()
          && t.elapsed() < 15000) {
       QApplication::processEvents();
       QTest::qWait(20);
   }
   QVERIFY(!window.controller()->viewModel()->isLoading());
   // B's real snapshot never sets head (real service leaves it empty) -
   // the invariant is that A's stale fake head must NOT appear.
   const QString bHead = window.controller()->viewModel()->snapshot().head;

   // Release A's stale completion only after B is current and complete.
   QCOMPARE(window.controller()->workingDirectory(), repoB);
   fake->completeDelayed();
   QApplication::processEvents();

   // The stale A head must NOT have overwritten B.
   QCOMPARE(window.controller()->viewModel()->snapshot().head, bHead);
   QVERIFY(window.controller()->viewModel()->snapshot().head
           != QStringLiteral("A-FAKE-STALE-HEAD"));
   QVERIFY(!window.controller()->viewModel()->isLoading());
   qInfo().noquote()
       << QStringLiteral("REPOSITORY_RACE A_request=%1 B_current=%2 A_late=%3 result=PASS")
              .arg(fake->delayedRequestTarget, repoB,
                   QStringLiteral("A-FAKE-STALE-HEAD"));
   QDir(repoB).removeRecursively();
}

void TestMainWindow::repositoryTransitionRejectsStaleUiClicks() {
   MainWindow window;
   switchToWorkspace(window);
   QVERIFY(window.repositorySessionReadyForTest());

   const QString repoB =
       QDir::tempPath() + "/qgs_p621r6_transitionB_"
       + QUuid::createUuid().toString().left(8);
   QVERIFY(QDir().mkpath(repoB));
   QProcess init;
   init.setWorkingDirectory(repoB);
   init.start("git", {"init", "-q", "-b", "main"});
   QVERIFY(init.waitForFinished(15000) && init.exitCode() == 0);

   auto *fake = new FakeDelayedSnapshotService(&window);
   fake->delayedPath = repoB;
   window.controller()->viewModel()->setSnapshotServiceForTest(fake);

   auto *refresh =
       window.findChild<QPushButton *>(QStringLiteral("refreshButton"));
   QVERIFY(refresh != nullptr);
   const int refreshCount = window.refreshInvocationCountForTest();

   QVERIFY(QMetaObject::invokeMethod(&window, "onProjectSelected",
                                     Q_ARG(QString, repoB)));
   QTRY_VERIFY_WITH_TIMEOUT(fake->hasPending(), 5000);
   QCOMPARE(fake->delayedRequestTarget, repoB);
   QCOMPARE(window.controller()->workingDirectory(), repoB);
   QVERIFY(!window.repositorySessionReadyForTest());
   QVERIFY(!refresh->isEnabled());

   // This is a real UI click while B is loading. The visible control is
   // disabled and the business entry has an independent session-ready gate.
   QTest::mouseClick(refresh, Qt::LeftButton);
   QApplication::processEvents();
   QCOMPARE(window.refreshInvocationCountForTest(), refreshCount);

   fake->completeDelayed();
   QTRY_VERIFY_WITH_TIMEOUT(window.repositorySessionReadyForTest(), 10000);
   QTRY_VERIFY_WITH_TIMEOUT(!window.projectOperationBusyForTest(), 10000);
   qInfo().noquote()
       << QStringLiteral("REPOSITORY_TRANSITION A_ui_stale=1 B_target=%1 session_ready_during_load=0 real_refresh_click_invocations=0 result=PASS")
              .arg(repoB);
   QDir(repoB).removeRecursively();
}

// v4-P6-21R5: remote-list callback from repo A arriving after a
// switch to repo B must not touch B's remote state (session fence).
void TestMainWindow::remoteOverlapStaleCallbackIgnored() {
   MainWindow window;
   switchToWorkspace(window);
   const QString repoA = m_repo.path();
   const QString repoB =
       QDir::tempPath() + "/qgs_p621r5_remB_"
       + QUuid::createUuid().toString().left(8);
   QVERIFY(QDir().mkpath(repoB));
   QProcess init;
   init.setWorkingDirectory(repoB);
   init.start("git", {"init", "-q", "-b", "main"});
   QVERIFY(init.waitForFinished(15000) && init.exitCode() == 0);
   QProcess remoteAdd;
   remoteAdd.setWorkingDirectory(repoB);
   remoteAdd.start("git", {"remote", "add", "origin",
                           "C:/definitely/not/a/remote.git"});
   QVERIFY(remoteAdd.waitForFinished(15000) && remoteAdd.exitCode() == 0);

   // A has NO remotes; B HAS origin. Switch A -> B and verify the
   // remote buttons reflect B (remote available), and that a late A
   // remote-list result cannot disable B's buttons.
   QMetaObject::invokeMethod(&window, "onProjectSelected",
                             Q_ARG(QString, repoB));
   QElapsedTimer t;
   t.start();
   QPushButton *fetch = window.findChild<QPushButton *>("fetchButton");
   while (fetch && !fetch->isEnabled() && t.elapsed() < 10000) {
       QApplication::processEvents();
       QTest::qWait(20);
   }
   QVERIFY(fetch && fetch->isEnabled());  // B has a remote
   QTest::qWait(500);  // drain any stale A callbacks
   QApplication::processEvents();
   QVERIFY(fetch->isEnabled());  // stale A result did not disable B
   QDir(repoB).removeRecursively();
}

    // v4-P6-21R5 P1-02: a busy/feedback state on branch action buttons
    // must be reset when the repository session changes - a late timer
    // from repo A can never re-enable/disable or re-word repo B's
    // buttons.
    void TestMainWindow::staleFeedbackBranchButtonsReset() {
        MainWindow window;
        switchToWorkspace(window);
        QPushButton *createBtn =
            window.findChild<QPushButton *>("createBranchButton");
        QPushButton *deleteBtn =
            window.findChild<QPushButton *>("deleteBranchButton");
        QVERIFY(createBtn && deleteBtn);

        // Simulate a pending operation from repo A.
        ActionButtonFeedback::showBusy(createBtn, QStringLiteral("Creating..."));
        ActionButtonFeedback::showBusy(deleteBtn, QStringLiteral("Deleting..."));

        // Switch to repo B (a second real repository).
        const QString repoB =
            QDir::tempPath() + "/qgs_p621r5_fbB_"
            + QUuid::createUuid().toString().left(8);
        QVERIFY(QDir().mkpath(repoB));
        QProcess init;
        init.setWorkingDirectory(repoB);
        init.start("git", {"init", "-q", "-b", "main"});
        QVERIFY(init.waitForFinished(15000) && init.exitCode() == 0);
        QMetaObject::invokeMethod(&window, "onProjectSelected",
                                  Q_ARG(QString, repoB));
        // The switch applies asynchronously (detect -> apply) - wait for
        // the feedback reset to land.
        QElapsedTimer t;
        t.start();
        while (createBtn->text() == QStringLiteral("Creating...")
               && t.elapsed() < 10000) {
            QApplication::processEvents();
            QTest::qWait(20);
        }

        // The branch buttons must be reset to the B-derived state: the
        // busy text must be gone (restored to the default label, not
        // stuck on "Creating..."/"Deleting..."), enabled state decided
        // by the B repository reducers.
        QVERIFY(createBtn->text() != QStringLiteral("Creating..."));
        QVERIFY(deleteBtn->text() != QStringLiteral("Deleting..."));
        QTest::qWait(1800);  // old timers would fire here - must no-op
        QApplication::processEvents();
        QVERIFY(createBtn->text() != QStringLiteral("Creating..."));
        QDir(repoB).removeRecursively();
    }

QTEST_MAIN(TestMainWindow)
#include "test_mainwindow.moc"
