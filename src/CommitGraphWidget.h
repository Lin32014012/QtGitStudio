// v4-P3-1/2/3 Commit Graph widget: draws commit nodes, parent links,
// branch labels and the HEAD marker on a QGraphicsScene. v4-P3-3 adds:
// typed scene items (CommitNodeItem / BranchLabelItem / EdgeItem),
// merge-commit styling, local/remote branch label colors, message/hash/
// author search filtering and a right-click menu (copy hash, view diff).
#ifndef COMMITGRAPHWIDGET_H
#define COMMITGRAPHWIDGET_H

#include <QGraphicsView>
#include <QGraphicsEllipseItem>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsPathItem>
#include "GitCommitGraphService.h"

class QGraphicsScene;
class QLabel;
class QLineEdit;
class QPushButton;
class DiffViewerWidget;
class QMenu;
class QContextMenuEvent;

// --- typed scene items (v4-P3-3) -------------------------------------

// Commit node: plain ellipse; merge commits get a double ring; HEAD gets
// a red fill.
class CommitNodeItem : public QGraphicsEllipseItem {
public:
    enum { Type = QGraphicsItem::UserType + 101 };
    CommitNodeItem(const CommitNode &node, qreal x, qreal y, qreal r,
                   class CommitGraphWidget *host);
    const QString &hash() const { return m_hash; }
    const CommitNode &node() const { return m_node; }
    // Explicit type() so scene items can be classified without
    // dynamic_cast (which is unreliable for Qt classes across DLL
    // boundaries on MinGW).
    int type() const override { return Type; }
    // v4-P6-12: visible selection ring.
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

protected:
    void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override;

private:
    void showContextMenu(const QPointF &scenePos);
    CommitNode m_node;
    QString m_hash;
    class CommitGraphWidget *m_host;
};

// Branch / HEAD label next to a commit node.
class BranchLabelItem : public QGraphicsSimpleTextItem {
public:
    BranchLabelItem(const QString &text, const QColor &color,
                    const QString &commitHash);
    QString commitHash() const { return m_commitHash; }

private:
    QString m_commitHash;
};

// Edge (parent connection) with a commit hash tag for filtering.
class EdgeItem : public QGraphicsPathItem {
public:
    EdgeItem(const QPainterPath &path, const QPen &pen, const QString &commitHash);
    QString commitHash() const { return m_commitHash; }

private:
    QString m_commitHash;
};

// --- the widget -------------------------------------------------------

class CommitGraphWidget : public QWidget {
    Q_OBJECT
public:
    explicit CommitGraphWidget(QWidget *parent = nullptr);

    void setCommits(const QList<CommitNode> &commits);
    void setRepositoryPath(const QString &path);
    QString repositoryPath() const { return m_repositoryPath; }
    // v4-P6-9/10/11: viewport helpers - fitGraphToView is a width fit
    // (shrink only), fitWindow fits the whole graph, resetView restores
    // 1.0 scale; toggleShowAllCommits switches recent-60 / all.
    void fitGraphToView();
    void fitWindow();
    void resetView();
    void toggleShowAllCommits();
    bool showingAllCommits() const { return m_showAll; }
    QPushButton *showAllButton() const { return m_showAllButton; }

    int nodeCount() const;          // visible nodes (after filtering)
    QString selectedFilesText() const;
    // v4-P3-3: expose items of a commit (tests).
    QList<QGraphicsItem *> itemsOfCommit(const QString &hash) const;
    QLineEdit *searchEdit() const { return m_searchEdit; }
    QGraphicsScene *scene() const { return m_scene; }

signals:
    void copyHashRequested(const QString &hash);
    // v4-P4-1: user asked to cherry-pick this commit onto the current
    // branch (from the node context menu).
    void cherryPickRequested(const QString &hash);

public slots:
    // v4-P4-1: emitted by CommitNodeItem context menu through the host.
    void requestCherryPick(const QString &hash);

private slots:
    void onSelectionChanged();
    void onSearchTextChanged(const QString &text);

protected:
    // v4-P6-9: refit once the view has its real size.
    void showEvent(QShowEvent *event) override;

private:
    void buildLayout(const QList<CommitNode> &commits);
    void updateDetail(const CommitNode &node);
    void loadCommitDiff(const QString &hash);
    void copyHashToClipboard(const QString &hash);
    // v4-P6-11: default view shows only the most recent commits so a
    // huge history does not render as one unreadable vertical strip.
    static constexpr int kDefaultCommitLimit = 60;

    QGraphicsScene *m_scene;
    // v4-P6-10: the actual graph view (inside a QSplitter with the
    // detail panel); the widget itself is a container.
    QGraphicsView *m_view;
    QLabel *m_detailLabel;
    QLabel *m_filesLabel;
    QLineEdit *m_searchEdit;
    DiffViewerWidget *m_diffViewer;
    // v4-P6-11: toolbar + viewport modes.
    QPushButton *m_fitWindowButton;
    QPushButton *m_resetViewButton;
    QPushButton *m_showAllButton;
    QList<CommitNode> m_allCommits;
    bool m_showAll = false;
    QHash<QString, CommitNode> m_nodesByHash;
    QHash<QString, QList<QGraphicsItem *>> m_itemsByHash;
    QString m_repositoryPath;
    CommitNode m_currentNode;
};

#endif // COMMITGRAPHWIDGET_H
