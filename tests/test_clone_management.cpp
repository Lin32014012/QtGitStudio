// v4-P1-3 Clone Support tests. Uses the existing ProjectController clone
// API (no second Git invocation stack). Scenarios:
//   1. clone from a local bare repository succeeds
//   2. invalid URL fails (InvalidSource)
//   3. non-empty target directory is rejected (TargetNotEmpty)
//   4. after clone, loadProject succeeds (detectProject -> isGitRepo)
#include <QtTest>
#include "test_settings_isolation.h"
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QDir>
#include "ProjectController.h"
#include "GitController.h"
#include "MainWindow.h"

class TestCloneManagement : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;

    int runGit(const QString &dir, const QStringList &args) {
        QProcess git;
        git.setWorkingDirectory(dir);
        git.start("git", args);
        if (!git.waitForFinished(30000)) return -1;
        return git.exitCode();
    }

private slots:
    void initTestCase() {
    isolateTestSettings();
        QVERIFY(m_dir.isValid());
    }

    void cloneFromBareSucceeds() {
        const QString bare = m_dir.path() + "/src_bare.git";
        QDir().mkpath(bare);
        QVERIFY2(runGit(bare, {"init", "--bare", "-q"}) == 0, "bare init");
        // Seed the bare with one commit via a helper worktree.
        const QString seed = m_dir.path() + "/seed";
        QDir().mkpath(seed);
        QVERIFY2(runGit(seed, {"init", "-q", "-b", "main"}) == 0, "seed init");
        QVERIFY2(runGit(seed, {"config", "user.name", "T"}) == 0, "name");
        QVERIFY2(runGit(seed, {"config", "user.email", "t@t.local"}) == 0, "email");
        QFile f(seed + "/readme.txt");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("hello\n");
        f.close();
        QVERIFY2(runGit(seed, {"add", "-A"}) == 0, "add");
        QVERIFY2(runGit(seed, {"commit", "-q", "-m", "init"}) == 0, "commit");
        QVERIFY2(runGit(seed, {"remote", "add", "origin", bare}) == 0, "remote");
        QVERIFY2(runGit(seed, {"push", "-q", "origin", "main"}) == 0, "push");

        const QString target = m_dir.path() + "/cloned";
        ProjectController pc;
        const ProjectController::CloneResult r =
            pc.cloneRepositoryDetailed(bare, target);
        QVERIFY2(r.success, qPrintable(r.summary));
        QVERIFY(QFile::exists(target + "/.git"));
        QVERIFY(QFile::exists(target + "/readme.txt"));
    }

    void invalidUrlFails() {
        const QString target = m_dir.path() + "/bad_target";
        ProjectController pc;
        const ProjectController::CloneResult r =
            pc.cloneRepositoryDetailed(
                "https://invalid.invalid/nonexistent/repo.git", target);
        QVERIFY(!r.success);
        QVERIFY(r.category == ProjectController::CloneFailureCategory::InvalidSource
                || r.category == ProjectController::CloneFailureCategory::Network
                // Sandboxed/proxied CI networks may terminate the TLS
                // handshake instead of failing DNS resolution.
                || r.category == ProjectController::CloneFailureCategory::Tls
                || r.category == ProjectController::CloneFailureCategory::Unknown);
    }

    void nonEmptyTargetRejected() {
        const QString bare = m_dir.path() + "/ne_bare.git";
        QDir().mkpath(bare);
        QVERIFY2(runGit(bare, {"init", "--bare", "-q"}) == 0, "bare init");
        // Target directory exists and is non-empty.
        const QString target = m_dir.path() + "/ne_target";
        QDir().mkpath(target);
        QFile f(target + "/existing.txt");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("occupied\n");
        f.close();

        ProjectController pc;
        const ProjectController::CloneResult r =
            pc.cloneRepositoryDetailed(bare, target);
        QVERIFY(!r.success);
        QCOMPARE(r.category,
                 ProjectController::CloneFailureCategory::TargetNotEmpty);
    }

    void cloneThenLoadProjectSucceeds() {
        const QString bare = m_dir.path() + "/lp_bare.git";
        QDir().mkpath(bare);
        QVERIFY2(runGit(bare, {"init", "--bare", "-q"}) == 0, "bare init");
        const QString seed = m_dir.path() + "/lp_seed";
        QDir().mkpath(seed);
        QVERIFY2(runGit(seed, {"init", "-q", "-b", "main"}) == 0, "seed init");
        QVERIFY2(runGit(seed, {"config", "user.name", "T"}) == 0, "name");
        QVERIFY2(runGit(seed, {"config", "user.email", "t@t.local"}) == 0, "email");
        QFile f(seed + "/app.txt");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("app\n");
        f.close();
        QVERIFY2(runGit(seed, {"add", "-A"}) == 0, "add");
        QVERIFY2(runGit(seed, {"commit", "-q", "-m", "init"}) == 0, "commit");
        QVERIFY2(runGit(seed, {"remote", "add", "origin", bare}) == 0, "remote");
        QVERIFY2(runGit(seed, {"push", "-q", "origin", "main"}) == 0, "push");

        const QString target = m_dir.path() + "/lp_cloned";
        ProjectController pc;
        const ProjectController::CloneResult r =
            pc.cloneRepositoryDetailed(bare, target);
        QVERIFY2(r.success, qPrintable(r.summary));

        // loadProject equivalent: detect the cloned repo, then open it in
        // MainWindow (setRepositoryPath + refresh must work).
        bool done = false;
        pc.detectProjectAsync(target, [&done](ProjectController::ProjectInfo info) {
            QVERIFY(info.isGitRepo);
            QCOMPARE(info.branch, QString("main"));
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000);

        MainWindow w;
        QMetaObject::invokeMethod(&w, "showWorkspace");
        w.controller()->setRepositoryPath(target);
        w.controller()->refresh();
        QApplication::processEvents();
        QTRY_VERIFY_WITH_TIMEOUT(
            w.controller()->viewModel()->currentBranch() == "main", 15000);
    }
};

QTEST_MAIN(TestCloneManagement)
#include "test_clone_management.moc"
