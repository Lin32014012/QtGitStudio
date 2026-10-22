// v4-P6-15 commit history (list-style) tests:
//   list load / default HEAD selection / search / detail / files /
//   diff on file click / empty state.
#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QUuid>
#include <QTreeWidget>
#include <QHeaderView>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include "CommitHistoryWidget.h"
#include "GitTestRepository.h"
#include "test_settings_isolation.h"

static bool runGit(const QString &repo, const QStringList &args) {
    QProcess p;
    p.setWorkingDirectory(repo);
    p.start("git", args);
    return p.waitForFinished(15000) && p.exitCode() == 0;
}

static void commitAll(const QString &repo, const QString &msg) {
    runGit(repo, {"add", "-A"});
    runGit(repo, {"-c", "user.name=Tester", "-c", "user.email=t@t",
                  "commit", "-q", "-m", msg});
}

class TestCommitHistory : public QObject {
    Q_OBJECT
private:
    GitTestRepository m_repo;

private slots:
    void initTestCase() {
        isolateTestSettings();
        QVERIFY(m_repo.isValid());
        QFile f(m_repo.path() + "/a.txt");
        f.open(QIODevice::WriteOnly);
        f.write("1\n");
        f.close();
        commitAll(m_repo.path(), "first commit");
        QFile f2(m_repo.path() + "/b.txt");
        f2.open(QIODevice::WriteOnly);
        f2.write("2\n");
        f2.close();
        commitAll(m_repo.path(), "second feature");
    }

    void messageColumnStretches() {
        // v4-P6-20: the message column must stretch to fill the
        // remaining width (never crushed by all-fixed columns).
        CommitHistoryWidget widget;
        widget.resize(900, 700);
        widget.setRepositoryPath(m_repo.path());
        widget.refresh();
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        QApplication::processEvents();
        QTreeWidget *tree = widget.findChild<QTreeWidget *>("historyCommitTree");
        QVERIFY(tree != nullptr);
        QHeaderView *header = tree->header();
        QCOMPARE(header->sectionResizeMode(2), QHeaderView::Stretch);
        QCOMPARE(header->sectionResizeMode(0), QHeaderView::Fixed);
        QVERIFY(tree->columnWidth(2) >= 300);
    }

    void listLoadsAndDefaultsToHead() {
        CommitHistoryWidget widget;
        widget.resize(900, 700);
        widget.setRepositoryPath(m_repo.path());
        widget.refresh();
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        QApplication::processEvents();

        // Three commits: GitTestRepository creates an initial commit,
        // plus the two made here. Newest first (HEAD = second feature).
        QCOMPARE(widget.rowCount(), 3);
        QTreeWidget *tree = widget.commitTree();
        QVERIFY(tree != nullptr);
        const QString headHash =
            tree->topLevelItem(0)->data(0, Qt::UserRole).toString();
        QCOMPARE(widget.selectedCommitHash(), headHash);
        // Detail is filled for the HEAD commit.
        QLabel *detail = widget.findChild<QLabel *>("historyDetail");
        QVERIFY(detail != nullptr);
        QVERIFY(detail->text().contains(QStringLiteral("second feature")));
        QVERIFY(detail->text().contains(QStringLiteral("作者")));
    }

    void searchFiltersByMessageAndHash() {
        CommitHistoryWidget widget;
        widget.setRepositoryPath(m_repo.path());
        widget.refresh();
        QLineEdit *search = widget.searchEdit();
        QVERIFY(search != nullptr);

        search->setText(QStringLiteral("second"));
        QApplication::processEvents();
        QCOMPARE(widget.rowCount(), 1);

        search->setText(QStringLiteral("first"));
        QApplication::processEvents();
        QCOMPARE(widget.rowCount(), 1);

        search->clear();
        QApplication::processEvents();
        QCOMPARE(widget.rowCount(), 3);
    }

    void filesAndDiffOnClick() {
        CommitHistoryWidget widget;
        widget.resize(900, 700);
        widget.setRepositoryPath(m_repo.path());
        widget.refresh();
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        QApplication::processEvents();

        QListWidget *files = widget.findChild<QListWidget *>("historyFileList");
        QVERIFY(files != nullptr);
        // HEAD commit changed b.txt (added).
        QVERIFY(files->count() >= 1);
        bool sawB = false;
        for (int i = 0; i < files->count(); ++i) {
            if (files->item(i)->text().contains("b.txt")) {
                sawB = true;
            }
        }
        QVERIFY(sawB);

        // Click a file -> diff preview fills and enlarge enables.
        for (int i = 0; i < files->count(); ++i) {
            if (files->item(i)->text().contains("b.txt")) {
                files->setCurrentRow(i);
                QApplication::processEvents();
                break;
            }
        }
        QPushButton *enlarge =
            widget.findChild<QPushButton *>("historyEnlargeDiff");
        QVERIFY(enlarge != nullptr);
        QVERIFY(enlarge->isEnabled());
        QVERIFY(!widget.currentDiffText().isEmpty());
    }

    void emptyRepositoryShowsEmptyState() {
        // A TRULY empty repository: git init, zero commits.
        const QString repo =
            QDir::tempPath() + "/qgs_p615_emptyhist_" + QUuid::createUuid().toString().left(8);
        QDir().mkpath(repo);
        QVERIFY(runGit(repo, {"init", "-q", "-b", "main"}));
        CommitHistoryWidget widget;
        widget.setRepositoryPath(repo);
        widget.refresh();
        QLabel *detail = widget.findChild<QLabel *>("historyDetail");
        QVERIFY(detail != nullptr);
        QVERIFY(detail->text().contains(QStringLiteral("暂无提交历史")));
        QCOMPARE(widget.rowCount(), 0);
        QDir(repo).removeRecursively();
    }
};

QTEST_MAIN(TestCommitHistory)
#include "test_commit_history.moc"
