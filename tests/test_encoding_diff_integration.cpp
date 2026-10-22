// v4-P0-R1.3: EncodingDiagnostic wired into the diff view flow.
// Scenarios:
//   1. UTF-8 markdown      -> normal text diff (no diagnosis)
//   2. UTF-16 LE markdown   -> binary verdict + UTF-16 LE diagnosis
//   3. Empty txt            -> text diff (deleted lines), NOT binary
//   4. Plain binary bytes   -> binary verdict + NUL diagnosis
// Verifies via the real MainWindow flow: select the changed file in the
// changes tree, wait for the async diff, read the diff viewer text.
#include <QtTest>
#include "test_settings_isolation.h"
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QLabel>
#include <QTreeWidget>
#include <QTextEdit>
#include "MainWindow.h"
#include "DiffViewerWidget.h"
#include "EncodingDiagnostic.h"
#include "GitController.h"

class TestEncodingDiffIntegration : public QObject {
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
        // v4-P6-21R6: go through the REAL repository-loading path so
        // m_repositorySessionReady is set (the direct controller path
        // bypasses the session gate and diff requests are dropped).
        QMetaObject::invokeMethod(w, "onProjectSelected",
                                  Q_ARG(QString, m_repoPath));
        QApplication::processEvents();
        QLabel *bl = w->findChild<QLabel *>("branchLabel");
        for (int i = 0; i < 50 && !(bl && bl->text().contains("main")); ++i) {
            QApplication::processEvents();
            QTest::qWait(300);
            bl = w->findChild<QLabel *>("branchLabel");
        }
        QApplication::processEvents();
        return w;
    }

    // Select the file in the changes tree; fills  once the async
    // diff has populated the viewer.
    void selectAndWaitDiff(MainWindow *w, const QString &fileName,
                           QString *outText) {
        auto *tree = w->findChild<QTreeWidget *>("changesTree");
        QVERIFY(tree);
        QTRY_VERIFY_WITH_TIMEOUT(tree->topLevelItemCount() >= 1, 10000);
        QApplication::processEvents();
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            if (tree->topLevelItem(i)->data(0, Qt::UserRole).toString()
                == fileName) {
                tree->setCurrentItem(tree->topLevelItem(i));
                break;
            }
        }
        QApplication::processEvents();
        auto *viewer = w->findChild<DiffViewerWidget *>("fileDiffViewer");
        QVERIFY(viewer);
        QTRY_VERIFY_WITH_TIMEOUT(!viewer->currentText().isEmpty(), 15000);
        *outText = viewer->currentText();
    }

private slots:
    void initTestCase() {
    isolateTestSettings();
        QVERIFY(m_dir.isValid());
        // v4-P6-21R6: the user home directory is a git work tree on
        // this machine - without GIT_CEILING a temp-dir fixture would
        // resolve into the parent work tree and `git diff -- <file>`
        // would return empty (file not tracked there) instead of the
        // real diff.
        qputenv("GIT_CEILING_DIRECTORIES", QDir::tempPath().toUtf8());
        m_repoPath = m_dir.path();
        QVERIFY(runGit({"init", "-q", "-b", "main"}));
        QVERIFY(runGit({"config", "user.name", "Test"}));
        QVERIFY(runGit({"config", "user.email", "t@t.local"}));
        QVERIFY(runGit({"config", "core.autocrlf", "false"}));
        // Tracked files used by the scenarios.
        QVERIFY(writeFile("README.md", "# Demo\n\nnormal text\n"));
        QVERIFY(writeFile("empty.txt", "keep this line\n"));
        QVERIFY(writeFile("data.bin", "original\n"));
        QVERIFY(runGit({"add", "-A"}));
        QVERIFY(runGit({"commit", "-q", "-m", "init"}));
    }

    void utf8MarkdownShowsTextDiff() {
        QVERIFY(writeFile("README.md", "# Demo\n\nadded line\n"));
        std::unique_ptr<MainWindow> w(openRepo());
        QString text;
        selectAndWaitDiff(w.get(), "README.md", &text);
        QVERIFY(text.contains("added line"));
        QVERIFY(!text.contains("二进制文件"));
        QVERIFY(!text.contains("UTF-16"));
    }

    void utf16MarkdownShowsBinaryDiagnosis() {
        // Rewrite README.md as UTF-16 LE with BOM.
        QByteArray utf16;
        utf16.append("\xFF\xFE", 2);
        for (const char c : QByteArray("# Demo\n\nutf16 line\n")) {
            utf16.append(c);
            utf16.append('\0');
        }
        QVERIFY(writeFile("README.md", utf16));
        std::unique_ptr<MainWindow> w(openRepo());
        QString text;
        selectAndWaitDiff(w.get(), "README.md", &text);
        QVERIFY(text.contains("二进制文件"));
        QVERIFY(text.contains("UTF-16 LE"));
        QVERIFY(text.contains("转换为 UTF-8"));
    }

    void emptyTxtShowsTextDiffNotBinary() {
        QVERIFY(writeFile("empty.txt", QByteArray()));  // emptied
        std::unique_ptr<MainWindow> w(openRepo());
        QString text;
        selectAndWaitDiff(w.get(), "empty.txt", &text);
        QVERIFY(!text.contains("二进制文件"));
        QVERIFY(!text.contains("UTF-16"));
    }

    void plainBinaryShowsNulDiagnosis() {
        // Binary-looking bytes without any BOM: NUL byte detection path.
        QByteArray bin;
        bin.append("PK\x03\x04");  // zip-like magic
        bin.append('\0');
        bin.append("binary payload");
        QVERIFY(writeFile("data.bin", bin));
        std::unique_ptr<MainWindow> w(openRepo());
        QString text;
        selectAndWaitDiff(w.get(), "data.bin", &text);
        QVERIFY(text.contains("二进制文件"));
        QVERIFY(text.contains("空字节"));
    }
};

QTEST_MAIN(TestEncodingDiffIntegration)
#include "test_encoding_diff_integration.moc"
