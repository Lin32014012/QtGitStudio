// v4-P0-R1.3 Stability Fix regression tests.
// 1. Stage All write-operation mutex (task 2) — behaviour verification
// 2. Encoding diagnostic detection (task 3) — pure function tests
// 3. Repeat-drop: NOT tested (no drag-drop code in this baseline).
#include <QLabel>
#include "test_settings_isolation.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QPushButton>
#include <QTextEdit>
#include "MainWindow.h"
#include "ActionButtonFeedback.h"
#include "GitController.h"
#include "GitSnapshot.h"
#include "EncodingDiagnostic.h"

using namespace Qt::Literals::StringLiterals;

class TestStabilityRegression : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    QString m_repoPath;

    bool runGit(const QStringList &args) {
        QProcess git;
        git.setWorkingDirectory(m_repoPath);
        git.start("git", args);
        if (!git.waitForFinished(15000)) return false;
        return git.exitStatus() == QProcess::NormalExit && git.exitCode() == 0;
    }

    bool writeFile(const QString &name, const QByteArray &data) {
        QFile f(m_repoPath + "/" + name);
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write(data);
        return true;
    }

    MainWindow *openRepo() {
        auto *w = new MainWindow;
        QMetaObject::invokeMethod(w, "showWorkspace");
        w->controller()->setRepositoryPath(m_repoPath);
        w->controller()->refresh();
        QApplication::processEvents();
        // Poll until the branch label shows "main".
        QLabel *bl = w->findChild<QLabel *>("branchLabel");
        for (int i = 0; i < 50 && !(bl && bl->text().contains("main")); ++i) {
            QApplication::processEvents();
            QTest::qWait(300);
            bl = w->findChild<QLabel *>("branchLabel");
        }
        QApplication::processEvents();
        return w;
    }

private slots:
    void initTestCase() {
    isolateTestSettings();
        QVERIFY(m_dir.isValid());
        m_repoPath = m_dir.path();
        QVERIFY(runGit({"init", "-q", "-b", "main"}));
        QVERIFY(runGit({"config", "user.name", "Test"}));
        QVERIFY(runGit({"config", "user.email", "t@t.local"}));
        QVERIFY(writeFile("README.md", "# test\n"));
        QVERIFY(runGit({"add", "-A"}));
        QVERIFY(runGit({"commit", "-q", "-m", "init"}));
    }

    void stageAllNoErrorOnIdleWindow() {
        std::unique_ptr<MainWindow> w(openRepo());
        QMetaObject::invokeMethod(w.get(), "onStageAll");
        // Wait for button feedback cycle to complete.
        auto *btn = w->findChild<QPushButton *>("stageAllButton");
        QTRY_VERIFY_WITH_TIMEOUT(!ActionButtonFeedback::isActive(btn), 5000);
        QApplication::processEvents();
        auto *err = w->findChild<QTextEdit *>("errorDisplay");
        QVERIFY(err);
        QVERIFY(!err->toPlainText().contains("失败"));
    }

    void stageAllBlockedWhileBusy() {
        std::unique_ptr<MainWindow> w(openRepo());
        auto *btn = w->findChild<QPushButton *>("stageAllButton");
        QVERIFY(btn);
        ActionButtonFeedback::showBusy(btn, "busy");
        QApplication::processEvents();

        QMetaObject::invokeMethod(w.get(), "onStageAll");
        QApplication::processEvents();
        QTest::qWait(300);
        QApplication::processEvents();

        auto *err = w->findChild<QTextEdit *>("errorDisplay");
        QVERIFY(err);
        QVERIFY(!err->toPlainText().contains("失败"));
    }

    void unstageAllBlockedWhileBusy() {
        std::unique_ptr<MainWindow> w(openRepo());
        auto *commitBtn = w->findChild<QPushButton *>("commitButton");
        QVERIFY(commitBtn);
        ActionButtonFeedback::showBusy(commitBtn, "busy");
        QApplication::processEvents();

        QMetaObject::invokeMethod(w.get(), "onUnstageAll");
        QApplication::processEvents();
        QTest::qWait(300);
        QApplication::processEvents();

        auto *err = w->findChild<QTextEdit *>("errorDisplay");
        QVERIFY(err);
        QVERIFY(!err->toPlainText().contains("失败"));
    }

    void utf16LeDetected() {
        QByteArray data;
        data.append("\xFF\xFE", 2);
        for (const char c : QByteArray("hello")) { data.append(c); data.append('\0'); }
        auto d = diagnoseEncoding(data);
        QCOMPARE(d.kind, EncodingDiagnostic::Kind::Utf16LeBom);
        QVERIFY(d.isBinaryCause());
        QVERIFY(d.explanation().contains("UTF-16 LE"));
    }

    void utf16BeDetected() {
        QByteArray data;
        data.append("\xFE\xFF", 2);
        for (const char c : QByteArray("hello")) { data.append('\0'); data.append(c); }
        auto d = diagnoseEncoding(data);
        QCOMPARE(d.kind, EncodingDiagnostic::Kind::Utf16BeBom);
        QVERIFY(d.isBinaryCause());
        QVERIFY(d.explanation().contains("UTF-16 BE"));
    }

    void utf8BomReportedNotBinaryCause() {
        QByteArray data;
        data.append("\xEF\xBB\xBF", 3);
        data.append("normal text");
        auto d = diagnoseEncoding(data);
        QCOMPARE(d.kind, EncodingDiagnostic::Kind::Utf8Bom);
        QVERIFY(!d.isBinaryCause());
    }

    void nullBytesDetected() {
        QByteArray data("text with \0 inside", 19);
        auto d = diagnoseEncoding(data);
        QCOMPARE(d.kind, EncodingDiagnostic::Kind::NullBytes);
        QVERIFY(d.isBinaryCause());
        QVERIFY(d.hasNullBytes);
    }

    void cleanUtf8NotMisdiagnosed() {
        auto d = diagnoseEncoding("Hello world\n");
        QCOMPARE(d.kind, EncodingDiagnostic::Kind::None);
        QVERIFY(!d.isBinaryCause());
        QVERIFY(!d.hasNullBytes);
    }
};

QTEST_MAIN(TestStabilityRegression)
#include "test_stability_regression.moc"
