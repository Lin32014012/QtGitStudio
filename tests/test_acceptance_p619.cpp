// v4-P6-19 manual-acceptance automation: the acceptance items that can
// be verified headlessly - init + first commit chain, rapid re-entrant
// clicks, credential persistence round-trip, refresh x10, remote
// failure/success, stash round-trip, tag create/delete.
#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QTimer>
#include <QEventLoop>
#include <QSignalSpy>
#include <QPushButton>
#include <QTextEdit>
#include "GitController.h"
#include "GitCommitService.h"
#include "GitStashService.h"
#include "GitTagService.h"
#include "GitRemoteService.h"
#include "GitHubAccountService.h"
#include "CredentialStoreWin.h"
#include "MainWindow.h"
#include "test_settings_isolation.h"

static bool runGit(const QString &repo, const QStringList &args) {
    QProcess p;
    p.setWorkingDirectory(repo);
    p.start("git", args);
    return p.waitForFinished(15000) && p.exitCode() == 0;
}

static void writeFile(const QString &repo, const QString &name,
                      const QByteArray &content) {
    QFile f(repo + "/" + name);
    f.open(QIODevice::WriteOnly);
    f.write(content);
    f.close();
}

static bool waitLoop(QEventLoop &loop, int ms = 15000) {
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
    return true;
}

    // v4-P6-21R5 P1-05: deterministic fallback chain via injected fake
    // Win32 API - no real Windows credential is touched.
    class FakeCredentialApi : public ICredentialApi {
    public:
        // per-attempt results: true = success, false = fail with lastError
        QList<QPair<bool, unsigned long>> attempts;
        unsigned long err = 0;
        bool write(void *, unsigned long) override {
            if (attempts.isEmpty()) {
                return true;
            }
            const auto r = attempts.takeFirst();
            err = r.second;
            return r.first;
        }
        bool read(const wchar_t *, int, void **) override { return false; }
        bool remove(const wchar_t *, int, unsigned long) override { return true; }
        unsigned long lastError() const override { return err; }
    };

class TestAcceptanceP619 : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        isolateTestSettings();
    }

    // P0: init an empty repo, load it, then first commit appears.
    void initRepoThenFirstCommitWorks() {
        const QString repo =
            QDir::tempPath() + "/qgs_p619_init_"
            + QUuid::createUuid().toString().left(8);
        QDir().mkpath(repo);
        QVERIFY(runGit(repo, {"init", "-q", "-b", "main"}));

        MainWindow window;
        QMetaObject::invokeMethod(&window, "onProjectSelected",
                                  Q_ARG(QString, repo));
        QEventLoop loop;
        QTimer::singleShot(8000, &loop, &QEventLoop::quit);
        loop.exec();
        QApplication::processEvents();
        auto *graphButton = window.findChild<QPushButton *>(
            "openGraphViewerButton");
        QVERIFY(graphButton != nullptr);  // no crash on empty repo

        // First commit through the services the UI uses.
        writeFile(repo, "README.md", "hello\n");
        GitController controller;
        controller.setRepositoryPath(repo);
        bool staged = false;
        QEventLoop stageLoop;
        controller.stageFilesAsync({QStringLiteral("README.md")},
                                   [&](const GitController::OperationResult &r) {
                                       staged = r.success;
                                       stageLoop.quit();
                                   });
        waitLoop(stageLoop);
        QVERIFY(staged);
        GitCommitService commitService;
        commitService.setWorkingDirectory(repo);
        bool committed = false;
        QEventLoop commitLoop;
        commitService.commitAsync(
            QStringLiteral("first commit"),
            [&](const GitCommitService::OperationResult &r) {
                committed = r.success;
                commitLoop.quit();
            });
        waitLoop(commitLoop);
        QVERIFY(committed);
        QString head;
        QVERIFY(runGit(repo, {"rev-parse", "--short", "HEAD"}));
        QDir(repo).removeRecursively();
    }

    // P0: rapid real clicks on the init card with an INJECTED picker
    // (never the native QFileDialog - it fast-fails 0xc0000409 when a
    // timer closes it on the visible platform). The second click must
    // be gated by m_operationBusy: picker invocation stays 1, then the
    // window is destroyed and the process must exit 0.
    // v4-P6-21R6: 30 clicks + event-loop drain + explicit stable state.
    void rapidInitClicksDoNotCrash() {
        MainWindow window;
        int pickerCalls = 0;
        // A small UNIQUE empty directory - git init must be fast and
        // must not scan the whole %TEMP% tree.
        const QString tempDir =
            QDir::tempPath() + "/qgs_p621r2_rapid_"
            + QUuid::createUuid().toString().left(8);
        QDir().mkpath(tempDir);
        window.setDirectoryPicker(
            [&pickerCalls, tempDir](QWidget *, const QString &,
                                    const QString &) {
                ++pickerCalls;
                return tempDir;
            });
        window.show();
        QApplication::processEvents();
        QWidget *initBlock = window.findChild<QWidget *>("initAction");
        QVERIFY(initBlock != nullptr);
        for (int i = 0; i < 30; ++i) {
            QTest::mouseClick(initBlock, Qt::LeftButton);
        }
        // Only the FIRST click may start the init orchestration; the
        // remaining clicks are re-entrancy gated.
        QCOMPARE(pickerCalls, 1);
        // Drain pending async completions to a measured stable state.
        QTRY_VERIFY_WITH_TIMEOUT(!window.projectOperationBusyForTest(), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(!window.controller()->viewModel()->isLoading(),
                                 20000);
        bool eventLoopResponsive = false;
        QTimer::singleShot(0, &window,
                           [&eventLoopResponsive]() {
                               eventLoopResponsive = true;
                           });
        QTRY_VERIFY_WITH_TIMEOUT(eventLoopResponsive, 2000);
        // R6 test integrity requires a meaningful postcondition: the
        // window remains visible and responsive after 30 rapid clicks
        // and a measured async drain.
        QVERIFY(window.isVisible());
        QVERIFY(QApplication::activeModalWidget() == nullptr);
        QVERIFY(!window.projectOperationBusyForTest());
        qInfo().noquote()
            << QStringLiteral("RAPID_INIT clicks=30 picker_calls=%1 responsive=1 modal=0 busy=0 result=PASS")
                   .arg(pickerCalls);
        QDir(tempDir).removeRecursively();
    }

    // P1-1: real Windows Credential Manager round-trip (fake token).
    // v4-P6-21R1: uses an ISOLATED target namespace with a unique
    // suffix - never the production QtGitStudio/GitHubToken.
    void credentialFallbackEnterpriseFailsLocalMachineSucceeds() {
        FakeCredentialApi api;
        api.attempts = {{false, 1312}, {true, 0}};  // ENTERPRISE fails, LM ok
        CredentialStoreWin store(
            QStringLiteral("QtGitStudio/Test/GitHubToken/")
            + QUuid::createUuid().toString().left(8));
        store.setCredentialApi(&api);
        QVERIFY(store.save(QStringLiteral("u"), QStringLiteral("t")));
        QCOMPARE(store.lastPersistMode(),
                 CredentialStoreWin::PersistMode::LocalMachine);
    }

    void credentialFallbackUnknownErrorStopsImmediately() {
        FakeCredentialApi api;
        api.attempts = {{false, 5}, {true, 0}, {true, 0}};  // 5 = access denied
        CredentialStoreWin store(
            QStringLiteral("QtGitStudio/Test/GitHubToken/")
            + QUuid::createUuid().toString().left(8));
        store.setCredentialApi(&api);
        QVERIFY(!store.save(QStringLiteral("u"), QStringLiteral("t")));
        // LOCAL_MACHINE and SESSION must NEVER be attempted - exactly
        // ONE write call happened (the remaining attempts are untouched).
        QCOMPARE(api.attempts.size(), 2);
        QCOMPARE(store.lastPersistMode(),
                 CredentialStoreWin::PersistMode::None);
    }

    void credentialFallbackSessionChain() {
        FakeCredentialApi api;
        api.attempts = {{false, 1312}, {false, 1312}, {true, 0}};
        CredentialStoreWin store(
            QStringLiteral("QtGitStudio/Test/GitHubToken/")
            + QUuid::createUuid().toString().left(8));
        store.setCredentialApi(&api);
        QVERIFY(store.save(QStringLiteral("u"), QStringLiteral("t")));
        QCOMPARE(store.lastPersistMode(),
                 CredentialStoreWin::PersistMode::Session);
    }

    void credentialStoreWinRoundTrip() {
        const QString testTarget =
            QStringLiteral("QtGitStudio/Test/GitHubToken/")
            + QUuid::createUuid().toString().left(8);
        CredentialStoreWin store(testTarget);
        QVERIFY(store.save(QStringLiteral("acceptance-user"),
                           QStringLiteral("ghp_acceptance_fake_123")));
        QString user;
        QString token;
        QVERIFY(store.read(&user, &token));
        QCOMPARE(user, QStringLiteral("acceptance-user"));
        QCOMPARE(token, QStringLiteral("ghp_acceptance_fake_123"));
        QVERIFY(store.remove());
        QString u2;
        QString t2;
        QVERIFY(!store.read(&u2, &t2));  // gone after remove
    }

    // P1-3: refresh ten times on a normal repo - every round reports a
    // real outcome (success), never a hard-coded fail-then-success.
    void refreshTenTimesAllSucceed() {
        const QString repo =
            QDir::tempPath() + "/qgs_p619_refresh_"
            + QUuid::createUuid().toString().left(8);
        QDir().mkpath(repo);
        QVERIFY(runGit(repo, {"init", "-q", "-b", "main"}));
        runGit(repo, {"config", "user.name", "QtGitStudio Test"});
        runGit(repo, {"config", "user.email", "qtgitstudio@example.invalid"});
        writeFile(repo, "a.txt", "1\n");
        runGit(repo, {"add", "-A"});
        runGit(repo, {"-c", "user.name=T", "-c", "user.email=t@t",
                      "commit", "-q", "-m", "base"});

        // v4-P6-21R6 test integrity: drive the REAL Refresh button
        // through MainWindow instead of calling controller.refreshAsync
        // directly - the button must enter the CURRENT pipeline.
        MainWindow window;
        window.show();
        QApplication::processEvents();
        QVERIFY(QMetaObject::invokeMethod(&window, "onProjectSelected",
                                          Q_ARG(QString, repo)));
        QTRY_COMPARE_WITH_TIMEOUT(window.controller()->workingDirectory(), repo,
                                  15000);
        QTRY_VERIFY_WITH_TIMEOUT(!window.controller()->viewModel()->isLoading(),
                                 20000);
        QTRY_VERIFY_WITH_TIMEOUT(!window.projectOperationBusyForTest(), 20000);
        QPushButton *refreshBtn =
            window.findChild<QPushButton *>("refreshButton");
        QVERIFY(refreshBtn != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(refreshBtn->isEnabled(), 10000);
        // 10 real clicks; each click enters the CURRENT refresh
        // pipeline and completes - no stuck busy, button ready again.
        for (int i = 0; i < 10; ++i) {
            const int before = window.refreshInvocationCountForTest();
            const QString defaultText = refreshBtn->text();
            QTest::mouseClick(refreshBtn, Qt::LeftButton);
            QCOMPARE(window.refreshInvocationCountForTest(), before + 1);
            QVERIFY(!refreshBtn->isEnabled());
            QVERIFY(refreshBtn->text() != defaultText);
            QTRY_VERIFY_WITH_TIMEOUT(refreshBtn->isEnabled(), 15000);
            QTRY_COMPARE_WITH_TIMEOUT(refreshBtn->text(), defaultText, 15000);
            QCOMPARE(window.refreshInvocationCountForTest(), before + 1);
        }
        qInfo().noquote()
            << QStringLiteral("REFRESH_REAL_CLICK clicks=10 business_invocations=%1 transitions=10 result=PASS")
                   .arg(window.refreshInvocationCountForTest());
        QDir(repo).removeRecursively();
    }

    // P1-4: fetch against an invalid remote fails with a real error.
    void remoteFetchFailureShowsRealError() {
        const QString repo =
            QDir::tempPath() + "/qgs_p619_remote_"
            + QUuid::createUuid().toString().left(8);
        QDir().mkpath(repo);
        QVERIFY(runGit(repo, {"init", "-q", "-b", "main"}));
        writeFile(repo, "a.txt", "1\n");
        runGit(repo, {"add", "-A"});
        runGit(repo, {"-c", "user.name=T", "-c", "user.email=t@t",
                      "commit", "-q", "-m", "base"});
        runGit(repo, {"remote", "add", "origin",
                      "http://127.0.0.1:1/invalid/repo.git"});

        GitRemoteService service;
        service.setWorkingDirectory(repo);
        bool done = false;
        bool ok = true;
        QString err;
        QEventLoop loop;
        service.fetchAsync([&](const GitRemoteService::OperationResult &r) {
            ok = r.success;
            err = r.error;
            done = true;
            loop.quit();
        });
        waitLoop(loop);
        QVERIFY(done);
        QVERIFY(!ok);  // invalid remote must fail
        QVERIFY(!err.trimmed().isEmpty());
        QDir(repo).removeRecursively();
    }

    // Remote success path: local bare remote push + pull.
    void remotePushPullWithLocalBare() {
        const QString bare =
            QDir::tempPath() + "/qgs_p619_bare_"
            + QUuid::createUuid().toString().left(8) + ".git";
        const QString repo =
            QDir::tempPath() + "/qgs_p619_work_"
            + QUuid::createUuid().toString().left(8);
        QVERIFY(runGit(QDir::tempPath(),
                       {"init", "-q", "--bare", bare}));
        QDir().mkpath(repo);
        QVERIFY(runGit(repo, {"init", "-q", "-b", "main"}));
        writeFile(repo, "a.txt", "1\n");
        runGit(repo, {"add", "-A"});
        runGit(repo, {"-c", "user.name=T", "-c", "user.email=t@t",
                      "commit", "-q", "-m", "base"});
        runGit(repo, {"remote", "add", "origin", bare});
        // Establish upstream first (plain `git push` without upstream
        // fails with a real error - the UI uses the same command).
        QVERIFY(runGit(repo, {"push", "-u", "origin", "main"}));

        GitRemoteService service;
        service.setWorkingDirectory(repo);
        QEventLoop pushLoop;
        bool pushOk = false;
        service.pushAsync([&](const GitRemoteService::OperationResult &r) {
            pushOk = r.success;
            pushLoop.quit();
        });
        waitLoop(pushLoop);
        QVERIFY2(pushOk, "push to local bare remote must succeed");

        // Second clone pulling from the bare remote.
        const QString repo2 =
            QDir::tempPath() + "/qgs_p619_work2_"
            + QUuid::createUuid().toString().left(8);
        QVERIFY(runGit(QDir::tempPath(), {"clone", "-q", bare, repo2}));
        GitRemoteService service2;
        service2.setWorkingDirectory(repo2);
        QEventLoop pullLoop;
        bool pullOk = false;
        service2.pullAsync([&](const GitRemoteService::OperationResult &r) {
            pullOk = r.success;
            pullLoop.quit();
        });
        waitLoop(pullLoop);
        QVERIFY2(pullOk, "pull from local bare remote must succeed");
        QDir(bare).removeRecursively();
        QDir(repo).removeRecursively();
        QDir(repo2).removeRecursively();
    }

    // Stash round-trip: create / apply / pop / drop.
    void stashRoundTrip() {
        const QString repo =
            QDir::tempPath() + "/qgs_p619_stash_"
            + QUuid::createUuid().toString().left(8);
        QDir().mkpath(repo);
        QVERIFY(runGit(repo, {"init", "-q", "-b", "main"}));
        writeFile(repo, "a.txt", "1\n");
        runGit(repo, {"add", "-A"});
        runGit(repo, {"-c", "user.name=T", "-c", "user.email=t@t",
                      "commit", "-q", "-m", "base"});
        writeFile(repo, "a.txt", "2\n");  // dirty

        GitStashService service;
        service.setWorkingDirectory(repo);
        bool created = false;
        QEventLoop cLoop;
        service.createStashAsync(QStringLiteral("acceptance stash"),
                                 [&](const GitStashService::OperationResult &r) {
                                     created = r.success;
                                     cLoop.quit();
                                 });
        waitLoop(cLoop);
        QVERIFY2(created, "stash create must succeed");

        // List contains the stash.
        QProcess list;
        list.setWorkingDirectory(repo);
        list.start("git", {"stash", "list"});
        QVERIFY(list.waitForFinished(15000));
        QVERIFY(QString::fromUtf8(list.readAllStandardOutput())
                    .contains(QStringLiteral("acceptance stash")));

        bool applied = false;
        QEventLoop aLoop;
        service.applyStashAsync(QStringLiteral("stash@{0}"),
                                [&](const GitStashService::OperationResult &r) {
                                    applied = r.success;
                                    aLoop.quit();
                                });
        waitLoop(aLoop);
        QVERIFY2(applied, "stash apply must succeed");

        bool dropped = false;
        QEventLoop dLoop;
        service.dropStashAsync(QStringLiteral("stash@{0}"),
                               [&](const GitStashService::OperationResult &r) {
                                   dropped = r.success;
                                   dLoop.quit();
                               });
        waitLoop(dLoop);
        QVERIFY2(dropped, "stash drop must succeed");
        QDir(repo).removeRecursively();
    }

    // Tag: lightweight + annotated + delete.
    void tagCreateAndDelete() {
        const QString repo =
            QDir::tempPath() + "/qgs_p619_tag_"
            + QUuid::createUuid().toString().left(8);
        QDir().mkpath(repo);
        QVERIFY(runGit(repo, {"init", "-q", "-b", "main"}));
        writeFile(repo, "a.txt", "1\n");
        runGit(repo, {"add", "-A"});
        runGit(repo, {"-c", "user.name=T", "-c", "user.email=t@t",
                      "commit", "-q", "-m", "base"});
        QProcess head;
        head.setWorkingDirectory(repo);
        head.start("git", {"rev-parse", "HEAD"});
        QVERIFY(head.waitForFinished(15000));
        const QString hash =
            QString::fromUtf8(head.readAllStandardOutput()).trimmed();

        GitTagService service;
        service.setWorkingDirectory(repo);
        bool created = false;
        QEventLoop cLoop;
        service.createTagAsync(QStringLiteral("v1.0"),
                               hash, QString(), false,
                               [&](const GitTagService::OperationResult &r) {
                                   created = r.success;
                                   cLoop.quit();
                               });
        waitLoop(cLoop);
        QVERIFY2(created, "lightweight tag create must succeed");

        bool annCreated = false;
        QEventLoop aLoop;
        service.createTagAsync(QStringLiteral("v2.0"),
                               hash, QStringLiteral("v2.0 release"), true,
                               [&](const GitTagService::OperationResult &r) {
                                   annCreated = r.success;
                                   aLoop.quit();
                               });
        waitLoop(aLoop);
        QVERIFY2(annCreated, "annotated tag create must succeed");

        bool deleted = false;
        QEventLoop dLoop;
        service.deleteTagAsync(QStringLiteral("v1.0"),
                               [&](const GitTagService::OperationResult &r) {
                                   deleted = r.success;
                                   dLoop.quit();
                               });
        waitLoop(dLoop);
        QVERIFY2(deleted, "tag delete must succeed");
        QDir(repo).removeRecursively();
    }
};

QTEST_MAIN(TestAcceptanceP619)
#include "test_acceptance_p619.moc"
