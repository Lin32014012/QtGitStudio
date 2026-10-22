// v4-P1-4.1 Stability Hardening: business-layer tests for the drop-open
// session lifecycle. Deliberately does NOT use QApplication::sendEvent
// with QDropEvent (unreliable on the offscreen platform); the
// DropOpenSession + RepositoryResolver components are driven directly
// through their public API, which is exactly what MainWindow::dropEvent
// triggers. Scenarios:
//   1. A -> A                  (same repo twice, sequential sessions)
//   2. A -> A x10              (no deadlock, session always released)
//   3. A -> B -> A             (repository switch, path correctness)
// Assertions: session state machine converges to Done/Idle, resolver is
// never busy after finish, second begin while active is rejected.
#include <QtTest>
#include "test_settings_isolation.h"
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QSignalSpy>
#include "DropOpenSession.h"
#include "RepositoryResolver.h"

class TestRepositoryOpenSession : public QObject {
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

    // Begin a session and wait for the resolved signal.
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
    isolateTestSettings();
        QVERIFY(m_dir.isValid());
    }

    void sameRepositoryTwice() {
        const QString repoA = mkRepo("sess_a", "a.txt", "aaa\n");
        RepositoryResolver resolver;
        DropOpenSession session(&resolver);
        QVERIFY(session.state() == DropOpenSession::State::Idle);

        // First open: A.
        QString rp1;
        bool isRepo1 = false;
        QVERIFY(beginAndResolve(&session, repoA, &rp1, &isRepo1));
        QCOMPARE(rp1, repoA);
        QVERIFY(isRepo1);
        QCOMPARE(session.state(), DropOpenSession::State::Opening);
        session.finish();
        QCOMPARE(session.state(), DropOpenSession::State::Done);
        QVERIFY(!session.isActive());
        QVERIFY(!resolver.isBusy());

        // Second open: same A again - must succeed (session released).
        QString rp2;
        bool isRepo2 = false;
        QVERIFY(beginAndResolve(&session, repoA, &rp2, &isRepo2));
        QCOMPARE(rp2, repoA);
        QVERIFY(isRepo2);
        session.finish();
        QVERIFY(!session.isActive());
        QVERIFY(!resolver.isBusy());
    }

    void sameRepositoryTenTimes() {
        const QString repoA = mkRepo("sess_10x", "a.txt", "aaa\n");
        RepositoryResolver resolver;
        DropOpenSession session(&resolver);

        for (int i = 0; i < 10; ++i) {
            QString rp;
            bool isRepo = false;
            QVERIFY2(beginAndResolve(&session, repoA, &rp, &isRepo),
                     qPrintable(QString("iteration %1 failed").arg(i + 1)));
            QCOMPARE(rp, repoA);
            QVERIFY(isRepo);
            QCOMPARE(session.state(), DropOpenSession::State::Opening);
            session.finish();
            QVERIFY(!session.isActive());
            QVERIFY(!resolver.isBusy());  // probe fully released
            QApplication::processEvents();
        }
    }

    void switchRepositoryABackToA() {
        const QString repoA = mkRepo("sess_switch_a", "a.txt", "aaa\n");
        const QString repoB = mkRepo("sess_switch_b", "b.txt", "bbb\n");
        RepositoryResolver resolver;
        DropOpenSession session(&resolver);

        QString rp;
        bool isRepo = false;
        QVERIFY(beginAndResolve(&session, repoA, &rp, &isRepo));
        QCOMPARE(rp, repoA);
        QVERIFY(isRepo);
        session.finish();

        QVERIFY(beginAndResolve(&session, repoB, &rp, &isRepo));
        QCOMPARE(rp, repoB);
        QVERIFY(isRepo);
        session.finish();

        QVERIFY(beginAndResolve(&session, repoA, &rp, &isRepo));
        QCOMPARE(rp, repoA);  // back to A - last path wins
        QVERIFY(isRepo);
        session.finish();
        QVERIFY(!session.isActive());
    }

    void busySessionRejectsNewDrop() {
        const QString repoA = mkRepo("sess_busy_a", "a.txt", "aaa\n");
        const QString repoB = mkRepo("sess_busy_b", "b.txt", "bbb\n");
        RepositoryResolver resolver;
        DropOpenSession session(&resolver);
        QSignalSpy rejectedSpy(&session, &DropOpenSession::rejected);

        QVERIFY(beginAndResolve(&session, repoA));
        // While Opening (not finished), a new drop must be rejected and
        // the session must stay on the FIRST path.
        QVERIFY(!session.begin(repoB));
        QVERIFY(rejectedSpy.count() >= 1);
        QCOMPARE(session.path(), repoA);
        session.finish();

        // After finish, drops are accepted again.
        QVERIFY(beginAndResolve(&session, repoB));
        QCOMPARE(session.path(), repoB);
        session.finish();
    }
};

QTEST_MAIN(TestRepositoryOpenSession)
#include "test_repository_open_session.moc"
