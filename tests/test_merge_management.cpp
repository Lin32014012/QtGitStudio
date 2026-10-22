// v4-P1-4 Merge Support tests. Fresh temp repository per scenario:
//   1. fast-forward merge (target branch is ahead, no divergence)
//   2. normal merge commit (diverged histories)
//   3. conflict merge (same line changed on both sides)
//   4. merge abort (conflict started, then git merge --abort restores)
#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QDir>
#include "GitController.h"
#include "GitMergeService.h"

class TestMergeManagement : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;

    int runGit(const QString &dir, const QStringList &args,
               QString *out = nullptr) {
        QProcess git;
        git.setWorkingDirectory(dir);
        git.start("git", args);
        if (!git.waitForFinished(30000)) return -1;
        if (out) *out = QString::fromUtf8(git.readAllStandardOutput());
        return git.exitCode();
    }

    bool writeFile(const QString &dir, const QString &name, const QByteArray &data) {
        QFile f(dir + "/" + name);
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write(data);
        return true;
    }

    // Fresh repo on main with one commit.
    QString mkRepo(const QString &tag) {
        const QString path = m_dir.path() + "/" + tag;
        QDir().mkpath(path);
        const int rc = runGit(path, {"init", "-q", "-b", "main"});
        if (rc != 0) {
            qWarning() << "mkRepo init failed rc=" << rc;
        }
        runGit(path, {"config", "user.name", "Test"});
        runGit(path, {"config", "user.email", "t@t.local"});
        runGit(path, {"config", "pull.rebase", "false"});
        if (!writeFile(path, "file.txt", "line1\nline2\n")) {
            qWarning() << "mkRepo write failed";
        }
        runGit(path, {"add", "-A"});
        runGit(path, {"commit", "-q", "-m", "base"});
        return path;
    }

    // Create branch `name`, switch to it, add one commit, return to main.
    void makeBranchCommit(const QString &repo, const QString &name,
                          const QString &fileName, const QByteArray &content,
                          const QString &msg) {
        QVERIFY2(runGit(repo, {"checkout", "-q", "-b", name}) == 0, "co -b");
        QVERIFY2(writeFile(repo, fileName, content), "write");
        QVERIFY2(runGit(repo, {"add", "-A"}) == 0, "add");
        QVERIFY2(runGit(repo, {"commit", "-q", "-m", msg}) == 0, "commit");
        QVERIFY2(runGit(repo, {"checkout", "-q", "main"}) == 0, "co main");
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
    }

    void fastForwardMerge() {
        const QString repo = mkRepo("ff");
        // feature branch ahead of main by one commit (no divergence).
        makeBranchCommit(repo, "feature/ff", "extra.txt", "x\n", "feature");

        GitController c;
        c.setRepositoryPath(repo);
        bool done = false;
        c.mergeAsync("feature/ff", [&done](GitMergeService::MergeResult r) {
            QVERIFY(r.success);
            QVERIFY(r.fastForward);
            QVERIFY(!r.conflict);
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
        // Feature file now in the working tree.
        QVERIFY(QFile::exists(repo + "/extra.txt"));
    }

    void normalMergeCommit() {
        const QString repo = mkRepo("nm");
        // Diverge: main adds a commit, feature adds a different file.
        makeBranchCommit(repo, "feature/nm", "feat.txt", "f\n", "feature");
        QVERIFY2(writeFile(repo, "main.txt", "m\n"), "write main");
        QVERIFY2(runGit(repo, {"add", "-A"}) == 0, "add main");
        QVERIFY2(runGit(repo, {"commit", "-q", "-m", "main work"}) == 0, "commit main");

        GitController c;
        c.setRepositoryPath(repo);
        bool done = false;
        c.mergeAsync("feature/nm", [&done](GitMergeService::MergeResult r) {
            QVERIFY(r.success);
            QVERIFY(!r.fastForward);  // real merge commit
            QVERIFY(!r.conflict);
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
        QVERIFY(QFile::exists(repo + "/feat.txt"));
        QVERIFY(QFile::exists(repo + "/main.txt"));
        // A merge commit exists.
        QString log;
        QVERIFY2(runGit(repo, {"log", "--oneline", "-3"}, &log) == 0, "log");
        QVERIFY(log.contains("Merge"));
    }

    void conflictMerge() {
        const QString repo = mkRepo("cf");
        // Both sides edit the SAME line of file.txt.
        makeBranchCommit(repo, "feature/cf", "file.txt", "FEATURE line1\nline2\n",
                         "feature change");
        QVERIFY2(writeFile(repo, "file.txt", "MAIN line1\nline2\n"), "write main");
        QVERIFY2(runGit(repo, {"add", "-A"}) == 0, "add main");
        QVERIFY2(runGit(repo, {"commit", "-q", "-m", "main change"}) == 0, "commit main");

        GitController c;
        c.setRepositoryPath(repo);
        bool done = false;
        c.mergeAsync("feature/cf", [&done](GitMergeService::MergeResult r) {
            QVERIFY(!r.success);
            QVERIFY(r.conflict);  // surfaced, NEVER auto-resolved
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
        // Conflict markers in the working tree + UU status.
        QFile f(repo + "/file.txt");
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString content = QString::fromUtf8(f.readAll());
        QVERIFY(content.contains("<<<<<<<") && content.contains(">>>>>>>"));
        QString status;
        QVERIFY2(runGit(repo, {"status", "--porcelain=v1"}, &status) == 0, "status");
        QVERIFY(status.contains("UU file.txt"));
    }

    void mergeAbortRestores() {
        const QString repo = mkRepo("ab");
        makeBranchCommit(repo, "feature/ab", "file.txt", "FEATURE line1\nline2\n",
                         "feature change");
        QVERIFY2(writeFile(repo, "file.txt", "MAIN line1\nline2\n"), "write main");
        QVERIFY2(runGit(repo, {"add", "-A"}) == 0, "add main");
        QVERIFY2(runGit(repo, {"commit", "-q", "-m", "main change"}) == 0, "commit main");

        GitController c;
        c.setRepositoryPath(repo);
        bool merged = false;
        c.mergeAsync("feature/ab", [&merged](GitMergeService::MergeResult r) {
            QVERIFY(r.conflict);
            merged = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(merged, 15000);

        // Abort the merge; working tree must be restored to MAIN's version.
        bool aborted = false;
        c.abortMergeAsync([&aborted](bool ok, const QString &err) {
            QVERIFY2(ok, qPrintable(err));
            aborted = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(aborted, 15000);
        QFile f(repo + "/file.txt");
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString content = QString::fromUtf8(f.readAll());
        QVERIFY(content.startsWith("MAIN line1"));
        QVERIFY(!content.contains("<<<<<<<"));
        // No merge state remains.
        QString status;
        QVERIFY2(runGit(repo, {"status", "--porcelain=v1"}, &status) == 0, "status");
        QVERIFY(!status.contains("UU"));
    }
};

QTEST_MAIN(TestMergeManagement)
#include "test_merge_management.moc"
