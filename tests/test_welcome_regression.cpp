// v4-P6-6 Welcome Page Regression Fix tests:
//   1. no "Release 管理" leak on the Welcome page
//   2. three quick-start titles complete
//   3. three quick-start signals fire
//   4. recent projects filter non-existing paths
//   5. test QSettings isolated from production QSettings
//   6. 700px-wide window: control geometry stays in bounds
#include <QtTest>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>
#include <QSettings>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QToolButton>
#include "WelcomeWidget.h"
#include "MainWindow.h"

class TestWelcomeRegression : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;

private slots:
    void initTestCase() {
        // v4-P6-6: this test must never touch production settings.
        // ProjectController uses IniFormat; redirect it into the test dir.
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_dir.path() + "/settings");
        QCoreApplication::setOrganizationName(QStringLiteral("QtGitStudioTest"));
        QCoreApplication::setApplicationName(QStringLiteral("QtGitStudioTest"));
        QVERIFY(m_dir.isValid());
    }

    void noReleaseManagementLeak() {
        MainWindow window;
        window.show();
        QApplication::processEvents();

        // The Welcome page must not contain any workspace-only widget.
        WelcomeWidget *welcome = window.findChild<WelcomeWidget *>();
        QVERIFY(welcome != nullptr);
        QVERIFY(welcome->isVisible());

        // "Release 管理" / "Git 工作流" must not be visible anywhere on
        // the Welcome page (they were leaking from the workspace header).
        const QList<QPushButton *> buttons =
            window.findChildren<QPushButton *>();
        for (const QPushButton *button : buttons) {
            if (button->isVisible()
                && (button->text().contains(QStringLiteral("Release"))
                    || button->text().contains(QStringLiteral("Git 工作流")))) {
                QFAIL(qPrintable(QStringLiteral("leaked button: %1")
                                     .arg(button->text())));
            }
        }
        // The workflow trigger is hidden until a repo is loaded.
        QToolButton *workflow = window.findChild<QToolButton *>("workflowButton");
        if (workflow) {
            QVERIFY(!workflow->isVisible());
        }
    }

    void quickStartTitlesComplete() {
        WelcomeWidget welcome;
        ActionBlock *openAction = welcome.findChild<ActionBlock *>("openAction");
        ActionBlock *initAction = welcome.findChild<ActionBlock *>("initAction");
        ActionBlock *cloneAction = welcome.findChild<ActionBlock *>("cloneAction");
        QVERIFY(openAction != nullptr);
        QVERIFY(initAction != nullptr);
        QVERIFY(cloneAction != nullptr);
        QCOMPARE(openAction->titleText(), QStringLiteral("打开仓库"));
        QCOMPARE(initAction->titleText(), QStringLiteral("初始化仓库"));
        QCOMPARE(cloneAction->titleText(), QStringLiteral("克隆仓库"));
        // No nested QPushButton inside the blocks.
        QVERIFY(openAction->findChildren<QPushButton *>().isEmpty());
        QVERIFY(initAction->findChildren<QPushButton *>().isEmpty());
        QVERIFY(cloneAction->findChildren<QPushButton *>().isEmpty());
    }

    void quickStartSignals() {
        WelcomeWidget welcome;
        QSignalSpy openSpy(&welcome, &WelcomeWidget::openProjectRequested);
        QSignalSpy initSpy(&welcome, &WelcomeWidget::initProjectRequested);
        QSignalSpy cloneSpy(&welcome, &WelcomeWidget::cloneProjectRequested);

        welcome.resize(960, 720);
        welcome.show();
        QVERIFY(QTest::qWaitForWindowExposed(&welcome));

        ActionBlock *openAction = welcome.findChild<ActionBlock *>("openAction");
        ActionBlock *initAction = welcome.findChild<ActionBlock *>("initAction");
        ActionBlock *cloneAction = welcome.findChild<ActionBlock *>("cloneAction");
        QTest::mouseClick(openAction, Qt::LeftButton);
        QTest::mouseClick(initAction, Qt::LeftButton);
        QTest::mouseClick(cloneAction, Qt::LeftButton);
        QApplication::processEvents();
        QCOMPARE(openSpy.count(), 1);
        QCOMPARE(initSpy.count(), 1);
        QCOMPARE(cloneSpy.count(), 1);
    }

    void recentProjectsFilterInvalidPaths() {
        WelcomeWidget welcome;
        // One real directory, one deleted path, one file (not a dir).
        const QString real = m_dir.path() + "/real-project";
        QDir().mkpath(real);
        const QString deleted = m_dir.path() + "/deleted-project";
        QDir().mkpath(deleted);
        QDir().rmdir(deleted);  // no longer exists
        const QString plainFile = m_dir.path() + "/plain.txt";
        QFile f(plainFile);
        f.open(QIODevice::WriteOnly);
        f.write("x");
        f.close();

        welcome.updateRecentProjects({real, deleted, plainFile});
        // v4-P6-15: rows are QFrame children of recentContainer.
        QFrame *container = welcome.findChild<QFrame *>("recentContainer");
        QVERIFY(container != nullptr);
        auto rowCount = [container]() {
            int n = 0;
            const auto all = container->findChildren<QFrame *>();
            for (QFrame *child : all) {
                if (child->parent() == container) {
                    ++n;
                }
            }
            return n;
        };
        QCOMPARE(rowCount(), 1);

        // All paths invalid -> empty placeholder, no crash.
        welcome.updateRecentProjects({deleted});
        QVERIFY(rowCount() >= 0);
    }

    void settingsIsolation() {
        // The same QSettings construction ProjectController uses must
        // resolve into the TEST directory after QSettings::setPath,
        // never into the production ini file.
        QSettings productionLike(QSettings::IniFormat, QSettings::UserScope,
                                 QStringLiteral("QtGitStudio"),
                                 QStringLiteral("QtGitStudio"));
        const QString fileName = productionLike.fileName();
        QVERIFY2(fileName.endsWith(QStringLiteral(".ini")),
                 qPrintable(QStringLiteral("expected ini file, got: %1")
                                .arg(fileName)));
        QVERIFY2(fileName.contains(m_dir.path()),
                 qPrintable(QStringLiteral(
                                "test settings not isolated, path: %1")
                                .arg(fileName)));
    }

    void geometryFitsAt700px() {
        WelcomeWidget welcome;
        const QString real = m_dir.path() + "/geom-project";
        QDir().mkpath(real);
        welcome.updateRecentProjects({real});
        welcome.resize(700, 900);
        welcome.show();
        QVERIFY(QTest::qWaitForWindowExposed(&welcome));
        QApplication::processEvents();

        // No widget may overflow the window bounds horizontally.
        const QList<QWidget *> all = welcome.findChildren<QWidget *>();
        for (const QWidget *widget : all) {
            if (!widget->isVisible() || widget == &welcome) continue;
            const QPoint bottomRight =
                widget->mapTo(&welcome, widget->rect().bottomRight());
            QVERIFY2(bottomRight.x() <= welcome.width() + 1,
                     qPrintable(QStringLiteral("overflow x=%1 for %2")
                                    .arg(bottomRight.x())
                                    .arg(widget->objectName())));
            QVERIFY2(bottomRight.y() <= welcome.height() + 1,
                     qPrintable(QStringLiteral("overflow y=%1 for %2")
                                    .arg(bottomRight.y())
                                    .arg(widget->objectName())));
        }

        // Quick-start blocks must be fully visible (no truncation).
        ActionBlock *openAction = welcome.findChild<ActionBlock *>("openAction");
        ActionBlock *cloneAction = welcome.findChild<ActionBlock *>("cloneAction");
        QVERIFY(openAction->isVisible());
        QVERIFY(cloneAction->isVisible());
        QVERIFY(openAction->mapTo(&welcome, openAction->rect().bottomRight()).x()
                <= welcome.width() + 1);
        QVERIFY(cloneAction->mapTo(&welcome, cloneAction->rect().bottomRight()).x()
                <= welcome.width() + 1);

        // v4-P6-15: at 700px the dashboard is single-column - the
        // quick-start blocks share the same left edge (stacked).
        const int openX =
            openAction->mapTo(&welcome, QPoint(0, 0)).x();
        const int cloneX =
            cloneAction->mapTo(&welcome, QPoint(0, 0)).x();
        QVERIFY2(qAbs(openX - cloneX) <= 2,
                 qPrintable(QString("blocks not stacked: open=%1 clone=%2")
                                .arg(openX).arg(cloneX)));
    }
};

QTEST_MAIN(TestWelcomeRegression)
#include "test_welcome_regression.moc"
