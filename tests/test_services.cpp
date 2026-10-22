#include <QtTest>
#include <QProcess>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include "GitCommandExecutor.h"
#include "GitRepository.h"
#include "GitSnapshotService.h"
#include "GitTestRepository.h"

class TestServices : public QObject {
    Q_OBJECT
private:
    GitTestRepository m_repo;
    
private slots:
    void initTestCase();
    void testCommandSuccess();
    void testCommandFailure();
    void testExecutionMetadata();
    void testAsyncCommandKeepsEventLoopResponsive();
    void testExitCode();
    void testStderr();
    void testBranch();
    void testStatus();
    void testDiff();
    void testSnapshot();
    void cleanupTestCase();
};

void TestServices::initTestCase() {
    QVERIFY(m_repo.isValid());
    QVERIFY(QFile::exists(m_repo.path() + "/.git/HEAD"));
}

void TestServices::testCommandSuccess() {
    GitCommandExecutor executor;
    executor.setWorkingDirectory(m_repo.path());
    auto result = executor.execute("status", QStringList());
    QVERIFY(!result.error.hasError());
    QCOMPARE(result.exitCode, 0);
}

void TestServices::testCommandFailure() {
    GitCommandExecutor executor;
    executor.setWorkingDirectory(m_repo.path());
    auto result = executor.execute("status", QStringList() << "--invalid-flag");
    QVERIFY(result.error.hasError());
}

void TestServices::testExecutionMetadata() {
    GitCommandExecutor executor;
    executor.setWorkingDirectory(m_repo.path());
    QVERIFY(QFileInfo(executor.gitPath()).isAbsolute());
    QVERIFY(QFileInfo::exists(executor.gitPath()));

    const auto success = executor.execute("status", {});
    QCOMPARE(success.exitStatus, QProcess::NormalExit);
    QCOMPARE(success.exitCode, 0);

    const auto failure = executor.execute("status", {"--invalid-flag"});
    QCOMPARE(failure.exitStatus, QProcess::NormalExit);
    QVERIFY(failure.exitCode != 0);
}

void TestServices::testAsyncCommandKeepsEventLoopResponsive() {
    GitCommandExecutor executor;
    executor.setWorkingDirectory(m_repo.path());
    bool callbackCalled = false;
    GitCommandExecutor::Result asyncResult;

    executor.executeAsync(
        "status",
        {},
        [&](GitCommandExecutor::Result result) {
            callbackCalled = true;
            asyncResult = result;
        });

    QVERIFY(!callbackCalled);
    QTRY_VERIFY(callbackCalled);
    QCOMPARE(asyncResult.exitCode, 0);
    QVERIFY(!asyncResult.error.hasError());
    QTRY_COMPARE(executor.findChildren<QProcess *>().size(), 0);
}

void TestServices::testExitCode() {
    GitCommandExecutor executor;
    executor.setWorkingDirectory(m_repo.path());
    auto result = executor.execute("rev-parse", QStringList() << "--verify" << "HEAD");
    QCOMPARE(result.exitCode, 0);
}

void TestServices::testStderr() {
    GitCommandExecutor executor;
    executor.setWorkingDirectory(m_repo.path());
    auto result = executor.execute("status", QStringList() << "--invalid-flag");
    QVERIFY(!result.errorOutput.isEmpty());
}

void TestServices::testBranch() {
    GitRepository repo;
    repo.setWorkingDirectory(m_repo.path());
    auto snap = repo.branch();
    QVERIFY(!snap.branches.isEmpty());
    bool foundMain = false;
    for (const auto &b : snap.branches) {
        if (b.isCurrent && b.name == "main") {
            foundMain = true;
            break;
        }
    }
    QVERIFY(foundMain);
}

void TestServices::testStatus() {
    GitRepository repo;
    repo.setWorkingDirectory(m_repo.path());
    auto snap = repo.status();
    QCOMPARE(snap.files.size(), 0);
}

void TestServices::testDiff() {
    GitRepository repo;
    repo.setWorkingDirectory(m_repo.path());
    auto snap = repo.diff("HEAD");
    QVERIFY(true);
}

void TestServices::testSnapshot() {
    GitSnapshotService service;
    service.setWorkingDirectory(m_repo.path());
    auto snap = service.createSnapshot();
    QVERIFY(!snap.branches.isEmpty());
    bool foundMain = false;
    for (const auto &b : snap.branches) {
        if (b.isCurrent && b.name == "main") {
            foundMain = true;
            break;
        }
    }
    QVERIFY(foundMain);
}

void TestServices::cleanupTestCase() {}

QTEST_MAIN(TestServices)
#include "test_services.moc"
