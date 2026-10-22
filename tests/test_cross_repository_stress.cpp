#include <QtTest>

#include <QDir>
#include <QProcess>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTreeWidget>

#include "GitBranchService.h"
#include "GitCommandExecutor.h"
#include "GitCommitService.h"
#include "GitController.h"
#include "GitRemoteService.h"
#include "GitSnapshotService.h"
#include "GitViewModel.h"
#include "DiffViewerWidget.h"
#include "MainWindow.h"
#include "test_settings_isolation.h"

namespace {

bool initRepository(const QString &path) {
    if (!QDir().mkpath(path)) {
        return false;
    }
    QProcess process;
    process.setWorkingDirectory(path);
    process.start(QStringLiteral("git"),
                  {QStringLiteral("init"), QStringLiteral("-q"),
                   QStringLiteral("-b"), QStringLiteral("main")});
    return process.waitForFinished(15000) && process.exitCode() == 0;
}

class ManualSnapshotService final : public GitSnapshotService {
public:
    using GitSnapshotService::GitSnapshotService;
    QString repoA;
    QString requestA;
    GitRepository::SnapshotCompletion pendingA;

    void createSnapshotAsync(GitRepository::SnapshotCompletion completion) override {
        const QString target = workingDirectory();
        if (target == repoA) {
            requestA = target;
            pendingA = completion;
            return;
        }
        GitSnapshot snapshot;
        snapshot.head = QStringLiteral("B-CURRENT-HEAD");
        snapshot.headRef = QStringLiteral("refs/heads/main");
        completion(snapshot, GitError());
    }

    void releaseA() {
        const auto completion = pendingA;
        pendingA = {};
        GitSnapshot snapshot;
        snapshot.head = QStringLiteral("A-LATE-HEAD");
        snapshot.headRef = QStringLiteral("refs/heads/old-a");
        completion(snapshot, GitError());
    }
};

class TwoStageExecutor final : public GitCommandExecutor {
public:
    using GitCommandExecutor::GitCommandExecutor;
    bool firstHeld = false;
    Completion pendingFirst;
    QString firstRepository;
    QString firstCommand;
    int stage2Invocations = 0;
    QStringList stage2Repositories;

    void executeAsync(const QString &command, const QStringList &args,
                      Completion completion, int timeoutMs) override {
        Q_UNUSED(args);
        Q_UNUSED(timeoutMs);
        if (!firstHeld) {
            firstHeld = true;
            firstRepository = workingDirectory();
            firstCommand = command;
            pendingFirst = completion;
            return;
        }
        ++stage2Invocations;
        stage2Repositories << workingDirectory();
        Result result;
        result.exitCode = 0;
        result.exitStatus = QProcess::NormalExit;
        completion(result);
    }

    void releaseAsSuccess() {
        const auto completion = pendingFirst;
        pendingFirst = {};
        Result result;
        result.exitCode = 0;
        result.exitStatus = QProcess::NormalExit;
        completion(result);
    }

    void releaseSwitchUnavailable() {
        const auto completion = pendingFirst;
        pendingFirst = {};
        Result result;
        result.exitCode = 1;
        result.exitStatus = QProcess::NormalExit;
        result.error.type = GitErrorType::ProcessFailed;
        result.errorOutput = "git: 'switch' is not a git command";
        completion(result);
    }
};

class DelayedDiffExecutor final : public GitCommandExecutor {
public:
    using GitCommandExecutor::GitCommandExecutor;
    bool firstHeld = false;
    Completion pendingFirst;
    QStringList repositories;
    QStringList commands;

    void executeAsync(const QString &command, const QStringList &args,
                      Completion completion, int timeoutMs) override {
        Q_UNUSED(args);
        Q_UNUSED(timeoutMs);
        repositories << workingDirectory();
        commands << command;
        if (!firstHeld) {
            firstHeld = true;
            pendingFirst = completion;
            return;
        }
        Result result;
        result.exitCode = 0;
        result.exitStatus = QProcess::NormalExit;
        result.output = "A-STAGED\n";
        completion(result);
    }

    void releaseFirst() {
        const auto completion = pendingFirst;
        pendingFirst = {};
        Result result;
        result.exitCode = 0;
        result.exitStatus = QProcess::NormalExit;
        result.output = "A-UNSTAGED\n";
        completion(result);
    }
};

class ImmediateDiffExecutor final : public GitCommandExecutor {
public:
    using GitCommandExecutor::GitCommandExecutor;
    int calls = 0;
    void executeAsync(const QString &command, const QStringList &args,
                      Completion completion, int timeoutMs) override {
        Q_UNUSED(command);
        Q_UNUSED(args);
        Q_UNUSED(timeoutMs);
        ++calls;
        Result result;
        result.exitCode = 0;
        result.exitStatus = QProcess::NormalExit;
        result.output = calls == 1 ? QByteArray("B-UNSTAGED\n")
                                   : QByteArray("B-STAGED\n");
        completion(result);
    }
};

class DelayedRemoteService final : public GitRemoteService {
public:
    using GitRemoteService::GitRemoteService;
    struct Pending {
        QString repository;
        OperationCompletion completion;
    };
    QList<Pending> pending;

    void remotesAsync(RemotesCompletion completion) override {
        RemotesResult result;
        result.success = true;
        GitRemote remote;
        remote.name = QStringLiteral("origin");
        remote.fetchUrl = QStringLiteral("fake://fetch");
        remote.pushUrl = QStringLiteral("fake://push");
        result.remotes << remote;
        completion(result);
    }
    void fetchAsync(OperationCompletion completion) override {
        pending.append({workingDirectory(), completion});
    }
    void pullAsync(OperationCompletion completion) override {
        pending.append({workingDirectory(), completion});
    }
    void pushAsync(OperationCompletion completion) override {
        pending.append({workingDirectory(), completion});
    }
    bool complete(const QString &repository) {
        for (int i = 0; i < pending.size(); ++i) {
            if (pending.at(i).repository != repository) {
                continue;
            }
            const Pending item = pending.takeAt(i);
            OperationResult result;
            result.success = true;
            result.alreadyUpToDate = true;
            item.completion(result);
            return true;
        }
        return false;
    }
};

void openRepository(MainWindow &window, const QString &path) {
    QVERIFY(QMetaObject::invokeMethod(&window, "onProjectSelected",
                                      Q_ARG(QString, path)));
    QTRY_COMPARE_WITH_TIMEOUT(window.controller()->workingDirectory(), path,
                              15000);
    QTRY_VERIFY_WITH_TIMEOUT(!window.controller()->viewModel()->isLoading(),
                             20000);
    QTRY_VERIFY_WITH_TIMEOUT(!window.projectOperationBusyForTest(), 20000);
    QApplication::processEvents();
}

}  // namespace

class TestCrossRepositoryStress : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { isolateTestSettings(); }

    void thirtyOrderedOperations() {
        QTemporaryDir repos;
        QVERIFY(repos.isValid());
        const QString repoA = QDir(repos.path()).filePath(QStringLiteral("repo-a"));
        const QString repoB = QDir(repos.path()).filePath(QStringLiteral("repo-b"));
        QVERIFY(initRepository(repoA));
        QVERIFY(initRepository(repoB));

        for (int round = 1; round <= 30; ++round) {
            // 1/5 refresh: A snapshot pending, B completes, A arrives late.
            GitViewModel viewModel;
            auto *snapshots = new ManualSnapshotService(&viewModel);
            snapshots->repoA = repoA;
            viewModel.setSnapshotServiceForTest(snapshots);
            viewModel.setWorkingDirectory(repoA);
            const quint64 refreshAGeneration =
                viewModel.sessionGenerationForTest();
            viewModel.refreshAsync({});
            QVERIFY(bool(snapshots->pendingA));
            QCOMPARE(snapshots->requestA, repoA);
            viewModel.resetSession();
            viewModel.setWorkingDirectory(repoB);
            const quint64 refreshBGeneration =
                viewModel.sessionGenerationForTest();
            bool bRefreshDone = false;
            viewModel.refreshAsync([&](GitError error) {
                QVERIFY(!error.hasError());
                bRefreshDone = true;
            });
            QVERIFY(bRefreshDone);
            QCOMPARE(viewModel.snapshot().head, QStringLiteral("B-CURRENT-HEAD"));
            snapshots->releaseA();
            QCOMPARE(viewModel.snapshot().head, QStringLiteral("B-CURRENT-HEAD"));
            QVERIFY(!viewModel.isLoading());
            qInfo().noquote()
                << QStringLiteral("CROSS round=%1 op=refresh A_path=%2 A_generation=%3 B_path=%4 B_generation=%5 command=snapshot order=A-pending,B-complete,A-late result=PASS")
                       .arg(round)
                       .arg(repoA)
                       .arg(refreshAGeneration)
                       .arg(repoB)
                       .arg(refreshBGeneration);

            // 2/5 unstage: service stage 1 is A; no stage 2 may reach B.
            GitCommitService commitService;
            auto *unstageExecutor = new TwoStageExecutor(&commitService);
            commitService.setExecutorForTest(unstageExecutor);
            commitService.setWorkingDirectory(repoA);
            const quint64 unstageAGeneration =
                commitService.repositoryGenerationForTest();
            commitService.unstageFilesAsync({QStringLiteral("a.txt")},
                                            [](GitCommitService::OperationResult) {});
            QCOMPARE(unstageExecutor->firstRepository, repoA);
            commitService.setWorkingDirectory(repoB);
            const quint64 unstageBGeneration =
                commitService.repositoryGenerationForTest();
            unstageExecutor->releaseAsSuccess();
            QCOMPARE(unstageExecutor->stage2Invocations, 0);
            qInfo().noquote()
                << QStringLiteral("CROSS round=%1 op=unstage A_path=%2 A_generation=%3 B_path=%4 B_generation=%5 command=rev-parse->restore order=A-stage1-pending,B-retarget,A-late,stage2-blocked result=PASS")
                       .arg(round)
                       .arg(repoA)
                       .arg(unstageAGeneration)
                       .arg(repoB)
                       .arg(unstageBGeneration);

            // 3/5 branch fallback: A switch unavailable after retarget; no
            // checkout fallback may be launched in B.
            GitBranchService branchService;
            auto *branchExecutor = new TwoStageExecutor(&branchService);
            branchService.setExecutorForTest(branchExecutor);
            branchService.setWorkingDirectory(repoA);
            const quint64 branchAGeneration =
                branchService.repositoryGenerationForTest();
            branchService.checkoutBranchAsync(QStringLiteral("feature/a"),
                                              [](GitBranchService::OperationResult) {});
            QCOMPARE(branchExecutor->firstRepository, repoA);
            branchService.setWorkingDirectory(repoB);
            const quint64 branchBGeneration =
                branchService.repositoryGenerationForTest();
            branchExecutor->releaseSwitchUnavailable();
            QCOMPARE(branchExecutor->stage2Invocations, 0);
            qInfo().noquote()
                << QStringLiteral("CROSS round=%1 op=branch-fallback A_path=%2 A_generation=%3 B_path=%4 B_generation=%5 command=switch->checkout order=A-switch-pending,B-retarget,A-unavailable,checkout-blocked result=PASS")
                       .arg(round)
                       .arg(repoA)
                       .arg(branchAGeneration)
                       .arg(repoB)
                       .arg(branchBGeneration);

            // 4/5 diff: both request stages stay on immutable A even after
            // the controller's current repository becomes B.
            GitController diffController;
            DelayedDiffExecutor *diffExecutor = nullptr;
            diffController.setFileDiffExecutorFactoryForTest(
                [&](QObject *parent) {
                    diffExecutor = new DelayedDiffExecutor(parent);
                    return diffExecutor;
                });
            diffController.setRepositoryPath(repoA);
            const quint64 diffAGeneration =
                diffController.repositoryGenerationForTest();
            GitController::FileDiffResult diffResult;
            bool diffDone = false;
            diffController.fileDiffAsync(QStringLiteral("a.txt"),
                                         [&](GitController::FileDiffResult result) {
                                             diffResult = result;
                                             diffDone = true;
                                         });
            QVERIFY(diffExecutor != nullptr);
            QCOMPARE(diffExecutor->repositories, QStringList{repoA});
            diffController.setRepositoryPath(repoB);
            const quint64 diffBGeneration =
                diffController.repositoryGenerationForTest();
            diffExecutor->releaseFirst();
            QVERIFY(diffDone);
            QVERIFY(diffResult.success);
            QCOMPARE(diffExecutor->repositories, QStringList({repoA, repoA}));
            QVERIFY(diffResult.text.contains(QStringLiteral("A-UNSTAGED")));
            QVERIFY(diffResult.text.contains(QStringLiteral("A-STAGED")));
            QCOMPARE(diffController.workingDirectory(), repoB);
            qInfo().noquote()
                << QStringLiteral("CROSS round=%1 op=diff A_path=%2 A_generation=%3 B_path=%4 B_generation=%5 command=diff, diff--cached order=A-unstaged-pending,B-retarget,A-unstaged-complete,A-staged-complete result=PASS")
                       .arg(round)
                       .arg(repoA)
                       .arg(diffAGeneration)
                       .arg(repoB)
                       .arg(diffBGeneration);

            // 5/5 remote UI: A Fetch pending, switch B, B Fetch pending,
            // A late completion must leave B busy until B completes.
            MainWindow window;
            auto *remote = new DelayedRemoteService(window.controller());
            window.controller()->setRemoteServiceForTest(remote);
            openRepository(window, repoA);
            const quint64 remoteAGeneration =
                window.repositoryGenerationForTest();
            auto *fetch = window.findChild<QPushButton *>(QStringLiteral("fetchButton"));
            QVERIFY(fetch != nullptr);
            QTRY_VERIFY_WITH_TIMEOUT(fetch->isEnabled(), 10000);
            QTest::mouseClick(fetch, Qt::LeftButton);
            QTRY_COMPARE(remote->pending.size(), 1);
            openRepository(window, repoB);
            const quint64 remoteBGeneration =
                window.repositoryGenerationForTest();
            QTRY_VERIFY_WITH_TIMEOUT(fetch->isEnabled(), 10000);
            QTest::mouseClick(fetch, Qt::LeftButton);
            QTRY_COMPARE(remote->pending.size(), 2);
            QVERIFY(remote->complete(repoA));
            QApplication::processEvents();
            QVERIFY(fetch->property("busy").toBool());
            QVERIFY(!fetch->isEnabled());
            QVERIFY(remote->complete(repoB));
            QTRY_VERIFY_WITH_TIMEOUT(!fetch->property("busy").toBool(), 3000);
            qInfo().noquote()
                << QStringLiteral("CROSS round=%1 op=remote A_path=%2 A_generation=%3 B_path=%4 B_generation=%5 command=fetch order=A-pending,B-pending,A-late,B-complete result=PASS")
                       .arg(round)
                       .arg(repoA)
                       .arg(remoteAGeneration)
                       .arg(repoB)
                       .arg(remoteBGeneration);
        }
    }

    void staleDiffCannotOverwriteCurrentRepository() {
        QTemporaryDir repos;
        QVERIFY(repos.isValid());
        const QString repoA = QDir(repos.path()).filePath(QStringLiteral("diff-a"));
        const QString repoB = QDir(repos.path()).filePath(QStringLiteral("diff-b"));
        QVERIFY(initRepository(repoA));
        QVERIFY(initRepository(repoB));
        QFile fileA(QDir(repoA).filePath(QStringLiteral("a.txt")));
        QVERIFY(fileA.open(QIODevice::WriteOnly));
        fileA.write("a\n");
        fileA.close();
        QFile fileB(QDir(repoB).filePath(QStringLiteral("b.txt")));
        QVERIFY(fileB.open(QIODevice::WriteOnly));
        fileB.write("b\n");
        fileB.close();

        MainWindow window;
        DelayedDiffExecutor *delayedA = nullptr;
        int factoryCalls = 0;
        window.controller()->setFileDiffExecutorFactoryForTest(
            [&](QObject *parent) -> GitCommandExecutor * {
                ++factoryCalls;
                if (factoryCalls == 1) {
                    delayedA = new DelayedDiffExecutor(parent);
                    return delayedA;
                }
                return new ImmediateDiffExecutor(parent);
            });

        openRepository(window, repoA);
        auto *changes = window.findChild<QTreeWidget *>(QStringLiteral("changesTree"));
        auto *viewer = window.findChild<DiffViewerWidget *>(QStringLiteral("fileDiffViewer"));
        QVERIFY(changes != nullptr);
        QVERIFY(viewer != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(changes->topLevelItemCount() > 0, 10000);
        changes->setCurrentItem(changes->topLevelItem(0));
        QTRY_VERIFY_WITH_TIMEOUT(delayedA && bool(delayedA->pendingFirst), 3000);
        QCOMPARE(delayedA->repositories, QStringList{repoA});

        openRepository(window, repoB);
        QTRY_VERIFY_WITH_TIMEOUT(changes->topLevelItemCount() > 0, 10000);
        changes->setCurrentItem(changes->topLevelItem(0));
        QTRY_VERIFY_WITH_TIMEOUT(viewer->currentText().contains(
                                     QStringLiteral("B-UNSTAGED")),
                                 3000);
        const QString bText = viewer->currentText();
        delayedA->releaseFirst();
        QApplication::processEvents();
        QCOMPARE(viewer->currentText(), bText);
        QVERIFY(!viewer->currentText().contains(QStringLiteral("A-UNSTAGED")));
        QCOMPARE(window.controller()->workingDirectory(), repoB);
        qInfo().noquote()
            << QStringLiteral("STALE_DIFF A_request=%1 B_current=%2 B_visible=1 A_late_dropped=1 result=PASS")
                   .arg(repoA, repoB);
    }
};

QTEST_MAIN(TestCrossRepositoryStress)
#include "test_cross_repository_stress.moc"
