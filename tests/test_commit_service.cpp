#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include "GitCommitService.h"

class TestCommitService : public QObject {
    Q_OBJECT
private:
    bool runGit(const QString &workingDir, const QStringList &args) {
        QProcess proc;
        proc.setWorkingDirectory(workingDir);
        proc.start("git", args);
        proc.waitForFinished(5000);
        return proc.exitCode() == 0;
    }

    QByteArray gitOutput(const QString &workingDir, const QStringList &args, int *exitCode = nullptr) {
        QProcess proc;
        proc.setWorkingDirectory(workingDir);
        proc.start("git", args);
        if (!proc.waitForFinished(5000)) {
            if (exitCode) *exitCode = -1;
            return {};
        }
        if (exitCode) *exitCode = proc.exitCode();
        return proc.readAllStandardOutput();
    }
    
private slots:
    void testStageFiles();
    void testCommitSuccess();
    void testEmptyCommitMessage();
    void testNoFilesToStage();
    void testUnstageNewFileWithHead();
    void testUnstageTrackedModification();
    void testUnstageWithoutHead();
    void testUnstageWithoutHeadAsync();
    void testUnstagePathBoundaries();
};

void TestCommitService::testStageFiles() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    
    runGit(dir.path(), {"init"});
    runGit(dir.path(), {"config", "user.name", "Test"});
    runGit(dir.path(), {"config", "user.email", "test@test.com"});
    
    QFile file(dir.path() + "/test.txt");
    file.open(QIODevice::WriteOnly);
    file.write("initial");
    file.close();
    
    GitCommitService service;
    service.setWorkingDirectory(dir.path());
    
    QVERIFY(service.stageFiles({"test.txt"}));
}

void TestCommitService::testCommitSuccess() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    
    runGit(dir.path(), {"init"});
    runGit(dir.path(), {"config", "user.name", "Test"});
    runGit(dir.path(), {"config", "user.email", "test@test.com"});
    
    QFile file(dir.path() + "/README.md");
    file.open(QIODevice::WriteOnly);
    file.write("# Test");
    file.close();
    
    runGit(dir.path(), {"add", "."});
    
    GitCommitService service;
    service.setWorkingDirectory(dir.path());
    
    QVERIFY(service.commit("initial commit"));
    
    QProcess proc;
    proc.setWorkingDirectory(dir.path());
    proc.start("git", {"log", "-1", "--pretty=format:%s"});
    proc.waitForFinished(5000);
    QString output = QString::fromUtf8(proc.readAllStandardOutput());
    QCOMPARE(output, QString("initial commit"));
}

void TestCommitService::testEmptyCommitMessage() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    
    runGit(dir.path(), {"init"});
    runGit(dir.path(), {"config", "user.name", "Test"});
    runGit(dir.path(), {"config", "user.email", "test@test.com"});
    
    GitCommitService service;
    service.setWorkingDirectory(dir.path());
    
    QVERIFY(!service.commit(""));
    QVERIFY(service.lastError().hasError());
}

void TestCommitService::testNoFilesToStage() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    
    runGit(dir.path(), {"init"});
    runGit(dir.path(), {"config", "user.name", "Test"});
    runGit(dir.path(), {"config", "user.email", "test@test.com"});
    
    GitCommitService service;
    service.setWorkingDirectory(dir.path());
    
    QVERIFY(!service.stageFiles({}));
    QVERIFY(service.lastError().hasError());
}

void TestCommitService::testUnstageNewFileWithHead() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(runGit(dir.path(), {"init"}));
    QVERIFY(runGit(dir.path(), {"config", "user.name", "Test"}));
    QVERIFY(runGit(dir.path(), {"config", "user.email", "test@test.com"}));
    QVERIFY(runGit(dir.path(), {"commit", "--allow-empty", "-m", "initial"}));

    QFile file(dir.filePath("stage-commit-uat.txt"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("new file content"), qint64(16));
    file.close();

    GitCommitService service;
    service.setWorkingDirectory(dir.path());
    QVERIFY(service.stageFiles({"stage-commit-uat.txt"}));
    QCOMPARE(service.stagedFiles(), QStringList{"stage-commit-uat.txt"});
    QCOMPARE(gitOutput(dir.path(), {"status", "--porcelain=v1"}).trimmed(), QByteArray("A  stage-commit-uat.txt"));

    QVERIFY(service.unstageFiles(service.stagedFiles()));
    QCOMPARE(gitOutput(dir.path(), {"status", "--porcelain=v1"}).trimmed(), QByteArray("?? stage-commit-uat.txt"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("new file content"));
}

void TestCommitService::testUnstageTrackedModification() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(runGit(dir.path(), {"init"}));
    QVERIFY(runGit(dir.path(), {"config", "user.name", "Test"}));
    QVERIFY(runGit(dir.path(), {"config", "user.email", "test@test.com"}));

    QFile file(dir.filePath("tracked-file.txt"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("initial"), qint64(7));
    file.close();
    QVERIFY(runGit(dir.path(), {"add", "--", "tracked-file.txt"}));
    QVERIFY(runGit(dir.path(), {"commit", "-m", "initial"}));

    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Append));
    QCOMPARE(file.write(" modified"), qint64(9));
    file.close();

    GitCommitService service;
    service.setWorkingDirectory(dir.path());
    QVERIFY(service.stageFiles({"tracked-file.txt"}));
    QCOMPARE(gitOutput(dir.path(), {"status", "--porcelain=v1"}).trimmed(), QByteArray("M  tracked-file.txt"));
    QVERIFY(service.unstageFiles(service.stagedFiles()));
    QCOMPARE(gitOutput(dir.path(), {"status", "--porcelain=v1"}), QByteArray(" M tracked-file.txt\n"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("initial modified"));
}

void TestCommitService::testUnstageWithoutHead() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(runGit(dir.path(), {"init"}));

    QFile file(dir.filePath("unborn.txt"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("unborn content"), qint64(14));
    file.close();

    GitCommitService service;
    service.setWorkingDirectory(dir.path());
    QVERIFY(service.stageFiles({"unborn.txt"}));
    QCOMPARE(gitOutput(dir.path(), {"status", "--porcelain=v1"}).trimmed(), QByteArray("A  unborn.txt"));
    QVERIFY(service.unstageFiles(service.stagedFiles()));
    QCOMPARE(gitOutput(dir.path(), {"status", "--porcelain=v1"}).trimmed(), QByteArray("?? unborn.txt"));
    QVERIFY(QFile::exists(file.fileName()));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("unborn content"));
}

void TestCommitService::testUnstageWithoutHeadAsync() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(runGit(dir.path(), {"init"}));

    QFile file(dir.filePath("async-unborn.txt"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("keep this content"), qint64(17));
    file.close();
    QVERIFY(runGit(dir.path(), {"add", "--", "async-unborn.txt"}));

    GitCommitService service;
    service.setWorkingDirectory(dir.path());
    bool completed = false;
    GitCommitService::OperationResult result;
    service.unstageFilesAsync(
        {"async-unborn.txt"},
        [&](GitCommitService::OperationResult operationResult) {
            completed = true;
            result = operationResult;
        });

    QVERIFY(!completed);
    QTRY_VERIFY(completed);
    QVERIFY(result.success);
    QCOMPARE(gitOutput(dir.path(), {"status", "--porcelain=v1"}).trimmed(), QByteArray("?? async-unborn.txt"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("keep this content"));
}

void TestCommitService::testUnstagePathBoundaries() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(runGit(dir.path(), {"init"}));
    QVERIFY(runGit(dir.path(), {"config", "user.name", "Test"}));
    QVERIFY(runGit(dir.path(), {"config", "user.email", "test@test.com"}));

    QFile oldFile(dir.filePath("old name.txt"));
    QFile deletedFile(dir.filePath("中文文件.txt"));
    QVERIFY(oldFile.open(QIODevice::WriteOnly));
    QCOMPARE(oldFile.write("rename content"), qint64(14));
    oldFile.close();
    QVERIFY(deletedFile.open(QIODevice::WriteOnly));
    QCOMPARE(deletedFile.write("delete content"), qint64(14));
    deletedFile.close();
    QVERIFY(runGit(dir.path(), {"add", "--", "old name.txt", "中文文件.txt"}));
    QVERIFY(runGit(dir.path(), {"commit", "-m", "initial"}));

    QVERIFY(QFile::rename(dir.filePath("old name.txt"), dir.filePath("new name.txt")));
    QVERIFY(QFile::remove(dir.filePath("中文文件.txt")));
    QFile addedFile(dir.filePath("新增 文件.txt"));
    QVERIFY(addedFile.open(QIODevice::WriteOnly));
    QCOMPARE(addedFile.write("added content"), qint64(13));
    addedFile.close();
    QVERIFY(runGit(dir.path(), {"add", "-A"}));

    GitCommitService service;
    service.setWorkingDirectory(dir.path());
    const QStringList staged = service.stagedFiles();
    QCOMPARE(staged.size(), 4);
    QVERIFY(staged.contains("old name.txt"));
    QVERIFY(staged.contains("new name.txt"));
    QVERIFY(staged.contains("中文文件.txt"));
    QVERIFY(staged.contains("新增 文件.txt"));
    QVERIFY(service.unstageFiles(staged));
    QVERIFY(service.stagedFiles().isEmpty());
    QVERIFY(QFile::exists(dir.filePath("new name.txt")));
    QVERIFY(QFile::exists(dir.filePath("新增 文件.txt")));
    QVERIFY(!QFile::exists(dir.filePath("中文文件.txt")));
}

QTEST_MAIN(TestCommitService)
#include "test_commit_service.moc"
