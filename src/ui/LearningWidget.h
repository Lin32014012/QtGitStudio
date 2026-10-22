// v4-P6-5 Learning Mode MVP: offline, desktop-style Git learning
// widget. A topic tree (3 groups, 10 topics) on the left; selecting a
// topic shows its teaching content on the right. No network courses,
// no account system - all content is embedded static text.
#ifndef LEARNINGWIDGET_H
#define LEARNINGWIDGET_H

#include <QWidget>
#include <QString>
#include <QList>

class QTreeWidget;
class QTextBrowser;

struct LearningTopic {
    QString id;        // stable id, e.g. "git-what-is"
    QString title;     // e.g. "Git是什么"
    QString group;     // "Git基础" / "日常工作流" / "高级操作"
    QString contentHtml;
};

class LearningWidget : public QWidget {
    Q_OBJECT
public:
    explicit LearningWidget(QWidget *parent = nullptr);

    // All topics (static embedded content - no network).
    static const QList<LearningTopic> &topics();

    // Programmatically select a topic by id (used by tests).
    bool selectTopic(const QString &id);

    // Current topic title shown in the content area (empty if none).
    QString currentTopicTitle() const;

private slots:
    void onTopicActivated(const QString &id);

private:
    void buildTopicTree();
    void showTopic(const LearningTopic &topic);

    QTreeWidget *m_topicTree;
    QTextBrowser *m_content;
    QList<LearningTopic> m_topics;
    QString m_currentId;
};

#endif // LEARNINGWIDGET_H
