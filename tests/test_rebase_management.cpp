// v4-P4-2 Rebase Management tests:
//   1. successfulRebase        (feature commits land after main; hashes
//                               change; clean tree)
//   2. upToDateRebase          (explicit classification, no false failure)
//   3. conflictDetection       (outcome Conflict + conflictedFiles + state)
//   4. continueAfterResolution (resolve, add, continue -> done, clean)
//   5. abortRebase             (state cleared, branch restored, no residue)
//   6. dirtyWorkspaceRejected  (uncommitted changes -> Rejected, untouched)
//   7. sameBranchRejected      (target == current -> Rejected)
//   8. emptyOrDetachedRepository (clean failure, no crash, message set)
// Every scenario uses its own temporary repository; no network, no
// GitHub, no global git configuration.
#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QEventLoop>
#include "GitRebaseService.h"

class TestRebaseManagement : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;

    bool runGit(const QString &dir, const QStringList &args,
                QString *output = nullptr, QString *errOutput = nullptr) {
        QProcess git;
        git.setWorkingDirectory(dir);
        git.start("git", args);
        if (!git.waitForFinished(20000)) return false;
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
        return QString::fromUtf8(f.readAll()).remove(QLatin1Char('\r'));
    }

    QString workingTreeStatus(const QString &dir) {
        QString out;
        runGit(dir, {"status", "--porcelain"}, &out);
        return out.trimmed();
    }

    // Await a rebase/continue operation. The completion may fire
    // SYNCHRONOUSLY (Rejected from the precondition checks) or
    // asynchronously (git executed); the loop is only entered when the
    // completion has not already arrived.
    template <typename Launch>
    RebaseResult awaitRebaseOp(Launch launch) {
        RebaseResult out;
        bool finished = false;
        QEventLoop loop;
        launch([&out, &finished, &loop](const RebaseResult &result) {
            out = result;
            finished = true;
            if (loop.isRunning()) loop.quit();
        });
        if (!finished) {
            QTimer::singleShot(30000, &loop, &QEventLoop::quit);
            loop.exec();
        }
        return out;
    }

    RebaseResult awaitRebase(GitRebaseService *service,
                             const QString &target) {
        return awaitRebaseOp(
            [service, target](const std::function<void(const RebaseResult &)> &cb) {
                service->rebaseAsync(target, cb);
            });
    }

    RebaseResult awaitContinue(GitRebaseService *service) {
        return awaitRebaseOp(
            [service](const std::function<void(const RebaseResult &)> &cb) {
                service->continueAsync(cb);
            });
    }

    bool rebaseInProgress(GitRebaseService *service) {
        // isRebaseInProgressAsync invokes its completion synchronously
        // (the git-path probes are synchronous), so no event loop is
        // needed - and one would deadlock/rotate for the full timeout.
        bool value = false;
        service->isRebaseInProgressAsync(
            [&value](bool inProgress, const QString &) {
                value = inProgress;
            });
        return value;
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
    }

    void successfulRebase() {
        // main: base + "main work"; feature: base + "feature work".
        const QString repo = mkRepo("rb_success");
        writeFile(repo, "a.txt", "base\n");
        commitAll(repo, "base");
        runGit(repo, {"checkout", "-q", "-b", "feature/login"});
        writeFile(repo, "f.txt", "f\n");
        commitAll(repo, "feature work");
        const QString oldFeatureHead = headHash(repo);
        runGit(repo, {"checkout", "-q", "main"});
        writeFile(repo, "m.txt", "m\n");
        commitAll(repo, "main work");
        const QString mainHead = headHash(repo);

        runGit(repo, {"checkout", "-q", "feature/login"});
        GitRebaseService service;
        service.setWorkingDirectory(repo);
        const RebaseResult result = awaitRebase(&service, "main");
        QCOMPARE(static_cast<int>(result.outcome),
                 static_cast<int>(RebaseOutcome::Success));

        // feature's commits now sit after main's latest.
        QString log;
        runGit(repo, {"log", "--oneline", "-3"}, &log);
        QVERIFY(log.contains("feature work"));
        QVERIFY(log.contains("main work"));
        // The rebased feature commit hash changed.
        QVERIFY(headHash(repo) != oldFeatureHead);
        // Working tree is clean.
        QVERIFY(workingTreeStatus(repo).isEmpty());
        // No rebase state left.
        QVERIFY(!rebaseInProgress(&service));
    }

    void upToDateRebase() {
        // feature is based directly on main's tip: nothing to rebase.
        const QString repo = mkRepo("rb_uptodate");
        writeFile(repo, "a.txt", "base\n");
        commitAll(repo, "base");
        runGit(repo, {"checkout", "-q", "-b", "feature"});
        writeFile(repo, "f.txt", "f\n");
        commitAll(repo, "feature work");
        runGit(repo, {"checkout", "-q", "main"});
        // main has NOT advanced.

        runGit(repo, {"checkout", "-q", "feature"});
        GitRebaseService service;
        service.setWorkingDirectory(repo);
        const RebaseResult result = awaitRebase(&service, "main");
        QVERIFY(result.outcome == RebaseOutcome::UpToDate
                || result.outcome == RebaseOutcome::Success);
        // Not a failure.
        QVERIFY(result.outcome != RebaseOutcome::Failed);
        QVERIFY(!rebaseInProgress(&service));
    }

    void conflictDetection() {
        // Both branches edit the same line of a.txt.
        const QString repo = mkRepo("rb_conflict");
        writeFile(repo, "a.txt", "base\n");
        commitAll(repo, "base");
        runGit(repo, {"checkout", "-q", "-b", "feature"});
        writeFile(repo, "a.txt", "feature\n");
        commitAll(repo, "feature edit");
        runGit(repo, {"checkout", "-q", "main"});
        writeFile(repo, "a.txt", "main\n");
        commitAll(repo, "main edit");

        runGit(repo, {"checkout", "-q", "feature"});
        GitRebaseService service;
        service.setWorkingDirectory(repo);
        const RebaseResult result = awaitRebase(&service, "main");
        QCOMPARE(static_cast<int>(result.outcome),
                 static_cast<int>(RebaseOutcome::Conflict));
        QVERIFY(result.conflictedFiles.contains(QStringLiteral("a.txt")));
        QVERIFY(rebaseInProgress(&service));
    }

    void continueAfterResolution() {
        const QString repo = mkRepo("rb_continue");
        writeFile(repo, "a.txt", "base\n");
        commitAll(repo, "base");
        runGit(repo, {"checkout", "-q", "-b", "feature"});
        writeFile(repo, "a.txt", "feature\n");
        commitAll(repo, "feature edit");
        runGit(repo, {"checkout", "-q", "main"});
        writeFile(repo, "a.txt", "main\n");
        commitAll(repo, "main edit");
        runGit(repo, {"checkout", "-q", "feature"});

        GitRebaseService service;
        service.setWorkingDirectory(repo);
        const RebaseResult conflict = awaitRebase(&service, "main");
        QCOMPARE(static_cast<int>(conflict.outcome),
                 static_cast<int>(RebaseOutcome::Conflict));

        // Resolve: pick main's content, stage it, then continue.
        writeFile(repo, "a.txt", "resolved\n");
        QVERIFY(runGit(repo, {"add", "a.txt"}));
        const RebaseResult cont = awaitContinue(&service);
        QCOMPARE(static_cast<int>(cont.outcome),
                 static_cast<int>(RebaseOutcome::Success));

        // Rebase finished: no state left, tree clean, content resolved.
        QVERIFY(!rebaseInProgress(&service));
        QVERIFY(workingTreeStatus(repo).isEmpty());
        QCOMPARE(fileContent(repo, "a.txt"), QString("resolved\n"));
    }

    void abortRebase() {
        const QString repo = mkRepo("rb_abort");
        writeFile(repo, "a.txt", "base\n");
        commitAll(repo, "base");
        runGit(repo, {"checkout", "-q", "-b", "feature"});
        writeFile(repo, "a.txt", "feature\n");
        commitAll(repo, "feature edit");
        const QString featureHeadBefore = headHash(repo);
        runGit(repo, {"checkout", "-q", "main"});
        writeFile(repo, "a.txt", "main\n");
        commitAll(repo, "main edit");
        runGit(repo, {"checkout", "-q", "feature"});

        GitRebaseService service;
        service.setWorkingDirectory(repo);
        const RebaseResult conflict = awaitRebase(&service, "main");
        QCOMPARE(static_cast<int>(conflict.outcome),
                 static_cast<int>(RebaseOutcome::Conflict));

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

        // State cleared; branch back on its pre-rebase commit; no residue.
        QVERIFY(!rebaseInProgress(&service));
        QCOMPARE(headHash(repo), featureHeadBefore);
        const QString status = workingTreeStatus(repo);
        QVERIFY(!status.contains("UU"));
        QVERIFY(!status.contains("AA"));
        QVERIFY(!status.contains("DD"));
    }

    void dirtyWorkspaceRejected() {
        const QString repo = mkRepo("rb_dirty");
        writeFile(repo, "a.txt", "base\n");
        commitAll(repo, "base");
        runGit(repo, {"checkout", "-q", "-b", "feature"});
        writeFile(repo, "f.txt", "f\n");
        commitAll(repo, "feature work");
        runGit(repo, {"checkout", "-q", "main"});
        writeFile(repo, "m.txt", "m\n");
        commitAll(repo, "main work");
        runGit(repo, {"checkout", "-q", "feature"});
        const QString headBefore = headHash(repo);

        // Uncommitted change in the working tree.
        writeFile(repo, "dirty.txt", "dirty\n");

        GitRebaseService service;
        service.setWorkingDirectory(repo);
        const RebaseResult result = awaitRebase(&service, "main");
        QCOMPARE(static_cast<int>(result.outcome),
                 static_cast<int>(RebaseOutcome::Rejected));
        QVERIFY(result.message.contains("未提交修改"));

        // Working tree untouched, no rebase state entered.
        QVERIFY(workingTreeStatus(repo).contains("dirty.txt"));
        QCOMPARE(headHash(repo), headBefore);
        QVERIFY(!rebaseInProgress(&service));
    }

    void sameBranchRejected() {
        const QString repo = mkRepo("rb_same");
        writeFile(repo, "a.txt", "base\n");
        commitAll(repo, "base");

        GitRebaseService service;
        service.setWorkingDirectory(repo);
        const RebaseResult result = awaitRebase(&service, "main");
        QCOMPARE(static_cast<int>(result.outcome),
                 static_cast<int>(RebaseOutcome::Rejected));
        // No destructive action: HEAD unchanged, tree clean, no state.
        QVERIFY(workingTreeStatus(repo).isEmpty());
        QVERIFY(!rebaseInProgress(&service));
    }

    void emptyOrDetachedRepository() {
        // Empty repository (no commits): clean rejection, no crash.
        const QString empty = mkRepo("rb_empty");
        GitRebaseService emptyService;
        emptyService.setWorkingDirectory(empty);
        const RebaseResult emptyResult = awaitRebase(&emptyService, "main");
        QVERIFY(emptyResult.outcome == RebaseOutcome::Rejected
                || emptyResult.outcome == RebaseOutcome::Failed);
        QVERIFY(!emptyResult.message.isEmpty());

        // Detached HEAD: clean rejection, no crash.
        const QString detached = mkRepo("rb_detached");
        writeFile(detached, "a.txt", "1\n");
        commitAll(detached, "first");
        writeFile(detached, "b.txt", "2\n");
        commitAll(detached, "second");
        runGit(detached, {"checkout", "-q", "HEAD~1"});
        GitRebaseService detachedService;
        detachedService.setWorkingDirectory(detached);
        const RebaseResult detachedResult =
            awaitRebase(&detachedService, "main");
        QCOMPARE(static_cast<int>(detachedResult.outcome),
                 static_cast<int>(RebaseOutcome::Rejected));
        QVERIFY(!detachedResult.message.isEmpty());
    }
};

QTEST_MAIN(TestRebaseManagement)
#include "test_rebase_management.moc"
