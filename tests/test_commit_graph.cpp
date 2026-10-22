// v4-P3-1 Commit Graph MVP tests:
//   1. linear commit chain (parse + refs + HEAD)
//   2. branch commits (multiple refs on nodes)
//   3. merge commit (two parents)
//   4. empty repository (load succeeds with empty list, no crash)
#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QDir>
#include "GitCommitGraphService.h"
#include "CommitGraphWidget.h"

class TestCommitGraph : public QObject {
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

    bool commit(const QString &dir, const QString &file, const QByteArray &content,
                const QString &message) {
        QFile f(dir + "/" + file);
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write(content);
        f.close();
        return runGit(dir, {"add", "-A"})
            && runGit(dir, {"commit", "-q", "-m", message});
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
    }

    void linearChain() {
        const QString repo = mkRepo("graph_linear");
        QVERIFY(commit(repo, "a.txt", "1\n", "first"));
        QVERIFY(commit(repo, "b.txt", "2\n", "second"));
        QVERIFY(commit(repo, "c.txt", "3\n", "third"));

        const GitCommitGraphService::LoadResult result =
            GitCommitGraphService().load(repo);
        QVERIFY(result.success);
        QCOMPARE(result.commits.size(), 3);
        // Newest first.
        QCOMPARE(result.commits.at(0).message, QString("third"));
        QCOMPARE(result.commits.at(2).message, QString("first"));
        // Parent chain: third -> second -> first.
        QCOMPARE(result.commits.at(0).parents.size(), 1);
        QCOMPARE(result.commits.at(0).parents.first(),
                 result.commits.at(1).hash);
        QCOMPARE(result.commits.at(2).parents.size(), 0);  // root
        // HEAD marker on the newest commit.
        QVERIFY(result.commits.at(0).isHead);
        QVERIFY(result.commits.at(0).refs.contains("main"));
        // Author + time parsed.
        QCOMPARE(result.commits.at(0).author, QString("Test"));
        QVERIFY(result.commits.at(0).time.isValid());
        // shortHash is the 7-char prefix.
        QCOMPARE(result.commits.at(0).shortHash,
                 result.commits.at(0).hash.left(7));
    }

    void branchCommits() {
        const QString repo = mkRepo("graph_branch");
        QVERIFY(commit(repo, "a.txt", "1\n", "base"));
        QVERIFY(runGit(repo, {"checkout", "-q", "-b", "feature/x"}));
        QVERIFY(commit(repo, "f.txt", "f\n", "feature work"));
        QVERIFY(runGit(repo, {"update-ref",
                              "refs/remotes/company/feature/x", "HEAD"}));
        QVERIFY(runGit(repo, {"checkout", "-q", "main"}));
        QVERIFY(commit(repo, "m.txt", "m\n", "main work"));

        const GitCommitGraphService::LoadResult result =
            GitCommitGraphService().load(repo);
        QVERIFY(result.success);
        QVERIFY(result.commits.size() >= 3);
        // Both branches appear as refs.
        QStringList allRefs;
        for (const CommitNode &node : result.commits) {
            allRefs << node.refs;
        }
        QVERIFY(allRefs.contains("main"));
        QVERIFY(allRefs.contains("feature/x"));
        // A slash is valid in a local branch name. The production loader
        // must preserve full ref identity instead of guessing from the name.
        bool featureIsLocal = false;
        bool featureIsRemote = false;
        bool companyFeatureIsRemote = false;
        for (const CommitNode &node : result.commits) {
            featureIsLocal = featureIsLocal
                             || node.localBranches.contains("feature/x");
            featureIsRemote = featureIsRemote
                              || node.remoteBranches.contains("feature/x");
            companyFeatureIsRemote =
                companyFeatureIsRemote
                || node.remoteBranches.contains("company/feature/x");
        }
        QVERIFY(featureIsLocal);
        QVERIFY(!featureIsRemote);
        QVERIFY(companyFeatureIsRemote);
        // Exactly one HEAD marker (main work).
        int headCount = 0;
        for (const CommitNode &node : result.commits) {
            if (node.isHead) ++headCount;
        }
        QCOMPARE(headCount, 1);

        // Widget renders a node per commit.
        CommitGraphWidget widget;
        widget.setCommits(result.commits);
        QCOMPARE(widget.nodeCount(), result.commits.size());
    }

    void mergeCommit() {
        const QString repo = mkRepo("graph_merge");
        QVERIFY(commit(repo, "a.txt", "1\n", "base"));
        QVERIFY(runGit(repo, {"checkout", "-q", "-b", "topic"}));
        QVERIFY(commit(repo, "t.txt", "t\n", "topic work"));
        QVERIFY(runGit(repo, {"checkout", "-q", "main"}));
        QVERIFY(commit(repo, "m.txt", "m\n", "main work"));
        QVERIFY(runGit(repo, {"merge", "-q", "--no-ff", "-m", "merge topic", "topic"}));

        const GitCommitGraphService::LoadResult result =
            GitCommitGraphService().load(repo);
        QVERIFY(result.success);
        // Newest commit is the merge commit with TWO parents.
        QCOMPARE(result.commits.at(0).message, QString("merge topic"));
        QCOMPARE(result.commits.at(0).parents.size(), 2);
        // Both parents exist in the graph.
        const QString p1 = result.commits.at(0).parents.at(0);
        const QString p2 = result.commits.at(0).parents.at(1);
        bool found1 = false, found2 = false;
        for (const CommitNode &node : result.commits) {
            if (node.hash == p1) found1 = true;
            if (node.hash == p2) found2 = true;
        }
        QVERIFY(found1);
        QVERIFY(found2);

        CommitGraphWidget widget;
        widget.setCommits(result.commits);
        QCOMPARE(widget.nodeCount(), result.commits.size());
    }

    void emptyRepository() {
        const QString repo = mkRepo("graph_empty");
        const GitCommitGraphService::LoadResult result =
            GitCommitGraphService().load(repo);
        QVERIFY(result.success);
        QVERIFY(result.commits.isEmpty());  // no crash, empty history

        CommitGraphWidget widget;
        widget.setCommits(result.commits);
        QCOMPARE(widget.nodeCount(), 0);
    }

    void parseRefsEdgeCases() {
        // Pure parser: tag + remote refs + no-HEAD field.
        QStringList local, remote, tags;
        bool isHead = false;
        GitCommitGraphService::parseRefs(
            QStringLiteral("(HEAD -> refs/heads/main, refs/remotes/origin/main, refs/tags/v1.0)"),
            &local, &remote, &tags, &isHead);
        QVERIFY(isHead);
        QVERIFY(local.contains("main"));
        QVERIFY(remote.contains("origin/main"));
        QVERIFY(tags.contains("v1.0"));
        QVERIFY(!local.contains("HEAD"));

        local.clear();
        remote.clear();
        tags.clear();
        isHead = false;
        GitCommitGraphService::parseRefs(QStringLiteral(""), &local, &remote,
                                         &tags, &isHead);
        QVERIFY(!isHead);
        QVERIFY(local.isEmpty());
        QVERIFY(remote.isEmpty());
    }
};

QTEST_MAIN(TestCommitGraph)
#include "test_commit_graph.moc"
