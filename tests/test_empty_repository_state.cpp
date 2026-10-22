// v4-P2-1.1 Empty Repository Support tests:
//   1. fresh `git init` is recognized as EmptyRepository
//   2. first commit restores Normal state
//   3. branch creation works on an empty repository (switch -c, no start
//      point)
//   4. normal repository regression (Normal state + regular operations)
#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QDir>
#include "GitController.h"
#include "GitBranchService.h"

class TestEmptyRepositoryState : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;

    int runGit(const QString &dir, const QStringList &args) {
        QProcess git;
        git.setWorkingDirectory(dir);
        git.start("git", args);
        if (!git.waitForFinished(15000)) return -1;
        return git.exitCode();
    }

    bool writeFile(const QString &dir, const QString &name, const QByteArray &data) {
        QFile f(dir + "/" + name);
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write(data);
        return true;
    }

    QString mkEmptyRepo(const QString &tag) {
        const QString path = m_dir.path() + "/" + tag;
        QDir().mkpath(path);
        runGit(path, {"init", "-q", "-b", "main"});
        runGit(path, {"config", "user.name", "Test"});
        runGit(path, {"config", "user.email", "t@t.local"});
        return path;
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
    }

    void gitInitRecognizedAsEmpty() {
        const QString repo = mkEmptyRepo("empty_recognize");
        GitController c;
        c.setRepositoryPath(repo);
        QCOMPARE(c.repoState(), GitController::RepoState::EmptyRepository);
        // HEAD symbolic ref still names the intended branch.
        QCOMPARE(c.headBranchName(), QStringLiteral("main"));
    }

    void firstCommitRestoresNormal() {
        const QString repo = mkEmptyRepo("empty_normal");
        QVERIFY(writeFile(repo, "file.txt", "hi\n"));
        QVERIFY2(runGit(repo, {"add", "-A"}) == 0, "add");
        QVERIFY2(runGit(repo, {"commit", "-q", "-m", "first"}) == 0, "commit");

        GitController c;
        c.setRepositoryPath(repo);
        QCOMPARE(c.repoState(), GitController::RepoState::Normal);
        QCOMPARE(c.headBranchName(), QStringLiteral("main"));
    }

    void createBranchOnEmptyRepository() {
        const QString repo = mkEmptyRepo("empty_create");
        GitController c;
        c.setRepositoryPath(repo);

        // Empty repo: create with NO start point + switch -c semantics.
        bool done = false;
        c.createBranchAsync(
            "feature/empty-start", QString(), true,
            [&done](GitController::OperationResult r) {
                QVERIFY2(r.success, qPrintable(r.error));
                done = true;
            });
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
        // The unborn HEAD branch was created AND checked out.
        QCOMPARE(c.headBranchName(), QStringLiteral("feature/empty-start"));
        // State is still empty (no commit yet) but the branch exists.
        QCOMPARE(c.repoState(), GitController::RepoState::EmptyRepository);

        // First commit on the new branch -> Normal.
        QVERIFY(writeFile(repo, "a.txt", "a\n"));
        QVERIFY2(runGit(repo, {"add", "-A"}) == 0, "add");
        QVERIFY2(runGit(repo, {"commit", "-q", "-m", "first"}) == 0, "commit");
        QCOMPARE(c.repoState(), GitController::RepoState::Normal);
    }

    void normalRepositoryRegression() {
        const QString repo = mkEmptyRepo("normal_regression");
        QVERIFY(writeFile(repo, "file.txt", "hi\n"));
        QVERIFY2(runGit(repo, {"add", "-A"}) == 0, "add");
        QVERIFY2(runGit(repo, {"commit", "-q", "-m", "first"}) == 0, "commit");
        QVERIFY2(runGit(repo, {"checkout", "-q", "-b", "feature/x"}) == 0, "co");

        GitController c;
        c.setRepositoryPath(repo);
        QCOMPARE(c.repoState(), GitController::RepoState::Normal);
        // Detached HEAD classification.
        QVERIFY2(runGit(repo, {"checkout", "-q", "HEAD~0"}) == 0, "detach");
        QCOMPARE(c.repoState(), GitController::RepoState::DetachedHead);
        QVERIFY(c.headBranchName().isEmpty());
        // Back on a branch -> Normal again.
        QVERIFY2(runGit(repo, {"checkout", "-q", "main"}) == 0, "co main");
        QCOMPARE(c.repoState(), GitController::RepoState::Normal);
    }
};

QTEST_MAIN(TestEmptyRepositoryState)
#include "test_empty_repository_state.moc"
