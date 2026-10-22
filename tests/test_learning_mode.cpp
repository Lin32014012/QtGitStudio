// v4-P6-5 Learning Mode MVP tests:
//   1. topic table integrity (3 groups, 10 topics, unique ids)
//   2. topic content non-empty and teaching-relevant
//   3. selectTopic shows content (currentTopicTitle reflects selection)
//   4. unknown id is rejected
//   5. entry restored in MainWindow (learnButton exists + connected)
#include <QtTest>
#include "test_settings_isolation.h"
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTextBrowser>
#include <QDialog>
#include <QPushButton>
#include "LearningWidget.h"
#include "MainWindow.h"

class TestLearningMode : public QObject {
    Q_OBJECT
private:
    int groupCount(QTreeWidget *tree) const {
        int count = 0;
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            if (tree->topLevelItem(i)->childCount() > 0) ++count;
        }
        return count;
    }

private slots:
    void initTestCase() { isolateTestSettings(); }
    void contentMatchesCurrentBranchGraphUI() {
        // v4-P6-21 ISSUE-15: the learning content must describe the
        // CURRENT product (recent-30 default, 查看关系图 entry).
        const QList<LearningTopic> &topics = LearningWidget::topics();
        QString allText;
        for (const auto &t : topics) {
            allText += t.title + QLatin1Char('\n') + t.contentHtml
                       + QLatin1Char('\n');
        }
        QVERIFY2(!allText.contains(QStringLiteral("最近 60")),
                 "stale '最近 60' still present in learning content");
        QVERIFY2(!allText.contains(QStringLiteral("适应窗口")),
                 "stale '适应窗口' still present in learning content");
        QVERIFY2(!allText.contains(QStringLiteral("按前缀分组")),
                 "stale '按前缀分组' still present in learning content");
        QVERIFY2(allText.contains(QStringLiteral("最近 30")),
                 "missing current '最近 30' in learning content");
        QVERIFY2(allText.contains(QStringLiteral("查看关系图")),
                 "missing current '查看关系图' in learning content");
        QVERIFY2(allText.contains(QStringLiteral("不会按名称前缀")),
                 "missing current no-prefix-grouping contract");
        QVERIFY2(allText.contains(QStringLiteral("搜索（说明/Hash/作者）")),
                 "missing documented search fields in learning content");
    }
    void topicTableIntegrity() {
        const QList<LearningTopic> &topics = LearningWidget::topics();
        // v4-P6-9: expanded to 10 groups / 75 topics covering every
        // aspect; v4-P6-18: +3 usage-tutorial groups (远程协作/高级…)
        // -> 13 groups / 83 topics.
        QVERIFY2(topics.size() >= 70,
                 qPrintable(QString("expected >=70 topics, got %1")
                                .arg(topics.size())));

        QStringList groups;
        QSet<QString> ids;
        for (const LearningTopic &topic : topics) {
            QVERIFY(!topic.id.isEmpty());
            QVERIFY(!topic.title.isEmpty());
            QVERIFY(!topic.group.isEmpty());
            QVERIFY(!topic.contentHtml.isEmpty());
            QVERIFY(!ids.contains(topic.id));
            ids.insert(topic.id);
            if (!groups.contains(topic.group)) groups.append(topic.group);
        }
        // The ten required groups.
        const QStringList required = {
            QStringLiteral("Git基础"),
            QStringLiteral("仓库操作"),
            QStringLiteral("文件与提交"),
            QStringLiteral("分支工作流"),
            QStringLiteral("高级历史操作"),
            QStringLiteral("临时工作与版本标记"),
            QStringLiteral("Remote 与同步"),
            QStringLiteral("Commit Graph"),
            QStringLiteral("GitHub"),
            QStringLiteral("冲突处理")};
        for (const QString &group : required) {
            QVERIFY2(groups.contains(group),
                     qPrintable(QStringLiteral("missing group: %1").arg(group)));
        }
        QCOMPARE(groups.size(), 13);

        // Core topics that must exist.
        const QStringList mustHave = {
            QStringLiteral("git-what-is"), QStringLiteral("repository"),
            QStringLiteral("worktree"), QStringLiteral("index"),
            QStringLiteral("head"), QStringLiteral("commit"),
            QStringLiteral("branch"), QStringLiteral("tag"),
            QStringLiteral("open-repo"), QStringLiteral("init-repo"),
            QStringLiteral("clone-repo"), QStringLiteral("env-detect"),
            QStringLiteral("file-changes"), QStringLiteral("diff"),
            QStringLiteral("stage"), QStringLiteral("unstage"),
            QStringLiteral("commit-op"), QStringLiteral("create-branch"),
            QStringLiteral("switch-branch"), QStringLiteral("delete-branch"),
            QStringLiteral("merge"), QStringLiteral("merge-conflict"),
            QStringLiteral("cherry-pick"), QStringLiteral("rebase"),
            QStringLiteral("rebase-continue"), QStringLiteral("rebase-abort"),
            QStringLiteral("bisect"), QStringLiteral("stash"),
            QStringLiteral("stash-apply"), QStringLiteral("stash-pop"),
            QStringLiteral("stash-drop"), QStringLiteral("remote"),
            QStringLiteral("origin"), QStringLiteral("fetch"),
            QStringLiteral("pull"), QStringLiteral("push"),
            QStringLiteral("read-graph"), QStringLiteral("merge-commit"),
            QStringLiteral("github-optional"), QStringLiteral("pat"),
            QStringLiteral("github-release"), QStringLiteral("release-notes"),
            QStringLiteral("release-assets"), QStringLiteral("what-conflict"),
            QStringLiteral("conflict-markers"), QStringLiteral("manual-resolve")};
        for (const QString &id : mustHave) {
            QVERIFY2(ids.contains(id),
                     qPrintable(QStringLiteral("missing topic: %1").arg(id)));
        }
    }

    void teachingContent() {
        const QList<LearningTopic> &topics = LearningWidget::topics();
        // Content must carry real teaching text, not placeholders.
        for (const LearningTopic &topic : topics) {
            QVERIFY2(topic.contentHtml.contains(QStringLiteral("用途"))
                         || topic.contentHtml.contains(QStringLiteral("Git")),
                     qPrintable(topic.id + " content is not teaching text"));
            // v4-P6-9: every topic must include the required teaching
            // parts (what / steps / notes / risks).
            QVERIFY2(topic.contentHtml.contains(QStringLiteral("基本步骤")),
                     qPrintable(topic.id + " missing 步骤"));
            QVERIFY2(topic.contentHtml.contains(QStringLiteral("注意事项")),
                     qPrintable(topic.id + " missing 注意"));
            QVERIFY2(topic.contentHtml.contains(QStringLiteral("常见风险")),
                     qPrintable(topic.id + " missing 风险"));
            QVERIFY(topic.contentHtml.contains(QStringLiteral("QtGitStudio"))
                    || topic.id == QStringLiteral("git-what-is"));
        }
    }

    void selectTopicShowsContent() {
        LearningWidget widget;
        QVERIFY(widget.selectTopic(QStringLiteral("stash")));
        QCOMPARE(widget.currentTopicTitle(), QString("Stash 暂存"));
        QTextBrowser *content = widget.findChild<QTextBrowser *>("learningContent");
        QVERIFY(content != nullptr);
        QVERIFY(!content->toPlainText().isEmpty());
        QVERIFY(content->toPlainText().contains(QStringLiteral("Stash")));
        QVERIFY(content->toPlainText().contains(QStringLiteral("QtGitStudio")));
        QVERIFY(content->toPlainText().contains(QStringLiteral("基本步骤")));

        // Switching topic updates the content area.
        QVERIFY(widget.selectTopic(QStringLiteral("rebase")));
        QCOMPARE(widget.currentTopicTitle(), QString("Rebase 变基"));
        QVERIFY(content->toPlainText().contains(QStringLiteral("Rebase")));
        QVERIFY(content->toPlainText().contains(QStringLiteral("Hash")));

        // Back and forth between groups works.
        QVERIFY(widget.selectTopic(QStringLiteral("git-what-is")));
        QCOMPARE(widget.currentTopicTitle(), QString("Git是什么"));
        QVERIFY(widget.selectTopic(QStringLiteral("github-release")));
        QCOMPARE(widget.currentTopicTitle(), QString("GitHub Release"));
    }

    void unknownTopicRejected() {
        LearningWidget widget;
        // v4-P6-10: the widget auto-selects the first topic on open, so
        // the title is non-empty by design; an unknown id is rejected
        // and the current topic stays.
        QVERIFY(!widget.currentTopicTitle().isEmpty());
        const QString before = widget.currentTopicTitle();
        QVERIFY(!widget.selectTopic(QStringLiteral("no-such-topic")));
        QCOMPARE(widget.currentTopicTitle(), before);
    }

    // v4-P6-10: opening the widget must not show a blank content page -
    // the first topic is selected automatically.
    void opensWithFirstTopicSelected() {
        LearningWidget widget;
        QTextBrowser *content =
            widget.findChild<QTextBrowser *>("learningContent");
        QVERIFY(content != nullptr);
        QVERIFY(!content->toPlainText().isEmpty());
        QCOMPARE(widget.currentTopicTitle(), QString("Git是什么"));
    }

    void topicTreeGroups() {
        LearningWidget widget;
        QTreeWidget *tree = widget.findChild<QTreeWidget *>("learningTopicTree");
        QVERIFY(tree != nullptr);
        // v4-P6-9: ten groups.
        QCOMPARE(groupCount(tree), 13);
        // Every topic is present as a leaf item.
        int leaves = 0;
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            leaves += tree->topLevelItem(i)->childCount();
        }
        QVERIFY2(leaves >= 70,
                 qPrintable(QString("expected >=70 leaves, got %1").arg(leaves)));
    }

    void mainWindowEntryRestored() {
        MainWindow window;
        QPushButton *learnButton = window.findChild<QPushButton *>("learnButton");
        QVERIFY(learnButton != nullptr);
        // v4-P6-11: the button carries a light 🎓 icon.
        QVERIFY(learnButton->text().contains(QStringLiteral("学习模式")));
        QVERIFY(!learnButton->toolTip().isEmpty());
        // Button is enabled inside the workspace header (not disabled).
        QVERIFY(learnButton->isEnabled());
    }

    // v4-P6-11: expanded topics - GitHub connect flow and graph view
    // tools must exist with full teaching structure.
    void expandedTopicsPresent() {
        const QList<LearningTopic> &topics = LearningWidget::topics();
        QSet<QString> ids;
        for (const LearningTopic &topic : topics) {
            ids.insert(topic.id);
        }
        QVERIFY(ids.contains(QStringLiteral("github-connect")));
        QVERIFY(ids.contains(QStringLiteral("graph-view-tools")));
        for (const LearningTopic &topic : topics) {
            if (topic.id == QStringLiteral("github-connect")
                || topic.id == QStringLiteral("graph-view-tools")) {
                QVERIFY(topic.contentHtml.contains(QStringLiteral("基本步骤")));
                QVERIFY(topic.contentHtml.contains(QStringLiteral("注意事项")));
                QVERIFY(topic.contentHtml.contains(QStringLiteral("常见风险")));
            }
        }
    }
};

QTEST_MAIN(TestLearningMode)
#include "test_learning_mode.moc"
