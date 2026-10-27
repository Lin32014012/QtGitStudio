#include "CommitGraphWidget.h"
#include "DiffViewerWidget.h"
#include "GitCommitDiffService.h"
#include "NotificationManager.h"

#include <QGraphicsScene>
#include <QGraphicsSceneContextMenuEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QMenu>
#include <QClipboard>
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QPen>
#include <QBrush>
#include <QHash>

namespace {
constexpr int kNodeRadius = 9;
// v4-P6-9: taller rows so hash + message + ref labels never overlap.
constexpr int kRowHeight = 58;
constexpr int kLaneWidth = 130;
constexpr int kLeftMargin = 30;
constexpr int kTopMargin = 24;

QString nodeDetailText(const CommitNode &node) {
    QStringList lines;
    lines << QStringLiteral("提交：%1").arg(node.hash);
    lines << (node.message.isEmpty() ? QStringLiteral("(无提交说明)")
                                     : node.message);
    lines << QStringLiteral("作者：%1").arg(node.author);
    lines << QStringLiteral("时间：%1")
                 .arg(node.time.isValid()
                          ? node.time.toString(Qt::ISODate)
                          : QStringLiteral("-"));
    QStringList parents = node.parents;
    lines << QStringLiteral("父提交：%1")
                 .arg(parents.isEmpty() ? QStringLiteral("(无)")
                                        : parents.join(QStringLiteral(", ")));
    QStringList refs;
    if (node.isHead) refs << QStringLiteral("HEAD");
    refs << node.localBranches << node.remoteBranches << node.tags;
    lines << QStringLiteral("引用：%1")
                 .arg(refs.isEmpty() ? QStringLiteral("(无)")
                                     : refs.join(QStringLiteral(", ")));
    return lines.join(QStringLiteral("\n"));
}

bool commitMatches(const CommitNode &node, const QString &query) {
    if (query.isEmpty()) return true;
    return node.message.contains(query, Qt::CaseInsensitive)
        || node.hash.contains(query, Qt::CaseInsensitive)
        || node.author.contains(query, Qt::CaseInsensitive);
}
}  // namespace

// --- CommitNodeItem ----------------------------------------------------

CommitNodeItem::CommitNodeItem(const CommitNode &node, qreal x, qreal y,
                               qreal r, CommitGraphWidget *host)
    : QGraphicsEllipseItem(x, y, r * 2, r * 2)
    , m_node(node)
    , m_hash(node.hash)
    , m_host(host) {
    setBrush(node.isHead ? QColor("#DC2626") : QColor("#2563EB"));
    setPen(QPen(QColor("#1F2937"), node.isMerge ? 2.2 : 1.2));
    setFlags(QGraphicsItem::ItemIsSelectable);
    setData(0, node.hash);
    setZValue(1);
    setToolTip(node.message);
    // Merge commits: white inner dot (double-ring look).
    if (node.isMerge) {
        auto *inner = new QGraphicsEllipseItem(x + r / 2, y + r / 2, r, r, this);
        inner->setBrush(QColor("#FFFFFF"));
        inner->setPen(QPen(QColor("#1F2937"), 1.0));
    }
}

// v4-P6-12: explicit selection highlight - a visible blue ring around
// the node so the current selection is obvious on the graph.
void CommitNodeItem::paint(QPainter *painter,
                           const QStyleOptionGraphicsItem *option,
                           QWidget *widget) {
    QGraphicsEllipseItem::paint(painter, option, widget);
    if (isSelected()) {
        painter->setPen(QPen(QColor("#3B82F6"), 2.5));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(rect().adjusted(-3.5, -3.5, 3.5, 3.5));
    }
}

void CommitNodeItem::contextMenuEvent(QGraphicsSceneContextMenuEvent *event) {
    event->accept();
    showContextMenu(event->scenePos());
}

void CommitNodeItem::showContextMenu(const QPointF &scenePos) {
    QMenu menu;
    QAction *copyHash = menu.addAction(QStringLiteral("复制 Hash"));
    QAction *viewDiff = menu.addAction(QStringLiteral("查看 Diff"));
    menu.addSeparator();
    // v4-P4-1: cherry-pick onto the current branch.
    QAction *cherryPick = menu.addAction(
        QStringLiteral("Cherry-pick 到当前分支"));
    QAction *chosen = nullptr;
    if (!scene()->views().isEmpty()) {
        QGraphicsView *view = scene()->views().first();
        chosen = menu.exec(view->mapToGlobal(view->mapFromScene(scenePos)));
    }
    if (chosen == copyHash) {
        QApplication::clipboard()->setText(m_hash);
    } else if (chosen == viewDiff) {
        setSelected(true);
    } else if (chosen == cherryPick) {
        if (m_host) {
            m_host->requestCherryPick(m_hash);
        }
    }
}

// --- BranchLabelItem ----------------------------------------------------

BranchLabelItem::BranchLabelItem(const QString &text, const QColor &color,
                                 const QString &commitHash)
    : QGraphicsSimpleTextItem(text)
    , m_commitHash(commitHash) {
    setBrush(color);
    QFont f = font();
    f.setPointSize(8);
    setFont(f);
    setZValue(1);
}

// --- EdgeItem -----------------------------------------------------------

EdgeItem::EdgeItem(const QPainterPath &path, const QPen &pen,
                   const QString &commitHash)
    : QGraphicsPathItem(path)
    , m_commitHash(commitHash) {
    setPen(pen);
    setZValue(0);
}

// --- CommitGraphWidget --------------------------------------------------

CommitGraphWidget::CommitGraphWidget(QWidget *parent)
    : QWidget(parent)
    , m_scene(new QGraphicsScene(this))
    , m_view(new QGraphicsView(this))
    , m_detailLabel(new QLabel(this))
    , m_filesLabel(new QLabel(this))
    , m_searchEdit(new QLineEdit(this))
    , m_diffViewer(new DiffViewerWidget(this)) {
    m_view->setScene(m_scene);
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setDragMode(QGraphicsView::NoDrag);
    m_view->setBackgroundBrush(QColor("#FAFAFA"));
    // v4-P6-10: the graph view must never collapse into a narrow strip.
    m_view->setMinimumHeight(300);

    m_searchEdit->setObjectName("commitGraphSearch");
    m_searchEdit->setPlaceholderText(
        QStringLiteral("搜索：提交说明 / Hash / 作者…"));
    m_searchEdit->setClearButtonEnabled(true);

    m_detailLabel->setObjectName("commitGraphDetail");
    m_detailLabel->setWordWrap(true);
    m_detailLabel->setProperty("role", QStringLiteral("monoBox"));
    m_filesLabel->setObjectName("commitGraphFiles");
    m_filesLabel->setWordWrap(true);
    m_filesLabel->setProperty("role", QStringLiteral("infoBox"));
    m_filesLabel->setMinimumHeight(36);

    // v4-P6-11: graph toolbar - 适应窗口 / 重置视图 / 查看全部.
    m_fitWindowButton = new QPushButton(QStringLiteral("适应窗口"), this);
    m_fitWindowButton->setObjectName("graphFitWindow");
    m_resetViewButton = new QPushButton(QStringLiteral("重置视图"), this);
    m_resetViewButton->setObjectName("graphResetView");
    m_showAllButton = new QPushButton(QStringLiteral("查看全部"), this);
    m_showAllButton->setObjectName("graphShowAll");
    for (QPushButton *b : {m_fitWindowButton, m_resetViewButton,
                           m_showAllButton}) {
        b->setProperty("role", QStringLiteral("ghost"));
    }
    auto *toolRow = new QHBoxLayout();
    toolRow->setSpacing(6);
    toolRow->addWidget(m_fitWindowButton);
    toolRow->addWidget(m_resetViewButton);
    toolRow->addWidget(m_showAllButton);
    toolRow->addStretch();

    auto *leftPanel = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(4);
    leftLayout->addLayout(toolRow);
    leftLayout->addWidget(m_view, 1);

    // v4-P6-10: QSplitter - graph gets 3 parts, detail/diff 2 parts.
    auto *detailPanel = new QWidget(this);
    auto *detailLayout = new QVBoxLayout(detailPanel);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(6);
    detailLayout->addWidget(m_searchEdit);
    detailLayout->addWidget(m_detailLabel);
    detailLayout->addWidget(m_filesLabel);
    // Cap the diff area so the graph keeps the majority of the window.
    m_diffViewer->setMaximumHeight(260);
    detailLayout->addWidget(m_diffViewer, 1);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName("commitGraphSplitter");
    splitter->addWidget(leftPanel);
    splitter->addWidget(detailPanel);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    // v4-P6-12: default split ~65/35 (graph / detail) so the detail
    // panel never dominates; user can still drag it.
    splitter->setSizes({640, 360});

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);

    connect(m_scene, &QGraphicsScene::selectionChanged,
            this, &CommitGraphWidget::onSelectionChanged);
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &CommitGraphWidget::onSearchTextChanged);
    connect(m_fitWindowButton, &QPushButton::clicked,
            this, &CommitGraphWidget::fitWindow);
    connect(m_resetViewButton, &QPushButton::clicked,
            this, &CommitGraphWidget::resetView);
    connect(m_showAllButton, &QPushButton::clicked,
            this, &CommitGraphWidget::toggleShowAllCommits);
}

int CommitGraphWidget::nodeCount() const {
    int count = 0;
    const QList<QGraphicsItem *> items = m_scene->items();
    for (const QGraphicsItem *item : items) {
        // Only real commit nodes count (merge inner dots are plain
        // ellipses with type() == 4 and must not be counted). type() is
        // used instead of dynamic_cast - the latter misclassifies Qt
        // objects across DLL boundaries on MinGW.
        if (item->type() == CommitNodeItem::Type && item->isVisible()) {
            ++count;
        }
    }
    return count;
}

QString CommitGraphWidget::selectedFilesText() const {
    return m_filesLabel->text();
}

QList<QGraphicsItem *> CommitGraphWidget::itemsOfCommit(
    const QString &hash) const {
    return m_itemsByHash.value(hash);
}

void CommitGraphWidget::setRepositoryPath(const QString &path) {
    m_repositoryPath = path;
}

void CommitGraphWidget::setCommits(const QList<CommitNode> &commits) {
    m_scene->clear();
    m_nodesByHash.clear();
    m_itemsByHash.clear();
    m_detailLabel->clear();
    m_filesLabel->clear();
    m_diffViewer->clearView();
    m_allCommits = commits;
    m_showAll = false;
    if (commits.isEmpty()) {
        // v4-P6-1: empty state with reason + next step.
        auto *empty = m_scene->addSimpleText(
            NotificationManager::emptyStateText(
                QStringLiteral("仓库还没有提交"),
                QStringLiteral("暂存文件后点击“提交”，创建首次提交。")));
        empty->setBrush(QColor("#6B7280"));
        empty->setPos(kLeftMargin, kTopMargin);
        // v4-P6-12: the right panel must not stay a blank white area -
        // show an explicit empty state there as well.
        m_detailLabel->setText(
            QStringLiteral("暂无提交历史\n\n完成第一次 Commit 后，"
                           "这里将显示提交详情和文件变化。"));
        m_filesLabel->setText(QStringLiteral("变更文件：-"));
        m_diffViewer->showMessage(QStringLiteral("暂无提交历史"));
        return;
    }
    // v4-P6-11: default view focuses on the most recent commits so a
    // huge history never renders as one unreadable vertical strip.
    QList<CommitNode> display = commits;
    if (commits.size() > kDefaultCommitLimit) {
        display = commits.mid(0, kDefaultCommitLimit);
    }
    m_showAllButton->setVisible(commits.size() > kDefaultCommitLimit);
    m_showAllButton->setText(
        QStringLiteral("查看全部 (%1)").arg(commits.size()));
    buildLayout(display);
    onSearchTextChanged(m_searchEdit->text());
    // v4-P6-12: auto-select the most reasonable commit so the right
    // panel is never an empty blank: HEAD -> first commit -> empty
    // state (never assume the list is non-empty).
    if (display.isEmpty()) {
        m_detailLabel->setText(
            QStringLiteral("暂无提交历史\n\n完成第一次 Commit 后，"
                           "这里将显示提交详情和文件变化。"));
        m_filesLabel->setText(QStringLiteral("变更文件：-"));
        m_diffViewer->showMessage(
            QStringLiteral("暂无提交历史"));
        return;
    }
    const CommitNode *headNode = nullptr;
    for (const CommitNode &c : display) {
        if (c.isHead) {
            headNode = &c;
            break;
        }
    }
    const CommitNode &target =
        headNode ? *headNode : display.first();
    const QList<QGraphicsItem *> items = m_itemsByHash.value(target.hash);
    for (QGraphicsItem *item : items) {
        if (item->type() == CommitNodeItem::Type) {
            item->setSelected(true);  // fires onSelectionChanged
            break;
        }
    }
}

void CommitGraphWidget::toggleShowAllCommits() {
    if (m_allCommits.isEmpty()) {
        return;
    }
    m_showAll = !m_showAll;
    const QList<CommitNode> display =
        m_showAll ? m_allCommits
                  : m_allCommits.mid(0, kDefaultCommitLimit);
    // v4-P6-11: rebuild from scratch - the scene must be cleared or the
    // previous layout would overlap the new one (nodes doubled).
    m_scene->clear();
    m_nodesByHash.clear();
    m_itemsByHash.clear();
    m_showAllButton->setText(m_showAll
                                 ? QStringLiteral("仅看近期 60 条")
                                 : QStringLiteral("查看全部 (%1)")
                                       .arg(m_allCommits.size()));
    buildLayout(display);
    onSearchTextChanged(m_searchEdit->text());
}

void CommitGraphWidget::buildLayout(const QList<CommitNode> &commits) {
    // lane assignment: process oldest -> newest, inherit the first
    // parent's lane; a lane already occupied by another chain gets a new
    // lane (branching / merge fan-out).
    QHash<QString, int> laneOf;
    QHash<int, QString> laneTop;

    QList<CommitNode> rev;
    for (int i = commits.size() - 1; i >= 0; --i) {
        rev.append(commits.at(i));
    }
    for (const CommitNode &node : rev) {
        int lane = 0;
        if (!node.parents.isEmpty()) {
            lane = laneOf.value(node.parents.first(), 0);
        }
        int guard = 0;
        while (laneTop.contains(lane) && laneTop.value(lane) != node.parents.value(0)
               && guard < 64) {
            ++lane;
            ++guard;
        }
        laneOf.insert(node.hash, lane);
        laneTop.insert(lane, node.hash);
    }

    const QHash<QString, int> indexOf = [&commits]() {
        QHash<QString, int> map;
        for (int i = 0; i < commits.size(); ++i) {
            map.insert(commits.at(i).hash, i);
        }
        return map;
    }();

    // Edges first (under the nodes).
    for (const CommitNode &node : commits) {
        const int lane = laneOf.value(node.hash, 0);
        const QPointF from(kLeftMargin + lane * kLaneWidth + kNodeRadius,
                           kTopMargin + indexOf.value(node.hash) * kRowHeight
                               + kNodeRadius);
        QPen pen(QColor("#9CA3AF"), 1.6);
        QPainterPath path;
        bool first = true;
        for (const QString &parentHash : node.parents) {
            if (!laneOf.contains(parentHash)) continue;
            const int parentLane = laneOf.value(parentHash, 0);
            const QPointF to(kLeftMargin + parentLane * kLaneWidth + kNodeRadius,
                             kTopMargin + indexOf.value(parentHash) * kRowHeight
                                 + kNodeRadius);
            if (first) {
                path.moveTo(from);
                first = false;
            }
            path.lineTo(from.x(), to.y());
            path.lineTo(to);
        }
        if (!first) {
            auto *edge = new EdgeItem(path, pen, node.hash);
            m_scene->addItem(edge);
            m_itemsByHash[node.hash].append(edge);
        }
    }

    for (const CommitNode &node : commits) {
        const int lane = laneOf.value(node.hash, 0);
        const qreal x = kLeftMargin + lane * kLaneWidth;
        const qreal y = kTopMargin + indexOf.value(node.hash) * kRowHeight;

        auto *ellipse = new CommitNodeItem(node, x, y, kNodeRadius, this);
        m_scene->addItem(ellipse);
        m_nodesByHash.insert(node.hash, node);
        m_itemsByHash[node.hash].append(ellipse);

        auto *hashText = new BranchLabelItem(node.shortHash, QColor("#6B7280"),
                                             node.hash);
        QFont hashFont = hashText->font();
        hashFont.setPointSize(10);
        hashFont.setBold(true);
        hashText->setFont(hashFont);
        hashText->setPos(x + kNodeRadius * 2 + 6, y + 4);
        m_scene->addItem(hashText);
        m_itemsByHash[node.hash].append(hashText);

        // v4-P6-9: commit message must be clearly readable - larger,
        // darker, on its own line under the hash (kept short so lanes
        // never overlap).
        QString message = node.message.trimmed();
        if (message.size() > 32) {
            message = message.left(29) + QStringLiteral("…");
        }
        auto *msgText = new BranchLabelItem(message, QColor("#1F2937"),
                                            node.hash);
        QFont msgFont = msgText->font();
        msgFont.setPointSize(10);
        msgFont.setBold(true);
        msgText->setFont(msgFont);
        msgText->setPos(x + kNodeRadius * 2 + 6, y + 22);
        m_scene->addItem(msgText);
        m_itemsByHash[node.hash].append(msgText);

        // v4-P3-3: HEAD / local (blue) / remote (orange) labels.
        if (node.isHead) {
            auto *headText = new BranchLabelItem(QStringLiteral("HEAD"),
                                                 QColor("#DC2626"), node.hash);
            QFont headFont = headText->font();
            headFont.setBold(true);
            headText->setFont(headFont);
            headText->setPos(x + kNodeRadius * 2 + 6, y - 14);
            m_scene->addItem(headText);
            m_itemsByHash[node.hash].append(headText);
        }
        // Ref labels on their own line under the message.
        qreal labelY = y + 40;
        for (const QString &branch : node.localBranches) {
            auto *label = new BranchLabelItem(branch, QColor("#2563EB"),
                                              node.hash);
            label->setPos(x + kNodeRadius * 2 + 6, labelY);
            m_scene->addItem(label);
            m_itemsByHash[node.hash].append(label);
            labelY += 14;
        }
        for (const QString &branch : node.remoteBranches) {
            auto *label = new BranchLabelItem(branch, QColor("#EA580C"),
                                              node.hash);
            label->setPos(x + kNodeRadius * 2 + 6, labelY);
            m_scene->addItem(label);
            m_itemsByHash[node.hash].append(label);
            labelY += 14;
        }
        // v4-P3-4: tag labels (green), e.g. "v1.0".
        for (const QString &tag : node.tags) {
            auto *label = new BranchLabelItem(tag, QColor("#059669"),
                                              node.hash);
            label->setPos(x + kNodeRadius * 2 + 6, labelY);
            m_scene->addItem(label);
            m_itemsByHash[node.hash].append(label);
            labelY += 14;
        }
    }

    // v4-P6-4: dynamic scene rect - height follows the commit count
    // (rows * rowHeight + padding); no oversized fixed canvas. The
    // right padding is small because branch/tag labels are real scene
    // items and already included in itemsBoundingRect().
    m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(
        -20, -20, 40, 40));
    // Top-left alignment: a single commit renders at the top instead of
    // being centered in a large empty canvas.
    m_view->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    // v4-P6-9/10: fit content into the view - shrink only, never
    // enlarge a small graph beyond 1.0.
    fitGraphToView();
}

void CommitGraphWidget::fitGraphToView() {
    if (m_scene->itemsBoundingRect().isEmpty()) {
        return;
    }
    const QRectF bounds =
        m_scene->itemsBoundingRect().adjusted(-24, -24, 48, 48);
    if (m_view->width() <= 50 || m_view->height() <= 50) {
        return;
    }
    m_view->resetTransform();
    // v4-P6-11: default view fits the WIDTH (shrink only) - the height
    // stays natural and scrolls, instead of squeezing the whole history
    // into one long strip.
    const qreal sx = m_view->width() / bounds.width();
    if (sx < 1.0) {
        m_view->scale(sx, sx);
    }
    m_view->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_view->centerOn(0, 0);
}

void CommitGraphWidget::fitWindow() {
    if (m_scene->itemsBoundingRect().isEmpty()) {
        return;
    }
    const QRectF bounds =
        m_scene->itemsBoundingRect().adjusted(-24, -24, 48, 48);
    if (m_view->width() > 50 && m_view->height() > 50) {
        m_view->fitInView(bounds, Qt::KeepAspectRatio);
    }
    m_view->setAlignment(Qt::AlignLeft | Qt::AlignTop);
}

void CommitGraphWidget::resetView() {
    m_view->resetTransform();
    m_view->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_view->centerOn(0, 0);
}

void CommitGraphWidget::onSelectionChanged() {
    const QList<QGraphicsItem *> selected = m_scene->selectedItems();
    if (selected.isEmpty()) {
        return;
    }
    const QString hash = selected.first()->data(0).toString();
    if (m_nodesByHash.contains(hash)) {
        updateDetail(m_nodesByHash.value(hash));
        loadCommitDiff(hash);
    }
}

void CommitGraphWidget::onSearchTextChanged(const QString &text) {
    for (auto it = m_itemsByHash.constBegin(); it != m_itemsByHash.constEnd(); ++it) {
        const bool visible = commitMatches(m_nodesByHash.value(it.key()), text);
        const QList<QGraphicsItem *> items = it.value();
        for (QGraphicsItem *item : items) {
            item->setVisible(visible);
        }
    }
    m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(
        -20, -20, 40, 40));
    m_view->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    fitGraphToView();
}

void CommitGraphWidget::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    // v4-P6-9/10: the view has its real size now - shrink-to-fit once.
    fitGraphToView();
}

void CommitGraphWidget::updateDetail(const CommitNode &node) {
    m_currentNode = node;
    m_detailLabel->setText(nodeDetailText(node));
}

void CommitGraphWidget::loadCommitDiff(const QString &hash) {
    if (m_repositoryPath.isEmpty()) {
        m_filesLabel->setText(QStringLiteral("变更文件：-"));
        m_diffViewer->showMessage(QStringLiteral("未设置仓库路径，无法加载 diff"));
        return;
    }
    const GitCommitDiffService::LoadResult result =
        GitCommitDiffService().load(m_repositoryPath, hash);
    if (!result.success) {
        m_filesLabel->setText(QStringLiteral("变更文件：读取失败"));
        m_diffViewer->showMessage(result.error);
        return;
    }
    const CommitDiff &diff = result.diff;
    QString filesText = QStringLiteral("变更文件：%1 个")
                            .arg(diff.files.size());
    if (diff.files.isEmpty()) {
        // v4-P6-12: explicit empty text instead of a big blank panel.
        filesText += QStringLiteral("\n该提交没有可显示的文件变化");
    } else {
        filesText += QStringLiteral("\n") + diff.files.join(QStringLiteral("、"));
    }
    m_filesLabel->setText(filesText);
    QString header = QStringLiteral("+%1  −%2\n\n")
                         .arg(diff.additions)
                         .arg(diff.deletions);
    m_diffViewer->showTextDiff(header + diff.diffText);
}

void CommitGraphWidget::requestCherryPick(const QString &hash) {
    emit cherryPickRequested(hash);
}

void CommitGraphWidget::copyHashToClipboard(const QString &hash) {
    QApplication::clipboard()->setText(hash);
}
