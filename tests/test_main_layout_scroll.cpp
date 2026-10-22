// v4-P6-9 Main UI layout scroll tests:
//   1. workspace content lives in a QScrollArea (widgetResizable)
//   2. the top bar is NOT inside the scroll area (stays fixed)
//   3. small windows get a usable vertical scrollbar
//   4. scrolling to the bottom reaches the file-changes area
//   5. no horizontal overflow at narrow widths
#include <QtTest>
#include <QApplication>
#include <QScrollArea>
#include <QScrollBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QElapsedTimer>
#include "MainWindow.h"
#include "GitTestRepository.h"
#include "test_settings_isolation.h"

class TestMainLayoutScroll : public QObject {
    Q_OBJECT
private:
    GitTestRepository m_repo;

    void switchToWorkspace(MainWindow &window) {
        // v4-P6-12: workspace requires a loaded repository; load the
        // test repo (async) and wait for the workspace page (index 1).
        QMetaObject::invokeMethod(&window, "onProjectSelected",
                                  Q_ARG(QString, m_repo.path()));
        QStackedWidget *stack = window.findChild<QStackedWidget *>();
        QElapsedTimer timer;
        timer.start();
        while (stack && stack->currentIndex() != 1
               && timer.elapsed() < 15000) {
            QApplication::processEvents();
            QTest::qWait(20);
        }
        QApplication::processEvents();
    }

private slots:
    void initTestCase() {
        isolateTestSettings();
        QVERIFY(m_repo.isValid());
    }

    void workspaceUsesScrollArea() {
        MainWindow window;
        switchToWorkspace(window);

        QScrollArea *scroll =
            window.findChild<QScrollArea *>("workspaceScrollArea");
        QVERIFY(scroll != nullptr);
        QVERIFY(scroll->widgetResizable());

        // The top bar must not be inside the scroll area.
        QPushButton *home = window.findChild<QPushButton *>("goHomeButton");
        QPushButton *learn = window.findChild<QPushButton *>("learnButton");
        QVERIFY(home != nullptr);
        QVERIFY(learn != nullptr);
        QVERIFY(!scroll->isAncestorOf(home));
        QVERIFY(!scroll->isAncestorOf(learn));

        // The scroll area is inside the workspace page.
        QStackedWidget *stack = window.findChild<QStackedWidget *>();
        QVERIFY(stack != nullptr);
        QWidget *workspace = stack->widget(1);
        QVERIFY(workspace != nullptr);
        QVERIFY(workspace->isAncestorOf(scroll));
    }

    void smallWindowScrollsToBottom() {
        MainWindow window;
        window.resize(722, 900);
        window.show();
        switchToWorkspace(window);
        QApplication::processEvents();

        QScrollArea *scroll =
            window.findChild<QScrollArea *>("workspaceScrollArea");
        QVERIFY(scroll != nullptr);
        QVERIFY(scroll->isVisible());

        // Content is taller than the viewport -> vertical scrollbar usable.
        QScrollBar *vbar = scroll->verticalScrollBar();
        QVERIFY(vbar != nullptr);
        QVERIFY2(vbar->maximum() > 0,
                 qPrintable(QString("vertical scrollbar max=%1")
                                .arg(vbar->maximum())));

        // No horizontal overflow.
        QScrollBar *hbar = scroll->horizontalScrollBar();
        QVERIFY(hbar->maximum() <= 1);

        // Scroll to the bottom: the file-changes tree becomes visible.
        vbar->setValue(vbar->maximum());
        QApplication::processEvents();
        QTreeWidget *fileTree =
            window.findChild<QTreeWidget *>("filesTree");
        if (fileTree) {
            const QPoint topLeft =
                fileTree->mapTo(&window, QPoint(0, 0));
            QVERIFY2(topLeft.y() < window.height(),
                     qPrintable(QString("file tree off-screen y=%1")
                                    .arg(topLeft.y())));
        }
        // Diff viewer reachable through the scroll area.
        QWidget *diffViewer = window.findChild<QWidget *>("fileDiffViewer");
        if (diffViewer) {
            const QPoint topLeft =
                diffViewer->mapTo(&window, QPoint(0, 0));
            QVERIFY2(topLeft.y() < window.height(),
                     qPrintable(QString("diff off-screen y=%1")
                                    .arg(topLeft.y())));
        }
    }

    void noHorizontalOverflowAt960() {
        MainWindow window;
        window.resize(960, 720);
        window.show();
        switchToWorkspace(window);
        QApplication::processEvents();

        QScrollArea *scroll =
            window.findChild<QScrollArea *>("workspaceScrollArea");
        QVERIFY(scroll != nullptr);
        QVERIFY(scroll->horizontalScrollBar()->maximum() <= 1);
    }

    // v4-P6-12: without a repository the workspace must NOT show its
    // empty panels - a compact guide page with three entries is shown
    // instead, and the workspace restores after a repo loads.
    void noRepositoryShowsGuidePage() {
        MainWindow window;
        window.show();
        QApplication::processEvents();

        QStackedWidget *stack = window.findChild<QStackedWidget *>();
        QVERIFY(stack != nullptr);
        // Starts on the welcome page.
        QCOMPARE(stack->currentIndex(), 0);

        // Requesting the workspace without a repo routes to the guide.
        QMetaObject::invokeMethod(&window, "showWorkspace");
        QApplication::processEvents();
        QWidget *noRepo = window.findChild<QWidget *>("noRepoWidget");
        QVERIFY(noRepo != nullptr);
        QVERIFY(noRepo->isVisible());
        QVERIFY(noRepo->isAncestorOf(stack->currentWidget()));

        // Three entry buttons + back button present and visible.
        QPushButton *openBtn =
            window.findChild<QPushButton *>("noRepoOpenButton");
        QPushButton *initBtn =
            window.findChild<QPushButton *>("noRepoInitButton");
        QPushButton *cloneBtn =
            window.findChild<QPushButton *>("noRepoCloneButton");
        QPushButton *backBtn =
            window.findChild<QPushButton *>("noRepoBackButton");
        QVERIFY(openBtn != nullptr && openBtn->isVisible());
        QVERIFY(initBtn != nullptr && initBtn->isVisible());
        QVERIFY(cloneBtn != nullptr && cloneBtn->isVisible());
        QVERIFY(backBtn != nullptr && backBtn->isVisible());

        // Workspace controls must NOT be visible.
        QTreeWidget *branchTree =
            window.findChild<QTreeWidget *>("branchTree");
        QWidget *remoteCard = window.findChild<QWidget *>("remoteCard");
        QVERIFY(branchTree != nullptr);
        QVERIFY(remoteCard != nullptr);
        QVERIFY(!branchTree->isVisible());
        QVERIFY(!remoteCard->isVisible());

        // Loading a repository restores the full workspace.
        QMetaObject::invokeMethod(&window, "onProjectSelected",
                                  Q_ARG(QString, m_repo.path()));
        QElapsedTimer timer;
        timer.start();
        while (stack->currentIndex() != 1 && timer.elapsed() < 15000) {
            QApplication::processEvents();
            QTest::qWait(20);
        }
        QCOMPARE(stack->currentIndex(), 1);
        QVERIFY(branchTree->isVisible());
        QVERIFY(remoteCard->isVisible());
    }
};

// v4-P6-17: the test forces the offscreen platform itself (instead of
// relying on the caller's environment) - under ctest the windows
// platform crashes this test with STATUS_INVALID_HANDLE (exit 8)
// because ctest spawns without a window station. qputenv must run
// before QApplication is constructed.
int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    TestMainLayoutScroll tc;
    return QTest::qExec(&tc, argc, argv);
}
#include "test_main_layout_scroll.moc"
