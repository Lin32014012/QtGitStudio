// v4-P2-3 Stability Finalization: drop-session lifecycle tests at the
// business layer (direct component driving - no QDropEvent injection).
// Scenarios:
//   1. A -> A                 (same repo twice, sequential sessions)
//   2. A x10                  (no deadlock, session always released)
//   3. A -> B -> A            (repository switch, last path wins)
//   4. failed open -> reopen  (non-repo path fails cleanly, then a real
//                              repo opens successfully)
#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QSignalSpy>
#include "DropOpenSession.h"
#include "RepositoryResolver.h"

class TestDropSessionLifecycle : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;

    bool runGit(const QString &dir, const QStringList &args) {
        QProcess git;
        git.setWorkingDirectory(dir);
        git.start("git", args);
        if (!git.waitForFinished(15000)) return false;
        return git.exitStatus() == QProcess::NormalExit && git.exitCode() == 0;
    }

    QString mkRepo(const QString &name, const QString &file, const QByteArray &content) {
        const QString path = m_dir.path() + "/" + name;
        QDir().mkpath(path);
        runGit(path, {"init", "-q", "-b", "main"});
        runGit(path, {"config", "user.name", "Test"});
        runGit(path, {"config", "user.email", "t@t.local"});
        QFile f(path + "/" + file);
        f.open(QIODevice::WriteOnly);
        f.write(content);
        f.close();
        runGit(path, {"add", "-A"});
        runGit(path, {"commit", "-q", "-m", "init"});
        return path;
    }

    bool beginAndResolve(DropOpenSession *session, const QString &path,
                         QString *resolvedPath = nullptr, bool *isRepo = nullptr) {
        QSignalSpy spy(session, &DropOpenSession::resolved);
        if (!session->begin(path)) {
            return false;
        }
        for (int i = 0; i < 60 && spy.count() < 1; ++i) {
            QApplication::processEvents();
            QTest::qWait(250);
        }
        if (spy.count() >= 1) {
            if (resolvedPath) *resolvedPath = spy.at(0).at(0).toString();
            if (isRepo) *isRepo = spy.at(0).at(1).toBool();
        }
        return spy.count() >= 1;
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        // v4-P6-21R2: on this machine the user home directory is a git
        // work tree, so ANY temp directory looks like a repository to
        // `git rev-parse --is-inside-work-tree` (parent discovery).
        // Ceil discovery at the temp root so non-repo fixtures stay
        // truly non-repo. Product behaviour is untouched.
        qputenv("GIT_CEILING_DIRECTORIES", QDir::tempPath().toUtf8());
    }

    void sameRepositoryTwice() {
        const QString repoA = mkRepo("lc_a", "a.txt", "aaa\n");
        RepositoryResolver resolver;
        DropOpenSession session(&resolver);

        QString rp;
        bool isRepo = false;
        QVERIFY(beginAndResolve(&session, repoA, &rp, &isRepo));
        QCOMPARE(rp, repoA);
        QVERIFY(isRepo);
        session.finish();
        QVERIFY(!session.isActive());

        QVERIFY(beginAndResolve(&session, repoA, &rp, &isRepo));
        QCOMPARE(rp, repoA);
        QVERIFY(isRepo);
        session.finish();
        QVERIFY(!session.isActive());
        QVERIFY(!resolver.isBusy());
    }

    void sameRepositoryTenTimes() {
        const QString repoA = mkRepo("lc_10x", "a.txt", "aaa\n");
        RepositoryResolver resolver;
        DropOpenSession session(&resolver);

        for (int i = 0; i < 10; ++i) {
            QString rp;
            bool isRepo = false;
            QVERIFY2(beginAndResolve(&session, repoA, &rp, &isRepo),
                     qPrintable(QString("iteration %1 failed").arg(i + 1)));
            QCOMPARE(rp, repoA);
            QVERIFY(isRepo);
            session.finish();
            QVERIFY(!session.isActive());
            QVERIFY(!resolver.isBusy());
            QApplication::processEvents();
        }
    }

    void switchRepositoryABackToA() {
        const QString repoA = mkRepo("lc_sw_a", "a.txt", "aaa\n");
        const QString repoB = mkRepo("lc_sw_b", "b.txt", "bbb\n");
        RepositoryResolver resolver;
        DropOpenSession session(&resolver);

        QString rp;
        bool isRepo = false;
        QVERIFY(beginAndResolve(&session, repoA, &rp, &isRepo));
        QCOMPARE(rp, repoA);
        session.finish();

        QVERIFY(beginAndResolve(&session, repoB, &rp, &isRepo));
        QCOMPARE(rp, repoB);
        session.finish();

        QVERIFY(beginAndResolve(&session, repoA, &rp, &isRepo));
        QCOMPARE(rp, repoA);
        session.finish();
        QVERIFY(!session.isActive());
    }

    void failedOpenThenReopenSucceeds() {
        const QString repoA = mkRepo("lc_fail_a", "a.txt", "aaa\n");
        // Non-repo directory (empty folder).
        const QString nonRepo = m_dir.path() + "/not_a_repo";
        QDir().mkpath(nonRepo);
        RepositoryResolver resolver;
        DropOpenSession session(&resolver);

        // Opening a non-repo fails cleanly: resolved(isRepo=false),
        // session finishes, nothing leaks.
        QString rp;
        bool isRepo = true;
        QVERIFY(beginAndResolve(&session, nonRepo, &rp, &isRepo));
        QCOMPARE(rp, nonRepo);
        QVERIFY(!isRepo);
        QCOMPARE(session.state(), DropOpenSession::State::Opening);
        session.finish();
        QVERIFY(!session.isActive());
        QVERIFY(!resolver.isBusy());

        // Immediately re-open a real repo - must succeed.
        QVERIFY(beginAndResolve(&session, repoA, &rp, &isRepo));
        QCOMPARE(rp, repoA);
        QVERIFY(isRepo);
        session.finish();
        QVERIFY(!session.isActive());
    }
};

QTEST_MAIN(TestDropSessionLifecycle)
#include "test_drop_session_lifecycle.moc"
