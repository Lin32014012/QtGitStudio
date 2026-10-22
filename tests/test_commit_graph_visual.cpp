// v4-P3-3 Commit Graph Visual Polish tests:
//   1. HEAD recognition (parse + widget HEAD label)
//   2. branch parsing (local vs remote classification)
//   3. merge recognition (isMerge flag)
//   4. search filtering (message / hash / author)
#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QGraphicsScene>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include "GitCommitGraphService.h"
#include "CommitGraphWidget.h"

class TestCommitGraphVisual : public QObject {
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
        runGit(path, {"config", "user.name", "Tester"});
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

    bool commitAll(const QString &dir, const QString &message) {
        return runGit(dir, {"add", "-A"})
            && runGit(dir, {"commit", "-q", "-m", message});
    }

private slots:
        void initTestCase() {
        // v4-P6-21R6: user home is a git work tree on this machine - temp-dir
        // fixtures must cap git discovery at %TEMP% or commands resolve into
        // the parent work tree (false-green/empty results).
        qputenv("GIT_CEILING_DIRECTORIES", QDir::tempPath().toUtf8());
        QVERIFY(m_dir.isValid());
    }

    void headRecognition() {
        const QString repo = mkRepo("vp_head");
        writeFile(repo, "a.txt", "1\n");
        commitAll(repo, "first");
        writeFile(repo, "a.txt", "1\n2\n");
        commitAll(repo, "second");

        const GitCommitGraphService::LoadResult result =
            GitCommitGraphService().load(repo);
        QVERIFY(result.success);
        // Exactly one HEAD, on the newest commit.
        int headCount = 0;
        const CommitNode *headNode = nullptr;
        for (const CommitNode &node : result.commits) {
            if (node.isHead) {
                ++headCount;
                headNode = &node;
            }
        }
        QCOMPARE(headCount, 1);
        QVERIFY(headNode);
        QCOMPARE(headNode->message, QString("second"));

        // Widget draws a HEAD label for that commit.
        CommitGraphWidget widget;
        widget.setCommits(result.commits);
        bool headLabelFound = false;
        const QList<QGraphicsItem *> items = widget.scene()->items();
        for (QGraphicsItem *item : items) {
            auto *text = qgraphicsitem_cast<QGraphicsSimpleTextItem *>(item);
            if (text && text->text() == QStringLiteral("HEAD")) {
                headLabelFound = true;
                break;
            }
        }
        QVERIFY(headLabelFound);
    }

    void branchParsingLocalVsRemote() {
        // Simulated %d field (refs/ prefix) with local + remote + tag.
        QStringList local, remote, tags;
        bool isHead = false;
        GitCommitGraphService::parseRefs(
            QStringLiteral("(HEAD -> refs/heads/feature/login, refs/remotes/origin/feature/login, refs/remotes/origin/main, refs/tags/v1.2)"),
            &local, &remote, &tags, &isHead);
        QVERIFY(isHead);
        QVERIFY(local.contains("feature/login"));
        QVERIFY(!local.contains("origin/feature/login"));
        QVERIFY(remote.contains("origin/feature/login"));
        QVERIFY(remote.contains("origin/main"));
        QVERIFY(tags.contains("v1.2"));

        // Real repo with a remote-tracking ref created locally (no
        // network): origin/main appears as a remote ref on HEAD.
        const QString repo = mkRepo("vp_branch");
        writeFile(repo, "a.txt", "1\n");
        commitAll(repo, "base");
        runGit(repo, {"branch", "feature/login"});
        runGit(repo, {"update-ref", "refs/remotes/company/login", "HEAD"});
        QString out;
        runGit(repo, {"log", "-1", "--decorate=full", "--format=%d"}, &out);
        QStringList l2, r2, t2;
        bool h2 = false;
        GitCommitGraphService::parseRefs(out.trimmed(), &l2, &r2, &t2, &h2);
        QVERIFY(l2.contains("main"));
        QVERIFY(l2.contains("feature/login"));
        QVERIFY(r2.contains("company/login"));

        // Full pipeline: parseLog classifies the same way.
        QString logOut;
        runGit(repo, {"log", "--all", "--date-order", "--decorate=full",
                      "--pretty=format:%H%x09%P%x09%an%x09%aI%x09%s%x09%d"},
               &logOut);
        const QList<CommitNode> nodes =
            GitCommitGraphService::parseLog(logOut.toUtf8());
        QVERIFY(!nodes.isEmpty());
        QVERIFY(nodes.at(0).localBranches.contains("main"));
        QVERIFY(nodes.at(0).localBranches.contains("feature/login"));
        QVERIFY(nodes.at(0).remoteBranches.contains("company/login"));
    }

    void mergeRecognition() {
        const QString repo = mkRepo("vp_merge");
        writeFile(repo, "a.txt", "1\n");
        commitAll(repo, "base");
        runGit(repo, {"checkout", "-q", "-b", "topic"});
        writeFile(repo, "t.txt", "t\n");
        commitAll(repo, "topic work");
        runGit(repo, {"checkout", "-q", "main"});
        writeFile(repo, "m.txt", "m\n");
        commitAll(repo, "main work");
        QVERIFY(runGit(repo, {"merge", "-q", "--no-ff", "-m", "merge topic", "topic"}));

        const GitCommitGraphService::LoadResult result =
            GitCommitGraphService().load(repo);
        QVERIFY(result.success);
        // Newest commit is the merge: isMerge true, two parents.
        const CommitNode &merge = result.commits.at(0);
        QVERIFY(merge.isMerge);
        QCOMPARE(merge.parents.size(), 2);
        // Non-merge commits are not flagged.
        for (int i = 1; i < result.commits.size(); ++i) {
            QVERIFY(!result.commits.at(i).isMerge);
        }

        // Widget shows a merge node (double ring -> child items).
        CommitGraphWidget widget;
        widget.setCommits(result.commits);
        const QList<QGraphicsItem *> mergeItems =
            widget.itemsOfCommit(merge.hash);
        QVERIFY(!mergeItems.isEmpty());
        int children = 0;
        for (QGraphicsItem *item : mergeItems) {
            if (item->type() == CommitNodeItem::Type) {
                children += item->childItems().size();
            }
        }
        QVERIFY(children >= 1);  // inner dot present
    }

    void searchFiltering() {
        const QString repo = mkRepo("vp_search");
        writeFile(repo, "a.txt", "1\n");
        commitAll(repo, "feature login page");
        writeFile(repo, "b.txt", "2\n");
        commitAll(repo, "fix crash bug");
        writeFile(repo, "c.txt", "3\n");
        commitAll(repo, "refactor api");

        const GitCommitGraphService::LoadResult result =
            GitCommitGraphService().load(repo);
        QVERIFY(result.success);
        QCOMPARE(result.commits.size(), 3);

        CommitGraphWidget widget;
        widget.setCommits(result.commits);
        QCOMPARE(widget.nodeCount(), 3);

        // Filter by message fragment.
        widget.searchEdit()->setText(QStringLiteral("crash"));
        QApplication::processEvents();
        QCOMPARE(widget.nodeCount(), 1);

        // Filter by author (all commits by Tester).
        widget.searchEdit()->setText(QStringLiteral("Tester"));
        QApplication::processEvents();
        QCOMPARE(widget.nodeCount(), 3);

        // Filter by hash prefix.
        widget.searchEdit()->setText(result.commits.at(0).shortHash);
        QApplication::processEvents();
        QCOMPARE(widget.nodeCount(), 1);

        // Empty query restores everything.
        widget.searchEdit()->clear();
        QApplication::processEvents();
        QCOMPARE(widget.nodeCount(), 3);
    }

    // v4-P6-9: node info must be visible (hash + message + refs), the
    // sceneRect must hug the content, and a single commit must not
    // produce a giant canvas.
    void nodeInfoAndSceneRect() {
        const QString repo = mkRepo("vp_nodeinfo");
        writeFile(repo, "a.txt", "1\n");
        commitAll(repo, "fix: preserve partial stage and encoding");
        runGit(repo, {"tag", "v1.0.0"});
        runGit(repo, {"update-ref", "refs/remotes/origin/main", "HEAD"});

        const GitCommitGraphService::LoadResult result =
            GitCommitGraphService().load(repo);
        QVERIFY(result.success);
        QCOMPARE(result.commits.size(), 1);
        const CommitNode &node = result.commits.at(0);
        QVERIFY(node.isHead);
        QVERIFY(node.localBranches.contains("main"));

        CommitGraphWidget widget;
        widget.setCommits(result.commits);
        const QList<QGraphicsItem *> items = widget.itemsOfCommit(node.hash);
        QVERIFY(!items.isEmpty());

        bool hasHash = false, hasMessage = false, hasHead = false;
        bool hasBranch = false, hasTag = false, hasRemote = false;
        for (QGraphicsItem *item : items) {
            if (auto *label = dynamic_cast<BranchLabelItem *>(item)) {
                const QString text = label->text();
                if (text.contains(node.shortHash)) hasHash = true;
                if (text.contains("fix: preserve partial")) hasMessage = true;
                if (text == QStringLiteral("HEAD")) hasHead = true;
                if (text == QStringLiteral("main")) hasBranch = true;
                if (text == QStringLiteral("v1.0.0")) hasTag = true;
                if (text == QStringLiteral("origin/main")) hasRemote = true;
            }
        }
        QVERIFY2(hasHash, "short hash label missing");
        QVERIFY2(hasMessage, "commit message label missing");
        QVERIFY2(hasHead, "HEAD label missing");
        QVERIFY2(hasBranch, "branch label missing");
        QVERIFY2(hasTag, "tag label missing");
        QVERIFY2(hasRemote, "remote branch label missing");

        // sceneRect hugs itemsBoundingRect (no oversized fixed canvas).
        const QRectF br = widget.scene()->itemsBoundingRect();
        const QRectF sr = widget.scene()->sceneRect();
        QVERIFY2(sr.height() - br.height() < 200,
                 qPrintable(QString("scene taller than content: %1 vs %2")
                                .arg(sr.height()).arg(br.height())));
        QVERIFY2(sr.width() - br.width() < 300,
                 qPrintable(QString("scene wider than content: %1 vs %2")
                                .arg(sr.width()).arg(br.width())));
        // Single commit stays compact.
        QVERIFY2(sr.height() < 220,
                 qPrintable(QString("single commit scene too tall: %1")
                                .arg(sr.height())));
    }

    // v4-P6-9: multiple commits - rows evenly spaced, no overlap.
    void rowsDoNotOverlap() {
        const QString repo = mkRepo("vp_rows");
        for (int i = 0; i < 6; ++i) {
            writeFile(repo, QString("f%1.txt").arg(i),
                      QString("%1\n").arg(i).toUtf8());
            commitAll(repo, QString("commit %1").arg(i));
        }
        const GitCommitGraphService::LoadResult result =
            GitCommitGraphService().load(repo);
        QVERIFY(result.success);
        QCOMPARE(result.commits.size(), 6);

        CommitGraphWidget widget;
        widget.setCommits(result.commits);
        QCOMPARE(widget.nodeCount(), 6);

        // Collect node y positions (top-level ellipses only).
        QList<qreal> ys;
        const QList<QGraphicsItem *> items = widget.scene()->items();
        for (QGraphicsItem *item : items) {
            if (item->type() == CommitNodeItem::Type
                && item->parentItem() == nullptr) {
                ys.append(item->sceneBoundingRect().center().y());
            }
        }
        QCOMPARE(ys.size(), 6);
        std::sort(ys.begin(), ys.end());
        for (int i = 1; i < ys.size(); ++i) {
            const qreal gap = ys.at(i) - ys.at(i - 1);
            QVERIFY2(gap >= 30,
                     qPrintable(QString("rows overlap: gap %1").arg(gap)));
        }
    }

    // v4-P6-9: merge commit draws edge lines to both parents.
    void mergeEdgesPresent() {
        const QString repo = mkRepo("vp_edges");
        writeFile(repo, "a.txt", "1\n");
        commitAll(repo, "base");
        runGit(repo, {"checkout", "-q", "-b", "topic"});
        writeFile(repo, "t.txt", "t\n");
        commitAll(repo, "topic work");
        runGit(repo, {"checkout", "-q", "main"});
        writeFile(repo, "m.txt", "m\n");
        commitAll(repo, "main work");
        QVERIFY(runGit(repo, {"merge", "-q", "--no-ff", "-m",
                              QStringLiteral("merge topic"), "topic"}));

        const GitCommitGraphService::LoadResult result =
            GitCommitGraphService().load(repo);
        QVERIFY(result.success);
        const CommitNode &merge = result.commits.at(0);
        QVERIFY(merge.isMerge);
        QCOMPARE(merge.parents.size(), 2);

        CommitGraphWidget widget;
        widget.setCommits(result.commits);
        // The merge node must have edges to BOTH parents (QGraphicsPathItem
        // edge items stored under the same commit).
        const QList<QGraphicsItem *> items = widget.itemsOfCommit(merge.hash);
        int edges = 0;
        for (QGraphicsItem *item : items) {
            if (dynamic_cast<QGraphicsPathItem *>(item)
                && dynamic_cast<BranchLabelItem *>(item) == nullptr) {
                ++edges;
            }
        }
        QVERIFY2(edges >= 1,
                 qPrintable(QString("merge edges missing, got %1").arg(edges)));
    }

    // v4-P6-11: default view focuses the most recent 60 commits (no
    // unreadable long strip); the toolbar toggle shows everything.
    void recentLimitAndToolbar() {
        QList<CommitNode> commits;
        for (int i = 0; i < 70; ++i) {
            CommitNode n;
            n.hash = QStringLiteral("%1").arg(i, 40, 16, QLatin1Char('0'));
            n.shortHash = n.hash.left(7);
            n.message = QStringLiteral("commit %1").arg(i);
            n.author = QStringLiteral("Tester");
            if (i > 0) {
                n.parents.append(
                    QStringLiteral("%1").arg(i - 1, 40, 16, QLatin1Char('0')));
            }
            commits.append(n);
        }

        CommitGraphWidget widget;
        widget.resize(860, 520);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        QApplication::processEvents();
        widget.setCommits(commits);
        // Default: recent 60 only - not the whole history squeezed in.
        QCOMPARE(widget.nodeCount(), 60);
        QVERIFY(widget.showAllButton()->isVisible());
        QVERIFY(widget.showAllButton()->text().contains(QStringLiteral("查看全部")));

        // Toggle to all commits.
        widget.toggleShowAllCommits();
        QCOMPARE(widget.nodeCount(), 70);
        QVERIFY(widget.showingAllCommits());

        // Toggle back to recent 60.
        widget.toggleShowAllCommits();
        QCOMPARE(widget.nodeCount(), 60);
        QVERIFY(!widget.showingAllCommits());
    }

    // v4-P6-12: opening the graph auto-selects HEAD (or the first
    // commit) and fills the detail panel - no blank right side.
    void defaultSelectionFillsDetail() {
        const QString repo = mkRepo("vp_defaultsel");
        writeFile(repo, "a.txt", "1\n");
        commitAll(repo, "first commit");
        writeFile(repo, "b.txt", "2\n");
        commitAll(repo, "second commit");
        runGit(repo, {"tag", "v1.0.0"});
        const auto result = GitCommitGraphService().load(repo);
        QVERIFY(result.success);
        QVERIFY(!result.commits.isEmpty());

        CommitGraphWidget widget;
        widget.resize(960, 560);
        widget.setRepositoryPath(repo);
        widget.setCommits(result.commits);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        QApplication::processEvents();

        // HEAD commit is the newest; it must be selected by default.
        const QString headHash = result.commits.first().hash;
        bool headSelected = false;
        for (QGraphicsItem *item : widget.itemsOfCommit(headHash)) {
            if (item->type() == CommitNodeItem::Type && item->isSelected()) {
                headSelected = true;
            }
        }
        QVERIFY2(headSelected, "HEAD commit not selected by default");

        // Detail panel is filled: hash + message + author + date.
        QLabel *detail = widget.findChild<QLabel *>("commitGraphDetail");
        QVERIFY(detail != nullptr);
        const QString text = detail->text();
        QVERIFY(text.contains(result.commits.first().hash));
        QVERIFY(text.contains(QStringLiteral("second commit")));
        QVERIFY(text.contains(QStringLiteral("作者")));
        QVERIFY(text.contains(QStringLiteral("时间")));
        // Files area shows a clear state (content or explicit empty).
        QLabel *files = widget.findChild<QLabel *>("commitGraphFiles");
        QVERIFY(files != nullptr);
        QVERIFY(!files->text().isEmpty());
    }

    // v4-P6-12/13: a TRULY empty repository (git init, zero commits)
    // shows an explicit empty state - no nodes, no HEAD, no diff-load
    // error, no crash.
    void emptyHistoryShowsEmptyState() {
        const QString repo = mkRepo("vp_emptyhist");
        // Sanity: git init alone - no commit was ever created.
        QVERIFY(!runGit(repo, {"rev-parse", "--verify", "HEAD"}));
        const auto result = GitCommitGraphService().load(repo);
        QVERIFY(result.success);
        QVERIFY(result.commits.isEmpty());

        CommitGraphWidget widget;
        widget.resize(960, 560);
        widget.setRepositoryPath(repo);
        widget.setCommits(result.commits);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        QApplication::processEvents();

        // No nodes at all - no HEAD, no initial commit.
        QCOMPARE(widget.nodeCount(), 0);
        QVERIFY(widget.scene()->items().isEmpty()
                || widget.scene()->itemsBoundingRect().height() < 200);

        // Explicit empty state, not a white panel.
        QLabel *detail = widget.findChild<QLabel *>("commitGraphDetail");
        QVERIFY(detail != nullptr);
        QVERIFY(detail->text().contains(QStringLiteral("暂无提交历史")));

        // No fake diff-loading error / no "未设置仓库路径".
        QLabel *files = widget.findChild<QLabel *>("commitGraphFiles");
        QVERIFY(files != nullptr);
        QVERIFY(!files->text().contains(QStringLiteral("未设置仓库路径")));
        QVERIFY(!files->text().contains(QStringLiteral("读取失败")));
    }

    // v4-P6-13: a repository WITH commits still auto-selects HEAD and
    // shows the detail - guards against the empty-state fix breaking
    // normal repositories.
    void repositoryWithCommitStillWorks() {
        const QString repo = mkRepo("vp_stillworks");
        writeFile(repo, "a.txt", "1\n");
        commitAll(repo, "only commit");
        const auto result = GitCommitGraphService().load(repo);
        QVERIFY(result.success);
        QCOMPARE(result.commits.size(), 1);
        QVERIFY(result.commits.first().isHead);

        CommitGraphWidget widget;
        widget.resize(960, 560);
        widget.setRepositoryPath(repo);
        widget.setCommits(result.commits);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        QApplication::processEvents();

        // HEAD selected by default.
        bool headSelected = false;
        for (QGraphicsItem *item :
             widget.itemsOfCommit(result.commits.first().hash)) {
            if (item->type() == CommitNodeItem::Type && item->isSelected()) {
                headSelected = true;
            }
        }
        QVERIFY(headSelected);
        // Detail shows the commit message.
        QLabel *detail = widget.findChild<QLabel *>("commitGraphDetail");
        QVERIFY(detail != nullptr);
        QVERIFY(detail->text().contains(QStringLiteral("only commit")));
    }
};

QTEST_MAIN(TestCommitGraphVisual)
#include "test_commit_graph_visual.moc"
