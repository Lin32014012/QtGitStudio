// v4-P2-3 Stability Finalization: repeat drag-and-drop of the same
// repository. Simulates REAL drop events by constructing QDropEvent and
// invoking MainWindow::dropEvent directly (public inheritance exposes
// the protected handler) - this deliberately does NOT rely on
// QApplication::sendEvent, which does not dispatch drop events on the
// offscreen platform. Scenarios:
//   A -> A                 (same repo twice, sequential sessions)
//   A -> A -> A ... x10    (no deadlock, no overlay leak)
//   A -> B -> A            (generation: last project wins)
#include <QtTest>
#include "test_settings_isolation.h"
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QLabel>
#include <QMimeData>
#include <QUrl>
#include <QDropEvent>
#include <QApplication>
#include "MainWindow.h"
#include "DropOpenSession.h"

namespace {

// Expose the protected drop handler for direct invocation.
class DropTestWindow : public MainWindow {
public:
    using MainWindow::dropEvent;
};

}  // namespace

class TestRepeatDropStability : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    QString m_repoA;
    QString m_repoB;

    bool runGit(const QString &dir, const QStringList &args) {
        QProcess git;
        git.setWorkingDirectory(dir);
        git.start("git", args);
        if (!git.waitForFinished(15000)) return false;
        return git.exitStatus() == QProcess::NormalExit && git.exitCode() == 0;
    }

    QString mkRepo(const QString &dir, const QString &file, const QByteArray &content) {
        QDir().mkpath(dir);
        runGit(dir, {"init", "-q", "-b", "main"});
        runGit(dir, {"config", "user.name", "Test"});
        runGit(dir, {"config", "user.email", "t@t.local"});
        QFile f(dir + "/" + file);
        f.open(QIODevice::WriteOnly);
        f.write(content);
        f.close();
        runGit(dir, {"add", "-A"});
        runGit(dir, {"commit", "-q", "-m", "init"});
        return dir;
    }

    // Build a real QDropEvent and call the (exposed) handler directly.
    void sendDrop(MainWindow *w, const QString &path) {
        auto *mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(path)});
        QDropEvent ev(QPointF(10, 10), Qt::CopyAction, mime,
                      Qt::LeftButton, Qt::NoModifier);
        static_cast<DropTestWindow *>(w)->dropEvent(&ev);
        QApplication::processEvents();
        delete mime;
    }

    // Wait until the project label shows the expected repo name and the
    // drop overlay is hidden.
    bool waitOpened(MainWindow *w, const QString &repoDir) {
        const QString expected = "当前项目：" + QFileInfo(repoDir).fileName();
        QLabel *pl = w->findChild<QLabel *>("projectLabel");
        QLabel *ov = w->findChild<QLabel *>("dropOverlay");
        for (int i = 0; i < 60; ++i) {
            QApplication::processEvents();
            QTest::qWait(300);
            if (pl && pl->text().contains(expected) && ov && !ov->isVisible()) {
                return true;
            }
        }
        return false;
    }

private slots:
    void initTestCase() {
    isolateTestSettings();
        // v4-P6-6: test settings isolated from production (see
        // test_settings_isolation.h).
        QVERIFY(m_dir.isValid());
        m_repoA = mkRepo(m_dir.path() + "/repoA", "a.txt", "aaa\n");
        m_repoB = mkRepo(m_dir.path() + "/repoB", "b.txt", "bbb\n");
    }

    void dropSameRepositoryTwice() {
        DropTestWindow w;
        w.show();
        QApplication::processEvents();

        sendDrop(&w, m_repoA);
        QVERIFY(waitOpened(&w, m_repoA));

        sendDrop(&w, m_repoA);
        QVERIFY(waitOpened(&w, m_repoA));

        auto *ov = w.findChild<QLabel *>("dropOverlay");
        QVERIFY(ov && !ov->isVisible());
    }

    void dropSameRepositoryTenTimes() {
        DropTestWindow w;
        w.show();
        QApplication::processEvents();

        for (int i = 0; i < 10; ++i) {
            sendDrop(&w, m_repoA);
            QVERIFY2(waitOpened(&w, m_repoA),
                     qPrintable(QString("iteration %1 failed").arg(i + 1)));
        }

        auto *ov = w.findChild<QLabel *>("dropOverlay");
        QVERIFY(ov && !ov->isVisible());
    }

    void dropAThenBThenA() {
        DropTestWindow w;
        w.show();
        QApplication::processEvents();

        sendDrop(&w, m_repoA);
        QVERIFY(waitOpened(&w, m_repoA));

        sendDrop(&w, m_repoB);
        QVERIFY(waitOpened(&w, m_repoB));

        sendDrop(&w, m_repoA);
        QVERIFY(waitOpened(&w, m_repoA));

        QLabel *pl = w.findChild<QLabel *>("projectLabel");
        QVERIFY(pl && pl->text().contains(QFileInfo(m_repoA).fileName()));
    }
};

QTEST_MAIN(TestRepeatDropStability)
#include "test_repeat_drop_stability.moc"
