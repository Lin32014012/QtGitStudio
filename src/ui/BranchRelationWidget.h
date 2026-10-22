#pragma once
#include <QWidget>
#include <QSet>
#include <QSize>
#include "GitCommitGraphService.h"

class QGraphicsScene;
class QGraphicsView;
class QLabel;
class QCheckBox;
class QPushButton;
class QLineEdit;
class QEvent;

// v4-P6-17: commit-based branch relation graph - a GitKraken-style
// network view: commit nodes (dot + short hash + message) on lanes,
// branch labels at branch tips, merge lines, current-branch highlight.
// Reuses GitCommitGraphService (view layer only, no data-logic change).
class BranchRelationWidget : public QWidget {
    Q_OBJECT
public:
    explicit BranchRelationWidget(QWidget *parent = nullptr);
    enum GraphicsItemRole {
        ItemKindRole = Qt::UserRole + 401,
        NodeSemanticRole,
        ParentCountRole,
    };
    enum GraphicsItemKind {
        CommitHashItem = 1,
        BranchBadgeItem = 2,
        CommitNodeItem = 3,
        CommitMessageItem = 4,
        GraphEdgeItem = 5,
        TagBadgeItem = 6,
        CommitSecondaryItem = 7,
    };
    enum NodeSemantic {
        NormalCommitNode = 1,
        HeadCommitNode = 2,
        MergeCommitNode = 3,
        HeadMergeCommitNode = 4,
    };
    enum LayoutMode {
        LinearHistory,
        MultiBranchGraph,
    };
    void setRepositoryPath(const QString &path);
    void refresh();

    // test hooks
    int branchNodeCount() const;
    QStringList branchNames() const;
    bool hasCurrentBranchNode() const;
    QCheckBox *showRemoteCheck() const { return m_showRemoteCheck; }
    QCheckBox *unmergedOnlyCheck() const { return m_unmergedOnlyCheck; }
    QLineEdit *searchEdit() const { return m_searchEdit; }
    QPushButton *recentButton() const { return m_recentButton; }
    QPushButton *currentBranchOnlyButton() const {
        return m_currentBranchOnlyButton;
    }
    QGraphicsView *view() const { return m_view; }
    QGraphicsScene *scene() const { return m_scene; }
    // Called by the graph node items (context menu / double click).
    void showContextMenu(const QString &branch, const QPoint &globalPos,
                         bool isRemote = false);
    // Called by the branch tag items (select / double click).
    void selectBranchTag(const QString &branch, bool isRemote = false);
    // Called by commit node items (double click / context menu).
    QString branchForCommit(const QString &hash) const;
    bool isRemoteBranch(const QString &branch) const;
    bool branchLabelGeometryValid() const;
    LayoutMode layoutModeForTesting() const { return m_layoutMode; }
    bool usesLinearHistoryLayout() const {
        return m_layoutMode == LinearHistory;
    }
    int visibleLaneCountForTesting() const { return m_visibleLaneCount; }
    int emptyVisualMetadataCountForTesting() const;

signals:
    // Emitted after the visible topology and scene items have been rebuilt.
    // The standalone Viewer uses it to reconcile its presentation transform
    // with the newly selected layout mode without changing Git data/state.
    void graphLayoutRebuilt();
    void switchBranchRequested(const QString &branch);
    void deleteBranchRequested(const QString &branch);
    void mergeBranchRequested(const QString &branch);
    void rebaseBranchRequested(const QString &branch);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct BranchNode;  // defined below
    void loadBranches();
    void loadCommits();
    void buildScene(bool preserveSelection = false);
    // Hash of the commit carrying the HEAD marker (not necessarily the
    // first entry: commits sharing a timestamp can be listed in any order).
    QString headCommitHash() const;
    void scheduleViewportRelayout();
    qreal logicalViewportWidth() const;

    struct BranchNode {
        QString name;
        QString hash;
        bool isHead = false;
        bool isRemote = false;
        bool merged = false;
        int ahead = 0;
        int behind = 0;
        QString upstream;
    };

    QString m_repositoryPath;
    QGraphicsScene *m_scene;
    QGraphicsView *m_view;
    QLabel *m_detailLabel;
    QCheckBox *m_showRemoteCheck;
    QCheckBox *m_unmergedOnlyCheck;
    // v4-P6-17 toolbar.
    QPushButton *m_showAllButton;
    QPushButton *m_recentButton;
    QPushButton *m_allBranchesButton;
    QPushButton *m_currentBranchOnlyButton;
    QLineEdit *m_searchEdit;
    QList<BranchNode> m_nodes;
    QList<CommitNode> m_commits;
    QString m_currentBranch;
    QString m_selected;
    QString m_selectedBranch;
    bool m_selectedBranchRemote = false;
    LayoutMode m_layoutMode = MultiBranchGraph;
    int m_visibleLaneCount = 0;
    QSize m_lastLaidOutViewportSize;
    bool m_viewportRelayoutPending = false;
};
