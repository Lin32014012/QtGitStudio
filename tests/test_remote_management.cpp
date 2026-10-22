// v4-P1-2 Remote Support tests. Each test gets its OWN bare repository
// (shared bare repos would conflict on same-branch pushes). Scenarios:
//   1. no remote configured
//   2. remote listing (name + fetch + push urls)
//   3. fetch success (remote branch appears locally)
//   4. push success
//   5. pull success (working tree updated)
//   6. pull conflict (surfaced, never auto-resolved)
#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include "GitController.h"
#include "GitRemoteService.h"
#include "GitSnapshot.h"

class TestRemoteManagement : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;

    int runGit(const QString &dir, const QStringList &args,
               QString *out = nullptr, QString *errOut = nullptr) {
        QProcess git;
        git.setWorkingDirectory(dir);
        git.start("git", args);
        if (!git.waitForFinished(30000)) return -1;
        if (out) *out = QString::fromUtf8(git.readAllStandardOutput());
        if (errOut) *errOut = QString::fromUtf8(git.readAllStandardError());
        return git.exitCode();
    }

    bool writeFile(const QString &dir, const QString &name, const QByteArray &data) {
        QFile f(dir + "/" + name);
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write(data);
        return true;
    }

    // Fresh bare repository.
    QString mkBare(const QString &tag) {
        const QString path = m_dir.path() + "/" + tag + ".git";
        QDir().mkpath(path);
        QString err;
        const int rc = runGit(path, {"init", "--bare", "-q"}, nullptr, &err);
        if (rc != 0) {
            qWarning() << "bare init failed:" << err;
        }
        return path;
    }

    // Fresh local repo with one commit, remote added, pushed to `bare`.
    void mkLocalRepo(const QString &path, const QString &bare,
                     const QString &content, bool addRemote = true,
                     bool pushMain = true) {
        QDir().mkpath(path);
        QVERIFY2(runGit(path, {"init", "-q", "-b", "main"}) == 0, "init");
        QVERIFY2(runGit(path, {"config", "user.name", "Test"}) == 0, "name");
        QVERIFY2(runGit(path, {"config", "user.email", "t@t.local"}) == 0, "email");
        QVERIFY2(runGit(path, {"config", "pull.rebase", "false"}) == 0, "pull.rebase");
        QVERIFY2(writeFile(path, "file.txt", content.toUtf8()), "write");
        QVERIFY2(runGit(path, {"add", "-A"}) == 0, "add");
        QVERIFY2(runGit(path, {"commit", "-q", "-m", "v1"}) == 0, "commit");
        if (addRemote) {
            QVERIFY2(runGit(path, {"remote", "add", "origin", bare}) == 0, "remote add");
            if (pushMain) {
                QString pushErr, pushOut;
                const int pc = runGit(path, {"push", "-u", "origin", "main"},
                                      &pushOut, &pushErr);
                if (pc != 0) {
                    qDebug() << "DIAG push rc=" << pc;
                    qDebug() << "DIAG push stdout=[" << pushOut << "]";
                    qDebug() << "DIAG push stderr=[" << pushErr << "]";
                }
                QVERIFY2(pc == 0, qPrintable("push rc=" + QString::number(pc) + ": " + pushErr));
            }
        }
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
    }

    void noRemoteConfigured() {
        const QString repo = m_dir.path() + "/no_remote";
        mkLocalRepo(repo, m_dir.path() + "/unused_bare.git", "content\n",
                    /*addRemote=*/false);
        GitController c;
        c.setRepositoryPath(repo);
        bool done = false;
        c.remotesAsync([&done](GitController::RemoteResult r) {
            QVERIFY(r.success);
            QCOMPARE(r.remotes.size(), 0);
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
    }

    void remoteListingShowsUrls() {
        const QString bare = mkBare("bare_list");
        const QString repo = m_dir.path() + "/with_remote";
        mkLocalRepo(repo, bare, "content\n");
        GitController c;
        c.setRepositoryPath(repo);
        bool done = false;
        c.remotesAsync([&done, bare](GitController::RemoteResult r) {
            QVERIFY(r.success);
            QCOMPARE(r.remotes.size(), 1);
            QCOMPARE(r.remotes.first().name, QString("origin"));
            QCOMPARE(r.remotes.first().fetchUrl, bare);
            QCOMPARE(r.remotes.first().pushUrl, bare);
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
    }

    void fetchSucceeds() {
        const QString bare = mkBare("bare_fetch");
        const QString repoA = m_dir.path() + "/fetch_a";
        mkLocalRepo(repoA, bare, "aaa\n");
        // Second worktree pushes a NEW branch to the same bare.
        const QString repoB = m_dir.path() + "/fetch_b";
        mkLocalRepo(repoB, bare, "bbb\n", /*addRemote=*/true, /*pushMain=*/false);
        QVERIFY2(runGit(repoB, {"checkout", "-q", "-b", "feature/x"}) == 0, "co -b");
        QVERIFY2(writeFile(repoB, "extra.txt", "x\n"), "write2");
        QVERIFY2(runGit(repoB, {"add", "-A"}) == 0, "add2");
        QVERIFY2(runGit(repoB, {"commit", "-q", "-m", "v2"}) == 0, "commit2");
        QString pushErr;
        QVERIFY2(runGit(repoB, {"push", "-q", "-u", "origin", "feature/x"},
                        nullptr, &pushErr) == 0,
                 qPrintable("push2: " + pushErr));

        GitController c;
        c.setRepositoryPath(repoA);
        bool done = false;
        c.fetchAsync([&done](GitRemoteService::OperationResult r) {
            QVERIFY(r.success);
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
        QString branches;
        QVERIFY2(runGit(repoA, {"branch", "-r"}, &branches) == 0, "branch -r");
        QVERIFY(branches.contains("origin/feature/x"));
    }

    void pushSucceeds() {
        const QString bare = mkBare("bare_push");
        const QString repo = m_dir.path() + "/push_a";
        mkLocalRepo(repo, bare, "content\n");
        GitController c;
        c.setRepositoryPath(repo);
        QVERIFY2(writeFile(repo, "new.txt", "n\n"), "write");
        QVERIFY2(runGit(repo, {"add", "-A"}) == 0, "add");
        QVERIFY2(runGit(repo, {"commit", "-q", "-m", "local"}) == 0, "commit");
        bool done = false;
        c.pushAsync([&done](GitRemoteService::OperationResult r) {
            QVERIFY(r.success);
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
        QString log;
        QVERIFY2(runGit(bare, {"log", "--oneline", "main"}, &log) == 0, "bare log");
        QVERIFY(log.contains("local"));
    }

    void pullSucceeds() {
        const QString bare = mkBare("bare_pull");
        const QString repoA = m_dir.path() + "/pull_a";
        mkLocalRepo(repoA, bare, "base\n");
        // Remote side: another worktree adds a commit and pushes.
        const QString repoB = m_dir.path() + "/pull_b";
        QVERIFY2(runGit(m_dir.path(), {"clone", "-q", bare, "pull_b"}) == 0, "clone");
        QVERIFY2(runGit(repoB, {"config", "user.name", "Test"}) == 0, "name");
        QVERIFY2(runGit(repoB, {"config", "user.email", "t@t.local"}) == 0, "email");
        QVERIFY2(runGit(repoB, {"config", "pull.rebase", "false"}) == 0, "pull.rebase");
        QVERIFY2(writeFile(repoB, "remote.txt", "r\n"), "write");
        QVERIFY2(runGit(repoB, {"add", "-A"}) == 0, "add");
        QVERIFY2(runGit(repoB, {"commit", "-q", "-m", "remote"}) == 0, "commit");
        QString pushErr;
        QVERIFY2(runGit(repoB, {"push", "-q", "origin", "main"},
                        nullptr, &pushErr) == 0,
                 qPrintable("push: " + pushErr));

        GitController c;
        c.setRepositoryPath(repoA);
        bool done = false;
        c.pullAsync([&done](GitRemoteService::OperationResult r) {
            QVERIFY(r.success);
            QVERIFY(!r.conflict);
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
        QFile f(repoA + "/remote.txt");
        QVERIFY(f.exists());
    }

    void pullConflictIsSurfaced() {
        const QString bare = mkBare("bare_conf");
        const QString repoA = m_dir.path() + "/conf_a";
        mkLocalRepo(repoA, bare, "line1\nline2\n");
        // Remote side commits a conflicting change to the same line.
        const QString repoB = m_dir.path() + "/conf_b";
        QVERIFY2(runGit(m_dir.path(), {"clone", "-q", bare, "conf_b"}) == 0, "clone");
        QVERIFY2(runGit(repoB, {"config", "user.name", "Test"}) == 0, "name");
        QVERIFY2(runGit(repoB, {"config", "user.email", "t@t.local"}) == 0, "email");
        QVERIFY2(runGit(repoB, {"config", "pull.rebase", "false"}) == 0, "pull.rebase");
        QVERIFY2(writeFile(repoB, "file.txt", "REMOTE line1\nline2\n"), "write");
        QVERIFY2(runGit(repoB, {"add", "-A"}) == 0, "add");
        QVERIFY2(runGit(repoB, {"commit", "-q", "-m", "remote change"}) == 0, "commit");
        QString pushErr;
        QVERIFY2(runGit(repoB, {"push", "-q", "origin", "main"},
                        nullptr, &pushErr) == 0,
                 qPrintable("push: " + pushErr));

        // Local side commits a conflicting change to the same line.
        GitController c;
        c.setRepositoryPath(repoA);
        QVERIFY2(writeFile(repoA, "file.txt", "LOCAL line1\nline2\n"), "write");
        QVERIFY2(runGit(repoA, {"add", "-A"}) == 0, "add");
        QVERIFY2(runGit(repoA, {"commit", "-q", "-m", "local change"}) == 0, "commit");

        bool done = false;
        c.pullAsync([&done](GitRemoteService::OperationResult r) {
            QVERIFY(!r.success);
            QVERIFY(r.conflict);  // surfaced, never auto-resolved
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
        QFile f(repoA + "/file.txt");
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString content = QString::fromUtf8(f.readAll());
        QVERIFY(content.contains("<<<<<<<") && content.contains(">>>>>>>"));
    }
};

QTEST_MAIN(TestRemoteManagement)
#include "test_remote_management.moc"
