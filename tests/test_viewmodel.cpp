#include <QtTest>
#include <QProcess>
#include <QFile>
#include <QSignalSpy>
#include "GitViewModel.h"
#include "GitTestRepository.h"

class TestViewModel : public QObject {
    Q_OBJECT
private:
    GitTestRepository m_repo;
    
private slots:
    void initTestCase();
    void testInitialState();
    void testRefresh();
    void testSnapshotUpdate();
    void testErrorPropagation();
    void testLoadingState();
    void cleanupTestCase();
};

void TestViewModel::initTestCase() {
    QVERIFY(m_repo.isValid());
}

void TestViewModel::testInitialState() {
    GitViewModel vm;
    vm.setWorkingDirectory(m_repo.path());
    QVERIFY(!vm.isLoading());
    QVERIFY(vm.error().isEmpty());
}

void TestViewModel::testRefresh() {
    GitViewModel vm;
    vm.setWorkingDirectory(m_repo.path());
    QSignalSpy spy(&vm, &GitViewModel::snapshotChanged);
    vm.refresh();
    QCOMPARE(spy.count(), 1);
}

void TestViewModel::testSnapshotUpdate() {
    GitViewModel vm;
    vm.setWorkingDirectory(m_repo.path());
    vm.refresh();
    QVERIFY(!vm.snapshot().branches.isEmpty());
    bool foundMain = false;
    for (const auto &b : vm.snapshot().branches) {
        if (b.isCurrent && b.name == "main") {
            foundMain = true;
            break;
        }
    }
    QVERIFY(foundMain);
}

void TestViewModel::testErrorPropagation() {
    GitViewModel vm;
    vm.setWorkingDirectory("C:/nonexistent_path_12345");
    vm.refresh();
    QVERIFY(!vm.error().isEmpty());
}

void TestViewModel::testLoadingState() {
    GitViewModel vm;
    vm.setWorkingDirectory(m_repo.path());
    QSignalSpy spy(&vm, &GitViewModel::loadingChanged);
    vm.refresh();
    QVERIFY(spy.count() >= 2);
}

void TestViewModel::cleanupTestCase() {}

QTEST_MAIN(TestViewModel)
#include "test_viewmodel.moc"
