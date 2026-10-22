#include <QtTest>
#include <QProcess>
#include <QFile>
#include <QSignalSpy>
#include <QDebug>
#include "GitController.h"
#include "GitViewModel.h"
#include "GitTestRepository.h"

class TestController : public QObject {
    Q_OBJECT
private:
    GitTestRepository m_repo;
    
private slots:
    void initTestCase();
    void testControllerCreation();
    void testSetRepositoryPath();
    void testRefresh();
    void testSnapshotUpdate();
    void testBranchIsMain();
    void testBranchErrorIsExposed();
    void testUnstageRefreshesSnapshot();
    void testAsyncStageAndUnstageRefreshSnapshot();
    void testModifiedFile();
    void testErrorSignal();
    void testLoadingState();
    void cleanupTestCase();
};

void TestController::initTestCase() {
    QVERIFY(m_repo.isValid());
}

void TestController::testControllerCreation() {
    GitController controller;
    QVERIFY(controller.viewModel() != nullptr);
}

void TestController::testSetRepositoryPath() {
    GitController controller;
    controller.setRepositoryPath(m_repo.path());
    QCOMPARE(controller.viewModel()->isLoading(), false);
}

void TestController::testRefresh() {
    GitController controller;
    controller.setRepositoryPath(m_repo.path());
    QSignalSpy spy(controller.viewModel(), &GitViewModel::snapshotChanged);
    controller.refresh();
    QCOMPARE(spy.count(), 1);
}

void TestController::testSnapshotUpdate() {
    GitController controller;
    controller.setRepositoryPath(m_repo.path());
    controller.refresh();
    auto snap = controller.viewModel()->snapshot();
    QVERIFY(!snap.branches.isEmpty());
}

void TestController::testBranchIsMain() {
    GitController controller;
    controller.setRepositoryPath(m_repo.path());
    controller.refresh();
    auto snap = controller.viewModel()->snapshot();
    bool foundMain = false;
    for (const auto &b : snap.branches) {
        if (b.isCurrent && b.name == "main") {
            foundMain = true;
            break;
        }
    }
    QVERIFY(foundMain);
}

void TestController::testBranchErrorIsExposed() {
    GitController controller;
    controller.setRepositoryPath(m_repo.path());
    QVERIFY(!controller.checkoutBranch("missing-branch"));
    QVERIFY(controller.viewModel()->error().contains("missing-branch"));
}

void TestController::testUnstageRefreshesSnapshot() {
    QFile file(m_repo.path() + "/controller-unstage.txt");
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("controller content"), qint64(18));
    file.close();

    GitController controller;
    controller.setRepositoryPath(m_repo.path());
    controller.refresh();
    QVERIFY(controller.stageFiles({"controller-unstage.txt"}));
    QCOMPARE(controller.stagedFiles(), QStringList{"controller-unstage.txt"});

    QSignalSpy snapshotSpy(controller.viewModel(), &GitViewModel::snapshotChanged);
    QVERIFY(controller.unstageFiles(controller.stagedFiles()));
    QCOMPARE(snapshotSpy.count(), 1);
    QVERIFY(controller.stagedFiles().isEmpty());
    QVERIFY(QFile::exists(file.fileName()));
}

void TestController::testAsyncStageAndUnstageRefreshSnapshot() {
    QFile file(m_repo.path() + "/controller-async.txt");
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("async controller content"), qint64(24));
    file.close();

    GitController controller;
    controller.setRepositoryPath(m_repo.path());
    bool stageCompleted = false;
    GitController::OperationResult stageResult;
    controller.stageFilesAsync(
        {"controller-async.txt"},
        [&](GitController::OperationResult result) {
            stageCompleted = true;
            stageResult = result;
        });
    QVERIFY(!stageCompleted);
    QTRY_VERIFY(stageCompleted);
    QVERIFY(stageResult.success);
    QCOMPARE(controller.stagedFiles(), QStringList{"controller-async.txt"});

    bool unstageCompleted = false;
    GitController::OperationResult unstageResult;
    controller.unstageAllAsync([&](GitController::OperationResult result) {
        unstageCompleted = true;
        unstageResult = result;
    });
    QVERIFY(!unstageCompleted);
    QTRY_VERIFY(unstageCompleted);
    QVERIFY(unstageResult.success);
    QVERIFY(controller.stagedFiles().isEmpty());
    QVERIFY(QFile::exists(file.fileName()));
}

void TestController::testModifiedFile() {
    QFile file(m_repo.path() + "/modified.txt");
    file.open(QIODevice::WriteOnly);
    file.write("content");
    file.close();
    
    QProcess proc;
    proc.setWorkingDirectory(m_repo.path());
    proc.start("git", {"add", "modified.txt"});
    proc.waitForFinished(5000);
    proc.start("git", {"commit", "-m", "add"});
    proc.waitForFinished(5000);
    
    file.open(QIODevice::WriteOnly | QIODevice::Append);
    file.write("\nmore");
    file.close();
    
    GitController controller;
    controller.setRepositoryPath(m_repo.path());
    controller.refresh();
    auto snap = controller.viewModel()->snapshot();
    bool foundModified = false;
    for (const auto &f : snap.files) {
        if (f.path.contains("modified.txt")) {
            foundModified = true;
            break;
        }
    }
    QVERIFY(foundModified);
}

void TestController::testErrorSignal() {
    GitController controller;
    QSignalSpy errorSpy(controller.viewModel(), &GitViewModel::errorChanged);
    controller.setRepositoryPath("C:/nonexistent_path_12345");
    controller.refresh();
    QVERIFY(errorSpy.count() >= 1);
    QVERIFY(!controller.viewModel()->error().isEmpty());
}

void TestController::testLoadingState() {
    GitController controller;
    controller.setRepositoryPath(m_repo.path());
    QSignalSpy loadingSpy(controller.viewModel(), &GitViewModel::loadingChanged);
    controller.refresh();
    QVERIFY(loadingSpy.count() >= 2);
}

void TestController::cleanupTestCase() {}

QTEST_MAIN(TestController)
#include "test_controller.moc"
