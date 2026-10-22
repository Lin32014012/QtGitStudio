#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include "GitBranchService.h"

class TestBranchService : public QObject {
    Q_OBJECT
private:
    bool runGit(const QString &workingDir, const QStringList &args) {
        QProcess proc;
        proc.setWorkingDirectory(workingDir);
        proc.start("git", args);
        proc.waitForFinished(5000);
        return proc.exitCode() == 0;
    }

    QString gitOutput(const QString &workingDir, const QStringList &args) {
        QProcess proc;
        proc.setWorkingDirectory(workingDir);
        proc.start("git", args);
        if (!proc.waitForFinished(5000) || proc.exitCode() != 0) {
            return {};
        }
        return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    }

    bool initializeRepository(const QString &path) {
        if (!runGit(path, {"init"}) ||
            !runGit(path, {"config", "user.name", "Test"}) ||
            !runGit(path, {"config", "user.email", "test@test.com"})) {
            return false;
        }

        QFile file(path + "/README.md");
        if (!file.open(QIODevice::WriteOnly) || file.write("# Test") != 6) {
            return false;
        }
        file.close();
        return runGit(path, {"add", "."}) && runGit(path, {"commit", "-m", "init"});
    }
    
private slots:
    void testListBranches();
    void testCreateBranch();
    void testValidateBranchNameAsync();
    void testCreateFromStartPointAsync();
    void testCreateAndCheckoutFromStartPointAsync();
    void testWorkspaceStatusAsync();
    void testWorkspaceConflictStatusAsync();
    void testCheckoutBranch();
    void testCheckoutBranchAsync();
    void testCheckoutFailurePreservesGitError();
    void testDeleteMergedBranch();
    void testRejectAndForceDeleteUnmergedBranch();
    void testRejectDeletingCurrentBranch();
    void testInvalidBranchName();
};

void TestBranchService::testListBranches() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    
    QVERIFY(initializeRepository(dir.path()));
    
    // Create develop branch
    runGit(dir.path(), {"branch", "develop"});
    
    GitBranchService service;
    service.setWorkingDirectory(dir.path());
    
    QStringList branches = service.branches();
    QVERIFY(branches.contains("main"));
    QVERIFY(branches.contains("develop"));
}

void TestBranchService::testCreateBranch() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    
    QVERIFY(initializeRepository(dir.path()));
    
    GitBranchService service;
    service.setWorkingDirectory(dir.path());
    
    QVERIFY(service.createBranch("feature/demo"));
    QVERIFY(service.branches().contains("feature/demo"));
    QCOMPARE(service.currentBranch(), QString("main"));
}

void TestBranchService::testValidateBranchNameAsync() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(initializeRepository(dir.path()));

    GitBranchService service;
    service.setWorkingDirectory(dir.path());

    bool validCompleted = false;
    GitBranchService::OperationResult validResult;
    service.validateBranchNameAsync(
        "feature/branch-dialog",
        [&](GitBranchService::OperationResult result) {
            validCompleted = true;
            validResult = result;
        });
    QVERIFY(!validCompleted);
    QTRY_VERIFY(validCompleted);
    QVERIFY(validResult.success);

    bool invalidCompleted = false;
    GitBranchService::OperationResult invalidResult;
    service.validateBranchNameAsync(
        "feature..invalid",
        [&](GitBranchService::OperationResult result) {
            invalidCompleted = true;
            invalidResult = result;
        });
    QVERIFY(!invalidCompleted);
    QTRY_VERIFY(invalidCompleted);
    QVERIFY(!invalidResult.success);
    QVERIFY(invalidResult.error.hasError());
    QVERIFY(!invalidResult.error.stderrOutput.trimmed().isEmpty());
}

void TestBranchService::testCreateFromStartPointAsync() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(initializeRepository(dir.path()));
    QVERIFY(runGit(dir.path(), {"switch", "-c", "release/base"}));

    QFile file(dir.path() + "/release.txt");
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("release base"), qint64(12));
    file.close();
    QVERIFY(runGit(dir.path(), {"add", "release.txt"}));
    QVERIFY(runGit(dir.path(), {"commit", "-m", "release base"}));
    const QString startCommit = gitOutput(dir.path(), {"rev-parse", "release/base"});
    QVERIFY(!startCommit.isEmpty());
    QVERIFY(runGit(dir.path(), {"switch", "main"}));

    GitBranchService service;
    service.setWorkingDirectory(dir.path());
    bool completed = false;
    GitBranchService::OperationResult result;
    service.createBranchAsync(
        "feature/from-release",
        "release/base",
        false,
        [&](GitBranchService::OperationResult operationResult) {
            completed = true;
            result = operationResult;
        });

    QVERIFY(!completed);
    QTRY_VERIFY(completed);
    QVERIFY(result.success);
    QCOMPARE(service.currentBranch(), QString("main"));
    QCOMPARE(gitOutput(dir.path(), {"rev-parse", "feature/from-release"}), startCommit);
}

void TestBranchService::testCreateAndCheckoutFromStartPointAsync() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(initializeRepository(dir.path()));
    QVERIFY(runGit(dir.path(), {"branch", "develop"}));

    GitBranchService service;
    service.setWorkingDirectory(dir.path());
    bool completed = false;
    GitBranchService::OperationResult result;
    service.createBranchAsync(
        "feature/checked-out",
        "develop",
        true,
        [&](GitBranchService::OperationResult operationResult) {
            completed = true;
            result = operationResult;
        });

    QVERIFY(!completed);
    QTRY_VERIFY(completed);
    QVERIFY(result.success);
    QCOMPARE(service.currentBranch(), QString("feature/checked-out"));
}

void TestBranchService::testWorkspaceStatusAsync() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(initializeRepository(dir.path()));

    GitBranchService service;
    service.setWorkingDirectory(dir.path());
    bool completed = false;
    GitBranchService::WorkspaceStatus cleanStatus;
    service.workspaceStatusAsync([&](GitBranchService::WorkspaceStatus status) {
        completed = true;
        cleanStatus = status;
    });
    QVERIFY(!completed);
    QTRY_VERIFY(completed);
    QVERIFY(cleanStatus.success);
    QVERIFY(cleanStatus.isClean());

    QFile readme(dir.path() + "/README.md");
    QVERIFY(readme.open(QIODevice::WriteOnly));
    QCOMPARE(readme.write("modified"), qint64(8));
    readme.close();
    QFile staged(dir.path() + "/staged.txt");
    QVERIFY(staged.open(QIODevice::WriteOnly));
    QCOMPARE(staged.write("staged"), qint64(6));
    staged.close();
    QVERIFY(runGit(dir.path(), {"add", "staged.txt"}));
    QFile untracked(dir.path() + "/untracked.txt");
    QVERIFY(untracked.open(QIODevice::WriteOnly));
    QCOMPARE(untracked.write("untracked"), qint64(9));
    untracked.close();

    completed = false;
    GitBranchService::WorkspaceStatus dirtyStatus;
    service.workspaceStatusAsync([&](GitBranchService::WorkspaceStatus status) {
        completed = true;
        dirtyStatus = status;
    });
    QTRY_VERIFY(completed);
    QVERIFY(dirtyStatus.success);
    QVERIFY(!dirtyStatus.isClean());
    QVERIFY(dirtyStatus.hasTrackedChanges);
    QVERIFY(dirtyStatus.hasStagedChanges);
    QVERIFY(dirtyStatus.hasUntrackedFiles);
    QVERIFY(!dirtyStatus.hasConflicts);
    QVERIFY(dirtyStatus.rawPorcelain.contains("README.md"));
    QVERIFY(dirtyStatus.rawPorcelain.contains("staged.txt"));
    QVERIFY(dirtyStatus.rawPorcelain.contains("untracked.txt"));
}

void TestBranchService::testWorkspaceConflictStatusAsync() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(initializeRepository(dir.path()));
    QVERIFY(runGit(dir.path(), {"switch", "-c", "conflict-source"}));

    QFile readme(dir.path() + "/README.md");
    QVERIFY(readme.open(QIODevice::WriteOnly));
    QCOMPARE(readme.write("source side"), qint64(11));
    readme.close();
    QVERIFY(runGit(dir.path(), {"add", "README.md"}));
    QVERIFY(runGit(dir.path(), {"commit", "-m", "source side"}));
    QVERIFY(runGit(dir.path(), {"switch", "main"}));
    QVERIFY(readme.open(QIODevice::WriteOnly));
    QCOMPARE(readme.write("main side"), qint64(9));
    readme.close();
    QVERIFY(runGit(dir.path(), {"add", "README.md"}));
    QVERIFY(runGit(dir.path(), {"commit", "-m", "main side"}));
    QVERIFY(!runGit(dir.path(), {"merge", "conflict-source"}));

    GitBranchService service;
    service.setWorkingDirectory(dir.path());
    bool completed = false;
    GitBranchService::WorkspaceStatus conflictStatus;
    service.workspaceStatusAsync([&](GitBranchService::WorkspaceStatus status) {
        completed = true;
        conflictStatus = status;
    });
    QTRY_VERIFY(completed);
    QVERIFY(conflictStatus.success);
    QVERIFY(conflictStatus.hasConflicts);
    QVERIFY(conflictStatus.hasTrackedChanges);
    QVERIFY(!conflictStatus.isClean());
}

void TestBranchService::testCheckoutBranch() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    
    QVERIFY(initializeRepository(dir.path()));
    
    // Create and checkout feature branch
    runGit(dir.path(), {"branch", "feature/demo"});
    
    GitBranchService service;
    service.setWorkingDirectory(dir.path());
    
    QVERIFY(service.checkoutBranch("feature/demo"));
    QCOMPARE(service.currentBranch(), QString("feature/demo"));
}

void TestBranchService::testCheckoutBranchAsync() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(initializeRepository(dir.path()));
    QVERIFY(runGit(dir.path(), {"branch", "feature/async"}));

    GitBranchService service;
    service.setWorkingDirectory(dir.path());
    bool completed = false;
    GitBranchService::OperationResult result;
    service.checkoutBranchAsync(
        "feature/async",
        [&](GitBranchService::OperationResult operationResult) {
            completed = true;
            result = operationResult;
        });

    QVERIFY(!completed);
    QTRY_VERIFY(completed);
    QVERIFY(result.success);
    QCOMPARE(service.currentBranch(), QString("feature/async"));
}

void TestBranchService::testCheckoutFailurePreservesGitError() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(initializeRepository(dir.path()));
    QVERIFY(runGit(dir.path(), {"switch", "-c", "conflicting"}));

    QFile file(dir.path() + "/README.md");
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("branch version"), qint64(14));
    file.close();
    QVERIFY(runGit(dir.path(), {"add", "README.md"}));
    QVERIFY(runGit(dir.path(), {"commit", "-m", "branch version"}));
    QVERIFY(runGit(dir.path(), {"switch", "main"}));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("local changes"), qint64(13));
    file.close();

    GitBranchService service;
    service.setWorkingDirectory(dir.path());
    QVERIFY(!service.checkoutBranch("conflicting"));
    QVERIFY(service.lastError().stderrOutput.contains("would be overwritten", Qt::CaseInsensitive));
    QCOMPARE(service.currentBranch(), QString("main"));
}

void TestBranchService::testDeleteMergedBranch() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(initializeRepository(dir.path()));
    QVERIFY(runGit(dir.path(), {"branch", "merged"}));

    GitBranchService service;
    service.setWorkingDirectory(dir.path());
    QVERIFY(service.deleteBranch("merged"));
    QVERIFY(!service.branches().contains("merged"));
}

void TestBranchService::testRejectAndForceDeleteUnmergedBranch() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(initializeRepository(dir.path()));
    QVERIFY(runGit(dir.path(), {"switch", "-c", "unmerged"}));

    QFile file(dir.path() + "/unmerged.txt");
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("unmerged"), qint64(8));
    file.close();
    QVERIFY(runGit(dir.path(), {"add", "unmerged.txt"}));
    QVERIFY(runGit(dir.path(), {"commit", "-m", "unmerged work"}));
    QVERIFY(runGit(dir.path(), {"switch", "main"}));

    GitBranchService service;
    service.setWorkingDirectory(dir.path());
    QVERIFY(!service.deleteBranch("unmerged"));
    QVERIFY(service.lastDeleteRejectedAsUnmerged());
    QVERIFY(service.lastError().stderrOutput.contains("not fully merged", Qt::CaseInsensitive));
    QVERIFY(service.branches().contains("unmerged"));

    QVERIFY(service.deleteBranch("unmerged", true));
    QVERIFY(!service.branches().contains("unmerged"));
}

void TestBranchService::testRejectDeletingCurrentBranch() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(initializeRepository(dir.path()));

    GitBranchService service;
    service.setWorkingDirectory(dir.path());
    QVERIFY(!service.deleteBranch("main", true));
    QVERIFY(service.branches().contains("main"));
}

void TestBranchService::testInvalidBranchName() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    
    QVERIFY(initializeRepository(dir.path()));
    
    GitBranchService service;
    service.setWorkingDirectory(dir.path());
    
    QVERIFY(!service.createBranch("abc..test"));
    QVERIFY(service.lastError().hasError());
}

QTEST_MAIN(TestBranchService)
#include "test_branch_service.moc"
