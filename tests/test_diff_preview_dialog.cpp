// v4-P6-17 diff preview dialog tests (side-by-side panes):
//   title / side columns / zoom in-out-reset / maximize / close.
#include <QtTest>
#include <QPlainTextEdit>
#include <QPushButton>
#include "DiffPreviewDialog.h"

class TestDiffPreviewDialog : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
    }

    void titleAndContent() {
        DiffPreviewDialog dialog(
            QStringLiteral("--- a/x.txt\n+++ b/x.txt\n@@ -1 +1 @@\n-1\n+2\n"),
            QStringLiteral("8f44d5c1234567890abcdef1234567890abcdef12"),
            QStringLiteral("src/MainWindow.cpp"));
        QCOMPARE(dialog.windowTitle(),
                 QStringLiteral("Diff 预览 — 8f44d5c — src/MainWindow.cpp"));
        // v4-P6-17: side-by-side - old lines in the left pane, new
        // lines in the right pane; context/@@ lines in both.
        QPlainTextEdit *left = dialog.leftEdit();
        QPlainTextEdit *right = dialog.rightEdit();
        QVERIFY(left != nullptr);
        QVERIFY(right != nullptr);
        QVERIFY(left->toPlainText().contains(QStringLiteral("a/x.txt")));
        QVERIFY(left->toPlainText().contains(QStringLiteral("-1")));
        QVERIFY(right->toPlainText().contains(QStringLiteral("+2")));
        // Default 12pt monospace.
        QVERIFY(dialog.currentFontSize() == 12);
        QVERIFY(left->font().pointSize() == 12);
        QVERIFY(right->font().pointSize() == 12);
    }

    void zoomInOutReset() {
        DiffPreviewDialog dialog(QStringLiteral("diff"), QString(), QString());
        const int base = dialog.currentFontSize();
        dialog.zoomIn();
        QCOMPARE(dialog.currentFontSize(), base + 1);
        QCOMPARE(dialog.leftEdit()->font().pointSize(), base + 1);
        QCOMPARE(dialog.rightEdit()->font().pointSize(), base + 1);
        dialog.zoomOut();
        QCOMPARE(dialog.currentFontSize(), base);
        dialog.resetZoom();
        QCOMPARE(dialog.currentFontSize(), 12);
    }

    void maximizeAndClose() {
        DiffPreviewDialog dialog(QStringLiteral("diff"), QString(), QString());
        dialog.showMaximized();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QVERIFY(dialog.isMaximized() || dialog.width() >= 640);
        dialog.close();
        QVERIFY(!dialog.isVisible());
    }

    // --- v4-P6-21 ISSUE-11: asymmetric edits must keep both columns
    // the same row count; context after unequal edits stays aligned. ---
    void alignedTwoDeletesOneAdd() {
        DiffPreviewDialog dialog(
            QStringLiteral("@@ -1,2 +1,1 @@\n-a\n-b\n+c\n"), QString(), QString());
        const QStringList left =
            dialog.leftEdit()->toPlainText().split('\n');
        const QStringList right =
            dialog.rightEdit()->toPlainText().split('\n');
        QCOMPARE(left.size(), right.size());
        // -a and -b pair with +c; no orphaned rows.
        QVERIFY(left.contains(QStringLiteral("-a")));
        QVERIFY(left.contains(QStringLiteral("-b")));
        QVERIFY(right.contains(QStringLiteral("+c")));
    }

    void alignedOneDeleteThreeAdds() {
        DiffPreviewDialog dialog(
            QStringLiteral("@@ -1 +1,3 @@\n-a\n+b1\n+b2\n+b3\n"), QString(), QString());
        const QStringList left =
            dialog.leftEdit()->toPlainText().split('\n');
        const QStringList right =
            dialog.rightEdit()->toPlainText().split('\n');
        QCOMPARE(left.size(), right.size());
        QVERIFY(right.contains(QStringLiteral("+b3")));
    }

    void alignedMultipleHunks() {
        DiffPreviewDialog dialog(
            QStringLiteral("@@ -1 +1 @@\n-a\n+b\n@@ -5 +5,2 @@\n ctx\n-c\n+d1\n+d2\n"), QString(), QString());
        const QStringList left =
            dialog.leftEdit()->toPlainText().split('\n');
        const QStringList right =
            dialog.rightEdit()->toPlainText().split('\n');
        QCOMPARE(left.size(), right.size());
    }

    void alignedContextAfterUnequalChange() {
        DiffPreviewDialog dialog(
            QStringLiteral("@@ -1,4 +1,3 @@\n-a\n-b\n+c\n context1\n context2\n"), QString(), QString());
        const QStringList left =
            dialog.leftEdit()->toPlainText().split('\n');
        const QStringList right =
            dialog.rightEdit()->toPlainText().split('\n');
        QCOMPARE(left.size(), right.size());
        // context1 must sit at the SAME visual row in both columns.
        const int li = left.indexOf(QStringLiteral(" context1"));
        const int ri = right.indexOf(QStringLiteral(" context1"));
        QVERIFY(li >= 0);
        QCOMPARE(li, ri);
        const int li2 = left.indexOf(QStringLiteral(" context2"));
        const int ri2 = right.indexOf(QStringLiteral(" context2"));
        QVERIFY(li2 >= 0);
        QCOMPARE(li2, ri2);
    }
};

QTEST_MAIN(TestDiffPreviewDialog)
#include "test_diff_preview_dialog.moc"
