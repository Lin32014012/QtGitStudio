// v4-P4-3 Git Bisect Management tests:
//   1. start (bisect becomes active, log exists)
//   2. good (mark current commit good; status records it)
//   3. bad (bisect advances to a new test commit)
//   4. firstBadCommit (full bisect run locates the introducing commit)
//   5. reset (inactive again, branch restored)
//   6. emptyRepository (clean failure, no crash)
// Every scenario uses its own temporary repository.
#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QEventLoop>
#include "GitBisectService.h"

class TestBisectManagement : public QObject {
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

    QString currentBranch(const QString &dir) {
        QString out;
        runGit(dir, {"rev-parse", "--abbrev-ref", "HEAD"}, &out);
        return out.trimmed();
    }

    // Await an async bisect operation (completion fires asynchronously
    // via executeAsync; the loop guard also covers sync completions).
    template <typename Launch>
    BisectResult awaitBisect(Launch launch) {
        BisectResult out;
        bool finished = false;
        QEventLoop loop;
        launch([&out, &finished, &loop](const BisectResult &result) {
            out = result;
            finished = true;
            if (loop.isRunning()) loop.quit();
        });
        if (!finished) {
            QTimer::singleShot(15000, &loop, &QEventLoop::quit);
            loop.exec();
        }
        return out;
    }

    // statusAsync invokes its completion synchronously - call directly.
    BisectStatus statusOf(GitBisectService *service) {
        BisectStatus status;
        service->statusAsync([&status](const BisectStatus &s) {
            status = s;
        });
        return status;
    }

    // File content at a specific commit (for the bug marker).
    QString showContent(const QString &dir, const QString &commit,
                        const QString &path) {
        QString out;
        runGit(dir, {"show", commit + ":" + path}, &out);
        return out.trimmed();
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
    }

    void startBisect() {
        const QString repo = mkRepo("bs_start");
        for (int i = 1; i <= 6; ++i) {
            writeFile(repo, "f.txt", QString("v%1\n").arg(i).toUtf8());
            commitAll(repo, QString("commit %1").arg(i));
        }
        // `git bisect log` records resolved hashes, not ~expressions.
        QString firstRaw;
        runGit(repo, {"rev-parse", "HEAD~5"}, &firstRaw);
        const QString first = firstRaw.trimmed();
        const QString last = headHash(repo);

        GitBisectService service;
        service.setWorkingDirectory(repo);
        const BisectResult result = awaitBisect(
            [&service, &last, &first](const GitBisectService::BisectCompletion &cb) {
                service.startAsync(last, first, cb);
            });
        QVERIFY(result.outcome == BisectOutcome::Started
                || result.outcome == BisectOutcome::Marked);

        const BisectStatus status = statusOf(&service);
        QVERIFY(status.active);
        QVERIFY(!status.currentCommit.isEmpty());
        QCOMPARE(status.badCommit, last);
        QCOMPARE(status.goodCommit, first);
    }

    void markGood() {
        const QString repo = mkRepo("bs_good");
        for (int i = 1; i <= 6; ++i) {
            writeFile(repo, "f.txt", QString("v%1\n").arg(i).toUtf8());
            commitAll(repo, QString("commit %1").arg(i));
        }
        const QString last = headHash(repo);
        const QString first = last + "~5";

        GitBisectService service;
        service.setWorkingDirectory(repo);
        awaitBisect([&service, &last, &first](const GitBisectService::BisectCompletion &cb) {
            service.startAsync(last, first, cb);
        });
        // Mark the current test commit good (bisect advances).
        const BisectResult result = awaitBisect(
            [&service](const GitBisectService::BisectCompletion &cb) {
                service.markGoodAsync(cb);
            });
        QVERIFY(result.outcome == BisectOutcome::Started
                || result.outcome == BisectOutcome::Marked
                || result.outcome == BisectOutcome::Found);
        // The good commit is recorded in the status.
        const BisectStatus status = statusOf(&service);
        if (status.active) {
            QVERIFY(!status.goodCommit.isEmpty());
        }
    }

    void markBad() {
        const QString repo = mkRepo("bs_bad");
        for (int i = 1; i <= 6; ++i) {
            writeFile(repo, "f.txt", QString("v%1\n").arg(i).toUtf8());
            commitAll(repo, QString("commit %1").arg(i));
        }
        const QString last = headHash(repo);
        const QString first = last + "~5";

        GitBisectService service;
        service.setWorkingDirectory(repo);
        awaitBisect([&service, &last, &first](const GitBisectService::BisectCompletion &cb) {
            service.startAsync(last, first, cb);
        });
        const QString headBefore = statusOf(&service).currentCommit;

        const BisectResult result = awaitBisect(
            [&service](const GitBisectService::BisectCompletion &cb) {
                service.markBadAsync(cb);
            });
        QVERIFY(result.outcome == BisectOutcome::Started
                || result.outcome == BisectOutcome::Marked
                || result.outcome == BisectOutcome::Found);
        // Bisect advanced to a new test commit (or found it). The bad
        // record is the most recently marked bad commit (the candidate
        // we just marked), not the start-time bad.
        const BisectStatus status = statusOf(&service);
        QVERIFY(status.active);
        if (result.outcome != BisectOutcome::Found) {
            QVERIFY(status.currentCommit != headBefore);
            QCOMPARE(status.badCommit, headBefore);
        }
    }

    void firstBadCommit() {
        // 7 commits; bug.txt flips to "bad" at commit 4.
        const QString repo = mkRepo("bs_firstbad");
        QString firstBad;
        for (int i = 1; i <= 7; ++i) {
            writeFile(repo, "bug.txt",
                      QByteArray(i >= 4 ? "bad\n" : "ok\n"));
            writeFile(repo, "f.txt", QString("v%1\n").arg(i).toUtf8());
            commitAll(repo, QString("commit %1").arg(i));
            if (i == 4) firstBad = headHash(repo);
        }
        const QString first = headHash(repo) + "~6";
        const QString last = headHash(repo);

        GitBisectService service;
        service.setWorkingDirectory(repo);
        awaitBisect([&service, &last, &first](const GitBisectService::BisectCompletion &cb) {
            service.startAsync(last, first, cb);
        });

        // Drive the bisect: inspect the current test commit's marker and
        // mark good/bad accordingly until the first bad commit is found.
        BisectResult result;
        int guard = 0;
        do {
            const BisectStatus status = statusOf(&service);
            QVERIFY(status.active);
            QVERIFY(!status.currentCommit.isEmpty());
            const QString marker =
                showContent(repo, status.currentCommit, "bug.txt");
            result = (marker == QStringLiteral("bad"))
                         ? awaitBisect([&service](const GitBisectService::BisectCompletion &cb) {
                               service.markBadAsync(cb);
                           })
                         : awaitBisect([&service](const GitBisectService::BisectCompletion &cb) {
                               service.markGoodAsync(cb);
                           });
            ++guard;
            QVERIFY2(guard < 10, "bisect did not converge");
        } while (result.outcome != BisectOutcome::Found);

        QCOMPARE(result.suggestedCommit, firstBad);
    }

    void resetBisect() {
        const QString repo = mkRepo("bs_reset");
        for (int i = 1; i <= 6; ++i) {
            writeFile(repo, "f.txt", QString("v%1\n").arg(i).toUtf8());
            commitAll(repo, QString("commit %1").arg(i));
        }
        const QString branchBefore = currentBranch(repo);

        GitBisectService service;
        service.setWorkingDirectory(repo);
        awaitBisect([&service](const GitBisectService::BisectCompletion &cb) {
            service.startAsync(QString(), QString(), cb);
        });
        QVERIFY(statusOf(&service).active);

        const BisectResult result = awaitBisect(
            [&service](const GitBisectService::BisectCompletion &cb) {
                service.resetAsync(cb);
            });
        QCOMPARE(result.outcome, BisectOutcome::Reset);

        const BisectStatus status = statusOf(&service);
        QVERIFY(!status.active);
        QVERIFY(status.currentCommit.isEmpty());
        // Branch restored.
        QCOMPARE(currentBranch(repo), branchBefore);
    }

    void emptyRepository() {
        const QString repo = mkRepo("bs_empty");
        writeFile(repo, "a.txt", "1\n");

        GitBisectService service;
        service.setWorkingDirectory(repo);
        const BisectResult result = awaitBisect(
            [&service](const GitBisectService::BisectCompletion &cb) {
                service.startAsync(QString(), QString(), cb);
            });
        // No HEAD: clean error, never Found/Started, no crash.
        QVERIFY(result.outcome != BisectOutcome::Found);
        QVERIFY(result.outcome != BisectOutcome::Started);
        QVERIFY(!result.message.isEmpty());
        QVERIFY(!statusOf(&service).active);
    }
};

QTEST_MAIN(TestBisectManagement)
#include "test_bisect_management.moc"
