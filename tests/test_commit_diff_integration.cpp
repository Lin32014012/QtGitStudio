// v4-P3-2 Commit Graph Diff Integration tests:
//   1. regular commit diff (files / additions / deletions)
//   2. multi-file modification
//   3. first (root) commit
//   4. merge commit (combined diff, no crash)
// Plus a CommitGraphWidget integration check (node click -> changed
// files shown through the reused DiffViewerWidget).
#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QGraphicsScene>
#include <QGraphicsEllipseItem>
#include "GitCommitDiffService.h"
#include "GitController.h"
#include "CommitGraphWidget.h"
#include "GitCommitGraphService.h"

class TestCommitDiffIntegration : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;

    bool runGit(const QString &dir, const QStringList &args,
                QString *output = nullptr) {
        QProcess git;
        git.setWorkingDirectory(dir);
        git.start("git", args);
        if (!git.waitForFinished(15000)) return false;
        if (output) *output = QString::fromUtf8(git.readAllStandardOutput());
        return git.exitStatus() == QProcess::NormalExit && git.exitCode() == 0;
    }

    QString mkRepo(const QString &tag) {
        const QString path = m_dir.path() + "/" + tag;
        QDir().mkpath(path);
        runGit(path, {"init", "-q", "-b", "main"});
        runGit(path, {"config", "user.name", "Test"});
        runGit(path, {"config", "user.email", "t@t.local"});
        return path;
    }

    bool writeFile(const QString &dir, const QString &name, const QByteArray &data) {
        QFile f(dir + "/" + name);
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write(data);
        f.close();
        return true;
    }

    QString commit(const QString &dir, const QStringList &files, const QString &message) {
        Q_UNUSED(files);
        if (!runGit(dir, {"add", "-A"})) return QString();
        if (!runGit(dir, {"commit", "-q", "-m", message})) return QString();
        QString out;
        runGit(dir, {"rev-parse", "HEAD"}, &out);
        return out.trimmed();
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        // v4-P6-21R4: the user home directory is a git work tree on
        // this machine - without GIT_CEILING a non-repo temp fixture
        // would resolve into the parent work tree and git diff would
        // "succeed" instead of failing.
        qputenv("GIT_CEILING_DIRECTORIES", QDir::tempPath().toUtf8());
    }

    void regularCommitDiff() {
        const QString repo = mkRepo("cd_regular");
        writeFile(repo, "a.txt", "line1\nline2\nline3\n");
        writeFile(repo, "b.txt", "x\n");
        commit(repo, {}, "init");
        // Modify a.txt: +1 -1 (replace line2 with lineX).
        writeFile(repo, "a.txt", "line1\nlineX\nline3\n");
        const QString hash = commit(repo, {}, "modify a");

        const GitCommitDiffService::LoadResult result =
            GitCommitDiffService().load(repo, hash);
        QVERIFY2(result.success, qPrintable(result.error));
        QVERIFY(result.diff.files.contains("a.txt"));
        QVERIFY(!result.diff.files.contains("b.txt"));
        QVERIFY(result.diff.additions >= 1);
        QVERIFY(result.diff.deletions >= 1);
        QVERIFY(result.diff.diffText.contains("diff --git"));
    }

    void multiFileModification() {
        const QString repo = mkRepo("cd_multi");
        writeFile(repo, "a.txt", "1\n");
        writeFile(repo, "b.txt", "2\n");
        writeFile(repo, "c.txt", "3\n");
        commit(repo, {}, "init");
        writeFile(repo, "a.txt", "1\nchanged\n");
        writeFile(repo, "b.txt", "2\nchanged\n");
        writeFile(repo, "d.txt", "new file\n");
        const QString hash = commit(repo, {}, "multi change");

        const GitCommitDiffService::LoadResult result =
            GitCommitDiffService().load(repo, hash);
        QVERIFY(result.success);
        QVERIFY(result.diff.files.contains("a.txt"));
        QVERIFY(result.diff.files.contains("b.txt"));
        QVERIFY(result.diff.files.contains("d.txt"));
        QCOMPARE(result.diff.files.size(), 3);
        // No duplicates.
        QCOMPARE(result.diff.files.size(),
                 QSet<QString>(result.diff.files.begin(),
                               result.diff.files.end()).size());
    }

    void firstCommit() {
        const QString repo = mkRepo("cd_root");
        writeFile(repo, "r1.txt", "hello\n");
        writeFile(repo, "r2.txt", "world\n");
        const QString hash = commit(repo, {}, "first commit");

        const GitCommitDiffService::LoadResult result =
            GitCommitDiffService().load(repo, hash);
        QVERIFY(result.success);
        // Root commit shows both files.
        QVERIFY(result.diff.files.contains("r1.txt"));
        QVERIFY(result.diff.files.contains("r2.txt"));
        QVERIFY(result.diff.additions >= 2);
        QVERIFY(result.diff.deletions == 0);
    }

    void mergeCommit() {
        const QString repo = mkRepo("cd_merge");
        writeFile(repo, "base.txt", "base\n");
        commit(repo, {}, "base");
        QVERIFY(runGit(repo, {"checkout", "-q", "-b", "topic"}));
        writeFile(repo, "topic.txt", "topic\n");
        commit(repo, {}, "topic work");
        QVERIFY(runGit(repo, {"checkout", "-q", "main"}));
        writeFile(repo, "main.txt", "main\n");
        commit(repo, {}, "main work");
        QVERIFY2(runGit(repo, {"merge", "-q", "--no-ff", "-m", "merge", "topic"}),
                 "merge");
        QString mergeOut;
        runGit(repo, {"rev-parse", "HEAD"}, &mergeOut);
        const QString mergeHash = mergeOut.trimmed();

        const GitCommitDiffService::LoadResult result =
            GitCommitDiffService().load(repo, mergeHash);
        QVERIFY(result.success);  // combined diff, must not crash
        // Merge commit shows the merged-in files.
        QVERIFY(result.diff.files.contains("topic.txt")
                || result.diff.files.contains("main.txt"));
        QVERIFY(result.diff.additions >= 1);
    }

    void widgetIntegrationShowsChangedFiles() {
        const QString repo = mkRepo("cd_widget");
        writeFile(repo, "w.txt", "1\n");
        const QString first = commit(repo, {}, "first");
        writeFile(repo, "w.txt", "1\n2\n");
        const QString second = commit(repo, {}, "second");

        // Load the graph, pick the "second" node, expect its changed
        // files in the widget's files label (via the reused
        // DiffViewerWidget path).
        const GitCommitGraphService::LoadResult graph =
            GitCommitGraphService().load(repo);
        QVERIFY(graph.success);
        CommitGraphWidget widget;
        widget.setRepositoryPath(repo);
        widget.resize(800, 600);
        widget.show();
        widget.setCommits(graph.commits);

        // Select the newest (HEAD) node programmatically.
        QGraphicsScene *scene = widget.scene();
        QVERIFY(scene);
        const QList<QGraphicsItem *> items = scene->items();
        QGraphicsItem *headNode = nullptr;
        for (QGraphicsItem *item : items) {
            if (item->type() == CommitNodeItem::Type
                && item->data(0).toString() == second) {
                headNode = item;
                break;
            }
        }
        QVERIFY(headNode);
        scene->clearSelection();
        headNode->setSelected(true);
        QApplication::processEvents();

        const QString filesText = widget.selectedFilesText();
        QVERIFY(filesText.contains("变更文件"));
        QVERIFY(filesText.contains("w.txt"));
        Q_UNUSED(first);
    }

    void controllerDiffReportsGitFailure() {
        const QString nonRepository = m_dir.path() + "/not_a_repository";
        QVERIFY(QDir().mkpath(nonRepository));

        GitController controller;
        controller.setRepositoryPath(nonRepository);
        bool done = false;
        GitController::FileDiffResult observed;
        controller.fileDiffAsync(
            QStringLiteral("missing.txt"),
            [&done, &observed](GitController::FileDiffResult result) {
                observed = result;
                done = true;
            });

        QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
        QVERIFY(!observed.success);
        QVERIFY(!observed.error.isEmpty());
    }
};

QTEST_MAIN(TestCommitDiffIntegration)
#include "test_commit_diff_integration.moc"
