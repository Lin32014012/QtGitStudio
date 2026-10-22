#include <QtTest>

#include <QDir>
#include <QProcess>
#include <QPushButton>
#include <QStackedWidget>
#include <QTemporaryDir>

#include "GitController.h"
#include "GitRemoteService.h"
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

class DelayedRemoteService final : public GitRemoteService {
public:
    using GitRemoteService::GitRemoteService;

    struct Pending {
        QString operation;
        QString repository;
        OperationCompletion completion;
    };

    QList<Pending> pending;
    QStringList starts;

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
        hold(QStringLiteral("fetch"), completion);
    }
    void pullAsync(OperationCompletion completion) override {
        hold(QStringLiteral("pull"), completion);
    }
    void pushAsync(OperationCompletion completion) override {
        hold(QStringLiteral("push"), completion);
    }

    void hold(const QString &operation, OperationCompletion completion) {
        const QString repository = workingDirectory();
        starts << operation + QStringLiteral("|") + repository;
        pending.append({operation, repository, completion});
    }

    bool complete(const QString &repository, const QString &operation) {
        for (int i = 0; i < pending.size(); ++i) {
            if (pending.at(i).repository != repository
                || pending.at(i).operation != operation) {
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

QPushButton *buttonFor(MainWindow &window, const QString &operation) {
    return window.findChild<QPushButton *>(operation + QStringLiteral("Button"));
}

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

class TestRemoteBusyRace : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { isolateTestSettings(); }

    void staleCompletionCannotClearCurrentBusy_data() {
        QTest::addColumn<QString>("operation");
        QTest::newRow("fetch") << QStringLiteral("fetch");
        QTest::newRow("pull") << QStringLiteral("pull");
        QTest::newRow("push") << QStringLiteral("push");
    }

    void staleCompletionCannotClearCurrentBusy() {
        QFETCH(QString, operation);
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString repoA = QDir(root.path()).filePath(QStringLiteral("repo-a"));
        const QString repoB = QDir(root.path()).filePath(QStringLiteral("repo-b"));
        QVERIFY(initRepository(repoA));
        QVERIFY(initRepository(repoB));

        MainWindow window;
        auto *remote = new DelayedRemoteService(window.controller());
        window.controller()->setRemoteServiceForTest(remote);
        openRepository(window, repoA);

        QPushButton *button = buttonFor(window, operation);
        QVERIFY(button != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(button->isEnabled(), 10000);
        QTest::mouseClick(button, Qt::LeftButton);
        QTRY_COMPARE(remote->pending.size(), 1);
        QCOMPARE(remote->pending.first().repository, repoA);
        QVERIFY(button->property("busy").toBool());

        openRepository(window, repoB);
        QTRY_VERIFY_WITH_TIMEOUT(button->isEnabled(), 10000);
        QTest::mouseClick(button, Qt::LeftButton);
        QTRY_COMPARE(remote->pending.size(), 2);
        QCOMPARE(remote->pending.last().repository, repoB);
        QVERIFY(button->property("busy").toBool());
        QVERIFY(!button->isEnabled());

        QVERIFY(remote->complete(repoA, operation));
        QApplication::processEvents();
        QCOMPARE(remote->pending.size(), 1);
        QCOMPARE(remote->pending.first().repository, repoB);
        QVERIFY(button->property("busy").toBool());
        QVERIFY(!button->isEnabled());

        QVERIFY(remote->complete(repoB, operation));
        QTRY_VERIFY_WITH_TIMEOUT(!button->property("busy").toBool(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(button->isEnabled(), 5000);
        QVERIFY(remote->pending.isEmpty());
        qInfo().noquote()
            << QStringLiteral("REMOTE_BUSY operation=%1 A=%2 B=%3 order=A-start,B-start,A-late,B-complete result=PASS")
                   .arg(operation, repoA, repoB);
    }
};

QTEST_MAIN(TestRemoteBusyRace)
#include "test_remote_busy_race.moc"
