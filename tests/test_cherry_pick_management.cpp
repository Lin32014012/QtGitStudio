// v4-P4-1 Cherry-pick Management tests:
//   1. plain cherry-pick succeeds (change lands on current branch)
//   2. conflict detection (conflictedFiles parsed from git status)
//   3. abort (conflict state cleared, working tree restored)
//   4. empty repository (clean failure, no conflict flag)
// Every scenario uses its own temporary repository.
#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QEventLoop>
#include "GitCherryPickService.h"

class TestCherryPickManagement : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;

    bool runGit(const QString &dir, const QStringList &args,
                QString *output = nullptr, QString *errOutput = nullptr) {
        QProcess git;
        git.setWorkingDirectory(dir);
        git.start("git", args);
        if (!git.waitForFinished(15000)) return false;
        if (output) *output = QString::fromUtf8(git.readAllStandardOutput());
        if (errOutput) *errOutput = QString::fromUtf8(git.readAllStandardError());
        return git.exitStatus() == QProcess::NormalExit && git.exitCode() == 0;
    }

    QString mkRepo(const QString &tag) {
        const QString path = m_dir.path() + "/" + tag;
        QDir().mkpath(path);
        runGit(path, {"init", "-q", "-b", "main"});
        runGit(path, {"config", "user.name", "Tester"});
        runGit(path, {"config", "user.email", "t@t.local"});
        return path;
    }

    bool writeFile(const QString &dir, const QString &name,
                   const QByteArray &data) {
        QFile f(dir + "/" + name);
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write(data);
        f.close();
        return true;
    }

    bool commitAll(const QString &dir, const QString &message) {
        return runGit(dir, {"add", "-A"})
            && runGit(dir, {"commit", "-q", "-m", message});
    }

    QString headHash(const QString &dir) {
        QString out;
        runGit(dir, {"rev-parse", "HEAD"}, &out);
        return out.trimmed();
    }

    QString fileContent(const QString &dir, const QString &name) {
        QFile f(dir + "/" + name);
        if (!f.open(QIODevice::ReadOnly)) return QString();
        // Normalize CRLF (core.autocrlf on Windows) for content asserts.
        return QString::fromUtf8(f.readAll()).remove(QLatin1Char('\r'));
    }

    bool hasCherryPickHead(const QString &dir) {
        return QFile::exists(dir + "/.git/CHERRY_PICK_HEAD");
    }

    CherryPickResult awaitCherryPick(GitCherryPickService *service,
                                     const QString &hash) {
        CherryPickResult out;
        QEventLoop loop;
        service->cherryPickAsync(
            hash, [&out, &loop](const CherryPickResult &result) {
                out = result;
                loop.quit();
            });
        QTimer::singleShot(15000, &loop, &QEventLoop::quit);
        loop.exec();
        return out;
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
    }

    void plainCherryPickSuccess() {
        // main: base; topic: base + "topic change".
        const QString repo = mkRepo("cp_plain");
        writeFile(repo, "a.txt", "1\n");
        commitAll(repo, "base");
        runGit(repo, {"checkout", "-q", "-b", "topic"});
        writeFile(repo, "a.txt", "1\n2\n");
        commitAll(repo, "topic change");
        const QString topicHash = headHash(repo);
        runGit(repo, {"checkout", "-q", "main"});
        QCOMPARE(fileContent(repo, "a.txt"), QString("1\n"));

        GitCherryPickService service;
        service.setWorkingDirectory(repo);
        const CherryPickResult result = awaitCherryPick(&service, topicHash);
        QVERIFY2(result.success, qPrintable(result.message));
        QVERIFY(!result.conflict);
        QVERIFY(result.conflictedFiles.isEmpty());
        // The change landed on main; the pick commit exists on HEAD.
        QCOMPARE(fileContent(repo, "a.txt"), QString("1\n2\n"));
        QVERIFY(!hasCherryPickHead(repo));
    }

    void conflictDetection() {
        // main edits a.txt line 1; topic edits the same line differently.
        const QString repo = mkRepo("cp_conflict");
        writeFile(repo, "a.txt", "base\n");
        commitAll(repo, "base");
        runGit(repo, {"checkout", "-q", "-b", "topic"});
        writeFile(repo, "a.txt", "topic\n");
        commitAll(repo, "topic edit");
        const QString topicHash = headHash(repo);
        runGit(repo, {"checkout", "-q", "main"});
        writeFile(repo, "a.txt", "main\n");
        commitAll(repo, "main edit");

        GitCherryPickService service;
        service.setWorkingDirectory(repo);
        const CherryPickResult result = awaitCherryPick(&service, topicHash);
        QVERIFY(!result.success);
        QVERIFY(result.conflict);
        QVERIFY(result.conflictedFiles.contains(QStringLiteral("a.txt")));
        QVERIFY(hasCherryPickHead(repo));
    }

    void abortCherryPick() {
        const QString repo = mkRepo("cp_abort");
        writeFile(repo, "a.txt", "base\n");
        commitAll(repo, "base");
        runGit(repo, {"checkout", "-q", "-b", "topic"});
        writeFile(repo, "a.txt", "topic\n");
        commitAll(repo, "topic edit");
        const QString topicHash = headHash(repo);
        runGit(repo, {"checkout", "-q", "main"});
        writeFile(repo, "a.txt", "main\n");
        commitAll(repo, "main edit");
        // Record the pre-pick content to verify abort restores it.
        const QString before = fileContent(repo, "a.txt");
        QCOMPARE(before, QString("main\n"));

        GitCherryPickService service;
        service.setWorkingDirectory(repo);
        const CherryPickResult result = awaitCherryPick(&service, topicHash);
        QVERIFY(result.conflict);

        bool abortOk = false;
        QString abortError;
        QEventLoop loop;
        service.abortAsync([&](bool ok, const QString &error) {
            abortOk = ok;
            abortError = error;
            loop.quit();
        });
        QTimer::singleShot(15000, &loop, &QEventLoop::quit);
        loop.exec();
        QVERIFY2(abortOk, qPrintable(abortError));

        // Conflict state gone; working tree back to the pre-pick content.
        QVERIFY(!hasCherryPickHead(repo));
        QCOMPARE(fileContent(repo, "a.txt"), before);
        // No leftover unmerged entries.
        QString status;
        runGit(repo, {"status", "--porcelain"}, &status);
        QVERIFY(!status.contains("UU"));
        QVERIFY(!status.contains("AA"));
        QVERIFY(!status.contains("DD"));
    }

    void emptyRepository() {
        // No HEAD yet: cherry-pick must fail cleanly without a conflict.
        const QString repo = mkRepo("cp_empty");
        writeFile(repo, "a.txt", "1\n");

        GitCherryPickService service;
        service.setWorkingDirectory(repo);
        const CherryPickResult result =
            awaitCherryPick(&service, QStringLiteral("deadbeef"));
        QVERIFY(!result.success);
        QVERIFY(!result.conflict);
        QVERIFY(!result.message.isEmpty());
        QVERIFY(result.conflictedFiles.isEmpty());
    }
};

QTEST_MAIN(TestCherryPickManagement)
#include "test_cherry_pick_management.moc"
