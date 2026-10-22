// v4-P0-R1.3 stability: stage single file, then Stage All.
// Verifies the full user flow: select file -> stage it -> wait for the
// refresh chain -> Stage All uses the LATEST snapshot (no stale file list,
// no index.lock from concurrent writes, no error shown).
#include <QtTest>
#include "test_settings_isolation.h"
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QPushButton>
#include <QTreeWidget>
#include <QTextEdit>
#include <QLabel>
#include "MainWindow.h"
#include "ActionButtonFeedback.h"
#include "GitController.h"
#include "GitSnapshot.h"

class TestStageAfterSelectedStage : public QObject {
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
        // m_repositorySessionReady is set (direct controller path
        // bypasses the session gate and operations are dropped).
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

private slots:
        void initTestCase() {
        // v4-P6-21R6: user home is a git work tree on this machine - temp-dir
        // fixtures must cap git discovery at %TEMP% or commands resolve into
        // the parent work tree (false-green/empty results).
        qputenv("GIT_CEILING_DIRECTORIES", QDir::tempPath().toUtf8());
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

    void singleStageThenStageAll() {
        // Three modified files:  M A.txt  M B.txt  M C.txt
        QVERIFY(writeFile("A.txt", "aaa\n"));
        QVERIFY(writeFile("B.txt", "bbb\n"));
        QVERIFY(writeFile("C.txt", "ccc\n"));
        std::unique_ptr<MainWindow> w(openRepo());

        auto *tree = w->findChild<QTreeWidget *>("changesTree");
        QVERIFY(tree);
        QTRY_VERIFY_WITH_TIMEOUT(tree->topLevelItemCount() == 3, 10000);
        QApplication::processEvents();

        // Select A.txt and stage it.
        QTreeWidgetItem *itemA = nullptr;
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            if (tree->topLevelItem(i)->data(0, Qt::UserRole).toString() == "A.txt") {
                itemA = tree->topLevelItem(i);
                break;
            }
        }
        QVERIFY(itemA);
        tree->setCurrentItem(itemA);
        QApplication::processEvents();

        auto *stageBtn = w->findChild<QPushButton *>("stageButton");
        QVERIFY(stageBtn);
        QMetaObject::invokeMethod(w.get(), "onStageSelected");
        // Wait for the stage + refresh chain to finish.
        QTRY_VERIFY_WITH_TIMEOUT(!w->controller()->viewModel()->isLoading(), 10000);
        QApplication::processEvents();
        QTest::qWait(300);
        QApplication::processEvents();

        // A.txt must now be staged: git status shows "A  A.txt".
        QProcess git;
        git.setWorkingDirectory(m_repoPath);
        git.start("git", {"status", "--porcelain=v1"});
        QVERIFY(git.waitForFinished(15000));
        const QString status = QString::fromUtf8(git.readAllStandardOutput());
        qDebug() << "DIAG status=[" << status << "]";
        QVERIFY(status.contains("A  A.txt"));
        QVERIFY(status.contains("?? B.txt"));
        QVERIFY(status.contains("?? C.txt"));

        // Now Stage All: must succeed with the LATEST snapshot (2 files).
        QMetaObject::invokeMethod(w.get(), "onStageAll");
        QTRY_VERIFY_WITH_TIMEOUT(!w->controller()->viewModel()->isLoading(), 10000);
        QApplication::processEvents();
        QTest::qWait(300);
        QApplication::processEvents();

        QProcess git2;
        git2.setWorkingDirectory(m_repoPath);
        git2.start("git", {"status", "--porcelain=v1"});
        QVERIFY(git2.waitForFinished(15000));
        const QString finalStatus = QString::fromUtf8(git2.readAllStandardOutput());
        QVERIFY(finalStatus.contains("A  A.txt"));
        QVERIFY(finalStatus.contains("A  B.txt"));
        QVERIFY(finalStatus.contains("A  C.txt"));

        auto *err = w->findChild<QTextEdit *>("errorDisplay");
        QVERIFY(err);
        QVERIFY(!err->toPlainText().contains("失败"));
    }

    void stageWhileBusyIsRejected() {
        QVERIFY(writeFile("D.txt", "ddd\n"));
        std::unique_ptr<MainWindow> w(openRepo());

        auto *tree = w->findChild<QTreeWidget *>("changesTree");
        QVERIFY(tree);
        QTRY_VERIFY_WITH_TIMEOUT(tree->topLevelItemCount() >= 1, 10000);
        QApplication::processEvents();

        auto *stageBtn = w->findChild<QPushButton *>("stageButton");
        QVERIFY(stageBtn);
        ActionButtonFeedback::showBusy(stageBtn, "busy");
        QApplication::processEvents();

        // onStageSelected must be silently rejected (no error).
        QMetaObject::invokeMethod(w.get(), "onStageSelected");
        QApplication::processEvents();
        QTest::qWait(300);
        QApplication::processEvents();

        auto *err = w->findChild<QTextEdit *>("errorDisplay");
        QVERIFY(err);
        QVERIFY(!err->toPlainText().contains("失败"));
    }
};

QTEST_MAIN(TestStageAfterSelectedStage)
#include "test_stage_after_selected_stage.moc"
