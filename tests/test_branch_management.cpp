// v4-P1-1 Branch Management Enhancement regression tests.
// Covers:
//   1. create branch success
//   2. create duplicate branch fails
//   3. switch branch success
//   4. current branch cannot be deleted (UI guard)
//   5. delete merged branch succeeds
//   6. delete unmerged branch is rejected unless forced (Git guard)
// Each test uses a FRESH repository to avoid cross-test state leakage.
#include <QtTest>
#include "test_settings_isolation.h"
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QLabel>
#include <QTreeWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QLineEdit>
#include "MainWindow.h"
#include "ActionButtonFeedback.h"
#include "GitController.h"
#include "GitSnapshot.h"

class TestBranchManagement : public QObject {
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

    // Fresh repo used by one test only.
    void mkRepo(const QString &tag) {
        m_repoPath = m_dir.path() + "/" + tag;
        QDir().mkpath(m_repoPath);
        QVERIFY(runGit({"init", "-q", "-b", "main"}));
        QVERIFY(runGit({"config", "user.name", "Test"}));
        QVERIFY(runGit({"config", "user.email", "t@t.local"}));
        QVERIFY(writeFile("README.md", "# test\n"));
        QVERIFY(runGit({"add", "-A"}));
        QVERIFY(runGit({"commit", "-q", "-m", "init"}));
    }

    QString gitBranchList() {
        QProcess git;
        git.setWorkingDirectory(m_repoPath);
        git.start("git", {"branch", "--format=%(refname:short)"});
        if (!git.waitForFinished(15000)) return {};
        return QString::fromUtf8(git.readAllStandardOutput());
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

    // Select a branch item in the branch tree by its BranchNameRole data.
    bool selectBranch(MainWindow *w, const QString &name) {
        auto *tree = w->findChild<QTreeWidget *>("branchTree");
        if (!tree) {
            return false;
        }
        QTreeWidgetItemIterator it(tree);
        while (*it) {
            if ((*it)->data(0, Qt::UserRole).toString() == name) {
                tree->setCurrentItem(*it);
                return true;
            }
            ++it;
        }
        return false;
    }

private slots:
        void initTestCase() {
        // v4-P6-21R6: user home is a git work tree on this machine - temp-dir
        // fixtures must cap git discovery at %TEMP% or commands resolve into
        // the parent work tree (false-green/empty results).
        qputenv("GIT_CEILING_DIRECTORIES", QDir::tempPath().toUtf8());
    isolateTestSettings();
        QVERIFY(m_dir.isValid());
    }

    void createBranchSucceeds() {
        mkRepo("r_create");
        std::unique_ptr<MainWindow> w(openRepo());
        bool done = false;
        w->controller()->createBranchAsync(
            "feature/test", "main", false,
            [&done](GitController::OperationResult r) {
                QVERIFY(r.success);
                done = true;
            });
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
        QVERIFY(gitBranchList().contains("feature/test"));
    }

    void createDuplicateBranchFails() {
        mkRepo("r_dup");
        std::unique_ptr<MainWindow> w(openRepo());
        bool first = false, second = false;
        w->controller()->createBranchAsync(
            "dup", "main", false,
            [&first](GitController::OperationResult r) {
                QVERIFY(r.success);
                first = true;
            });
        QTRY_VERIFY_WITH_TIMEOUT(first, 15000);
        w->controller()->createBranchAsync(
            "dup", "main", false,
            [&second](GitController::OperationResult r) {
                QVERIFY(!r.success);  // duplicate must be rejected
                second = true;
            });
        QTRY_VERIFY_WITH_TIMEOUT(second, 15000);
        QCOMPARE(gitBranchList().count("dup\n"), 1);
    }

    void checkoutBranchSucceeds() {
        mkRepo("r_checkout");
        std::unique_ptr<MainWindow> w(openRepo());
        bool created = false;
        w->controller()->createBranchAsync(
            "feature/checkout", "main", false,
            [&created](GitController::OperationResult r) {
                QVERIFY(r.success);
                created = true;
            });
        QTRY_VERIFY_WITH_TIMEOUT(created, 15000);

        bool switched = false;
        w->controller()->checkoutBranchAsync(
            "feature/checkout",
            [&switched](GitController::OperationResult r) {
                QVERIFY(r.success);
                switched = true;
            });
        QTRY_VERIFY_WITH_TIMEOUT(switched, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(
            w->controller()->viewModel()->currentBranch() == "feature/checkout",
            15000);
    }

    void currentBranchCannotBeDeleted() {
        mkRepo("r_current");
        std::unique_ptr<MainWindow> w(openRepo());
        // Select "main" (the current branch) in the branch tree.
        QVERIFY(selectBranch(w.get(), "main"));
        QApplication::processEvents();
        QMetaObject::invokeMethod(w.get(), "requestDeleteSelectedBranch");
        QApplication::processEvents();

        auto *err = w->findChild<QTextEdit *>("errorDisplay");
        QVERIFY(err);
        QVERIFY(err->toPlainText().contains("当前分支不能删除"));
    }

    void deleteMergedBranchSucceeds() {
        mkRepo("r_merged");
        std::unique_ptr<MainWindow> w(openRepo());
        bool created = false;
        w->controller()->createBranchAsync(
            "feature/merged", "main", false,
            [&created](GitController::OperationResult r) {
                QVERIFY(r.success);
                created = true;
            });
        QTRY_VERIFY_WITH_TIMEOUT(created, 15000);

        bool deleted = false;
        w->controller()->deleteBranchAsync(
            "feature/merged", false,
            [&deleted](GitController::OperationResult r) {
                QVERIFY(r.success);  // at same commit -> -d succeeds
                deleted = true;
            });
        QTRY_VERIFY_WITH_TIMEOUT(deleted, 15000);
        QVERIFY(!gitBranchList().contains("feature/merged"));
    }

    void deleteUnmergedBranchRequiresForce() {
        mkRepo("r_unmerged");
        std::unique_ptr<MainWindow> w(openRepo());
        bool created = false;
        w->controller()->createBranchAsync(
            "feature/unmerged", "main", false,
            [&created](GitController::OperationResult r) {
                QVERIFY(r.success);
                created = true;
            });
        QTRY_VERIFY_WITH_TIMEOUT(created, 15000);
        bool switched = false;
        w->controller()->checkoutBranchAsync(
            "feature/unmerged",
            [&switched](GitController::OperationResult r) {
                QVERIFY(r.success);
                switched = true;
            });
        QTRY_VERIFY_WITH_TIMEOUT(switched, 15000);
        // A new commit on the branch makes it unmerged vs main.
        QVERIFY(writeFile("extra.txt", "unmerged\n"));
        w->controller()->refresh();
        QApplication::processEvents();
        bool committed = false;
        w->controller()->stageFilesAsync(
            {"extra.txt"},
            [&committed, &w](GitController::OperationResult r) {
                QVERIFY(r.success);
                auto *msg = w->findChild<QLineEdit *>("commitMessageEdit");
                if (msg) msg->setText("unmerged commit");
                QMetaObject::invokeMethod(w.get(), "onCommit");
                committed = true;
            });
        QTRY_VERIFY_WITH_TIMEOUT(committed, 15000);
        // Wait for the async commit to fully finish (snapshot clean).
        QTRY_VERIFY_WITH_TIMEOUT(
            w->controller()->viewModel()->snapshot().files.isEmpty(), 15000);
        QApplication::processEvents();

        // Switch back to main before deleting (can't delete checked-out).
        bool back = false;
        w->controller()->checkoutBranchAsync(
            "main", [&back](GitController::OperationResult r) {
                QVERIFY(r.success);
                back = true;
            });
        QTRY_VERIFY_WITH_TIMEOUT(back, 15000);

        // Safe delete must be rejected (unmerged).
        bool rejected = false;
        w->controller()->deleteBranchAsync(
            "feature/unmerged", false,
            [&rejected](GitController::OperationResult r) {
                QVERIFY(!r.success);
                QVERIFY(r.rejectedAsUnmerged);
                rejected = true;
            });
        QTRY_VERIFY_WITH_TIMEOUT(rejected, 15000);
        // Forced delete succeeds.
        bool forced = false;
        w->controller()->deleteBranchAsync(
            "feature/unmerged", true,
            [&forced](GitController::OperationResult r) {
                QVERIFY(r.success);
                forced = true;
            });
        QTRY_VERIFY_WITH_TIMEOUT(forced, 15000);
        QVERIFY(!gitBranchList().contains("feature/unmerged"));
    }
};

QTEST_MAIN(TestBranchManagement)
#include "test_branch_management.moc"
