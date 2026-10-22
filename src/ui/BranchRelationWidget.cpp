#include "BranchRelationWidget.h"
#include "GitCommitGraphService.h"

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsPathItem>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMenu>
#include <QAction>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QPen>
#include <QBrush>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QEvent>
#include <QTimer>
#include <QScrollBar>
#include <QMap>
#include <algorithm>
#include "GitCommandExecutor.h"

namespace {
bool runGitRead(const QString &repo, const QStringList &args,
                QString *out = nullptr) {
    if (args.isEmpty()) {
        return false;
    }
    GitCommandExecutor executor;
    executor.setWorkingDirectory(repo);
    const GitCommandExecutor::Result result =
        executor.execute(args.first(), args.mid(1), 15000);
    if (result.error.hasError() || result.exitCode != 0) {
        return false;
    }
    if (out) {
        *out = QString::fromUtf8(result.output);
    }
    return true;
}

QString shortHash(const QString &full) {
    return full.left(7);
}

constexpr qreal kBadgeHeight = 22;
constexpr qreal kBadgeGap = 8;
constexpr qreal kScenePadding = 18;
constexpr qreal kNodeRadius = 6;
constexpr qreal kBadgeToNodeGap = 8;

QString visualText(const QString &text) {
    return text.trimmed();
}

QFont badgeFont() {
    QFont font;
    font.setPointSizeF(9);
    font.setBold(true);
    return font;
}

QString elidedBadgeText(const QString &text) {
    return QFontMetricsF(badgeFont()).elidedText(
        visualText(text), Qt::ElideRight, 160);
}

qreal badgeWidthForText(const QString &text) {
    return qBound<qreal>(48,
                          QFontMetricsF(badgeFont()).horizontalAdvance(
                              elidedBadgeText(text))
                              + 12,
                          172);
}

QString tagBadgeText(const QString &tag) {
    return QStringLiteral("tag: %1").arg(visualText(tag));
}
}  // namespace

// v4-P6-17: commit node - dot + short hash + message.
class RelationCommitNodeItem : public QGraphicsPathItem {
public:
    enum { Type = QGraphicsItem::UserType + 220 };
    RelationCommitNodeItem(qreal x, qreal y, const CommitNode &commit,
                           bool highlighted, bool showSubject,
                           bool showSecondaryMetadata, qreal hashOffset,
                           qreal messageOffset, qreal messageWidth,
                           BranchRelationWidget *host)
        : QGraphicsPathItem()
        , m_hash(visualText(commit.hash))
        , m_host(host) {
        const bool isHead = commit.isHead;
        const bool isMerge = commit.isMerge;
        QPainterPath nodePath;
        if (isMerge) {
            nodePath.moveTo(0, -kNodeRadius - 1);
            nodePath.lineTo(kNodeRadius + 1, 0);
            nodePath.lineTo(0, kNodeRadius + 1);
            nodePath.lineTo(-kNodeRadius - 1, 0);
            nodePath.closeSubpath();
        } else {
            nodePath.addEllipse(-kNodeRadius, -kNodeRadius,
                                kNodeRadius * 2, kNodeRadius * 2);
        }
        setPath(nodePath);
        setPos(x, y);
        setData(0, m_hash);
        setData(BranchRelationWidget::ItemKindRole,
                BranchRelationWidget::CommitNodeItem);
        setData(BranchRelationWidget::NodeSemanticRole,
                isMerge ? (isHead ? BranchRelationWidget::HeadMergeCommitNode
                                  : BranchRelationWidget::MergeCommitNode)
                        : (isHead ? BranchRelationWidget::HeadCommitNode
                                  : BranchRelationWidget::NormalCommitNode));
        setData(BranchRelationWidget::ParentCountRole, commit.parents.size());
        setFlags(QGraphicsItem::ItemIsSelectable);
        setZValue(3);
        setPen(QPen(isHead ? QColor("#1D4ED8")
                            : (isMerge ? QColor("#7C3AED")
                                       : QColor("#64748B")),
                    isHead ? 2.2 : 1.4));
        setBrush(QColor(isHead     ? "#3B82F6"
                        : isMerge ? "#DDD6FE"
                                  : "#94A3B8"));
        setCursor(Qt::PointingHandCursor);

        QStringList tooltipLines;
        tooltipLines << QStringLiteral("Hash: %1").arg(m_hash);
        const QString displayMessage = visualText(commit.message);
        if (!displayMessage.isEmpty()) {
            tooltipLines << QStringLiteral("Subject: %1").arg(displayMessage);
        }
        const QString displayAuthor = visualText(commit.author);
        if (!displayAuthor.isEmpty()) {
            tooltipLines << QStringLiteral("Author: %1").arg(displayAuthor);
        }
        if (commit.time.isValid()) {
            tooltipLines << QStringLiteral("Time: %1")
                                .arg(commit.time.toLocalTime().toString(
                                    QStringLiteral("yyyy-MM-dd HH:mm")));
        }
        tooltipLines << QStringLiteral("Parents: %1").arg(commit.parents.size());
        setToolTip(tooltipLines.join(QLatin1Char('\n')));

        const QString displayHash = shortHash(m_hash).trimmed();
        if (!displayHash.isEmpty()) {
            auto *text = new QGraphicsSimpleTextItem(this);
            text->setText(displayHash);
            text->setData(0, displayHash);
            text->setData(BranchRelationWidget::ItemKindRole,
                          BranchRelationWidget::CommitHashItem);
            QFont f = text->font();
            f.setPointSizeF(9);
            f.setBold(isHead);
            text->setFont(f);
            text->setBrush(isHead ? QColor("#1D4ED8") : QColor("#334155"));
            // The text item is parented to the node, so its position is local
            // to the ellipse rather than another scene-space coordinate.
            text->setPos(hashOffset, -4);
        }

        if (showSubject && !displayMessage.isEmpty()) {
            auto *messageText = new QGraphicsSimpleTextItem(this);
            QFont messageFont = messageText->font();
            messageFont.setPointSizeF(9);
            messageText->setFont(messageFont);
            const QString elided = QFontMetricsF(messageFont).elidedText(
                displayMessage, Qt::ElideRight, messageWidth);
            messageText->setText(elided);
            messageText->setData(0, displayMessage);
            messageText->setData(BranchRelationWidget::ItemKindRole,
                                 BranchRelationWidget::CommitMessageItem);
            messageText->setToolTip(displayMessage);
            messageText->setBrush(QColor("#475569"));
            messageText->setPos(messageOffset, -4);
        }

        QStringList secondaryParts;
        if (!displayAuthor.isEmpty()) {
            secondaryParts << displayAuthor;
        }
        if (commit.time.isValid()) {
            secondaryParts << commit.time.toLocalTime().toString(
                QStringLiteral("yyyy-MM-dd HH:mm"));
        }
        const QString secondary = secondaryParts.join(QStringLiteral(" · "));
        if (showSecondaryMetadata && !secondary.isEmpty()) {
            auto *secondaryText = new QGraphicsSimpleTextItem(this);
            QFont secondaryFont = secondaryText->font();
            secondaryFont.setPointSizeF(8);
            secondaryText->setFont(secondaryFont);
            secondaryText->setText(QFontMetricsF(secondaryFont).elidedText(
                secondary, Qt::ElideRight, messageWidth));
            secondaryText->setData(0, secondary);
            secondaryText->setData(BranchRelationWidget::ItemKindRole,
                                   BranchRelationWidget::CommitSecondaryItem);
            secondaryText->setToolTip(secondary);
            secondaryText->setBrush(QColor("#94A3B8"));
            secondaryText->setPos(messageOffset, 14);
        }

        if (highlighted) {
            setPen(QPen(QColor("#1D4ED8"), 2.2));
        }
    }
    int type() const override { return Type; }
    QString hash() const { return m_hash; }

protected:
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override;

private:
    QString m_hash;
    BranchRelationWidget *m_host;
};

void RelationCommitNodeItem::mouseDoubleClickEvent(
    QGraphicsSceneMouseEvent *event) {
    event->accept();
    const QString branch = m_host->branchForCommit(m_hash);
    if (!branch.isEmpty() && !m_host->isRemoteBranch(branch)) {
        emit m_host->switchBranchRequested(branch);
    }
}

void RelationCommitNodeItem::contextMenuEvent(
    QGraphicsSceneContextMenuEvent *event) {
    event->accept();
    const QString branch = m_host->branchForCommit(m_hash);
    if (branch.isEmpty()) {
        return;
    }
    QGraphicsView *v = m_host->view();
    m_host->showContextMenu(
        branch, v->viewport()->mapToGlobal(v->mapFromScene(event->scenePos())),
        m_host->isRemoteBranch(branch));
}

// v4-P6-17: branch label tag at a branch tip commit.
class RelationBranchTagItem : public QGraphicsRectItem {
public:
    enum { Type = QGraphicsItem::UserType + 222 };
    RelationBranchTagItem(qreal x, qreal y, const QString &name,
                  const QColor &bg, const QColor &fg, const QColor &border,
                  BranchRelationWidget *host)
        : QGraphicsRectItem()
        , m_name(visualText(name))
        , m_host(host) {
        Q_ASSERT(!m_name.isEmpty());
        setData(0, m_name);
        setData(1, false);  // v4-P6-21R6 P1-03: remote flag slot
        setData(BranchRelationWidget::ItemKindRole,
                BranchRelationWidget::BranchBadgeItem);
        setFlags(QGraphicsItem::ItemIsSelectable);
        setZValue(4);
        setPen(QPen(border, 1.2));
        setBrush(bg);
        setCursor(Qt::PointingHandCursor);
        // v4-P6-21R6 P1-03: typed remote classification for read-only badges.
        setRemote(false);
        auto *label = new QGraphicsSimpleTextItem(this);
        const QFont f = badgeFont();
        label->setFont(f);
        label->setBrush(fg);
        const QString elided = elidedBadgeText(m_name);
        const qreal width = badgeWidthForText(m_name);
        setRect(0, 0, width, kBadgeHeight);
        setPos(x, y);
        label->setText(elided);
        label->setPos(6, 3);
    }
    // v4-P6-21R6 P1-03: typed remote classification for read-only badges.
    void setRemote(bool remote) {
        m_isRemote = remote;
        setData(1, remote);
        setToolTip(QStringLiteral("refs/%1/%2")
                       .arg(remote ? QStringLiteral("remotes")
                                   : QStringLiteral("heads"),
                            m_name));
    }
    int type() const override { return Type; }
    QString name() const { return m_name; }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override {
        QGraphicsRectItem::mousePressEvent(event);
        event->accept();
        if (m_host) {
            m_host->selectBranchTag(m_name, m_isRemote);
        }
    }
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override {
        QGraphicsRectItem::mouseDoubleClickEvent(event);
        event->accept();
        // v4-P6-21R6 P1-03: remote-tracking badges are READ-ONLY - a
        // double click must never switch (or fall back to) a previously
        // selected LOCAL branch.
        if (m_isRemote) {
            return;
        }
        emit m_host->switchBranchRequested(m_name);
    }
    void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override {
        event->accept();
        QGraphicsView *v = m_host->view();
        m_host->showContextMenu(
            m_name,
            v->viewport()->mapToGlobal(v->mapFromScene(event->scenePos())),
            m_isRemote);
    }

private:
    QString m_name;
    BranchRelationWidget *m_host;
    // v4-P6-21R6 P1-03: typed remote flag - read-only badges.
    bool m_isRemote = false;
};

class RelationTagBadgeItem : public QGraphicsRectItem {
public:
    enum { Type = QGraphicsItem::UserType + 223 };
    RelationTagBadgeItem(qreal x, qreal y, const QString &tag)
        : QGraphicsRectItem()
        , m_tag(visualText(tag)) {
        Q_ASSERT(!m_tag.isEmpty());
        setData(0, m_tag);
        setData(BranchRelationWidget::ItemKindRole,
                BranchRelationWidget::TagBadgeItem);
        setZValue(4);
        setPen(QPen(QColor("#FCD34D"), 1.2));
        setBrush(QColor("#FFFBEB"));
        auto *label = new QGraphicsSimpleTextItem(this);
        const QFont font = badgeFont();
        label->setFont(font);
        label->setBrush(QColor("#92400E"));
        const QString displayText = tagBadgeText(m_tag);
        const QString elided = elidedBadgeText(displayText);
        const qreal width = badgeWidthForText(displayText);
        setRect(0, 0, width, kBadgeHeight);
        setPos(x, y);
        label->setText(elided);
        label->setPos(6, 3);
        setToolTip(QStringLiteral("refs/tags/%1").arg(m_tag));
    }
    int type() const override { return Type; }

private:
    QString m_tag;
};

BranchRelationWidget::BranchRelationWidget(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    // v4-P6-17 toolbar: 查看全部 / 最近提交 / 全部分支 / 只看当前分支 /
    // 搜索 (+ filters).
    auto *toolRow = new QHBoxLayout();
    m_showAllButton = new QPushButton(QStringLiteral("查看全部"), this);
    m_showAllButton->setObjectName("relationShowAll");
    m_showAllButton->setCheckable(true);
    m_showAllButton->setChecked(false);  // v4-P6-20: recent commits first
    m_recentButton = new QPushButton(QStringLiteral("最近提交"), this);
    m_recentButton->setObjectName("relationRecent");
    m_recentButton->setCheckable(true);
    m_recentButton->setChecked(true);  // v4-P6-20: default view = recent
    m_allBranchesButton =
        new QPushButton(QStringLiteral("全部分支"), this);
    m_allBranchesButton->setObjectName("relationAllBranches");
    m_allBranchesButton->setCheckable(true);
    m_allBranchesButton->setChecked(true);
    m_currentBranchOnlyButton =
        new QPushButton(QStringLiteral("只看当前分支"), this);
    m_currentBranchOnlyButton->setObjectName("relationCurrentOnly");
    m_currentBranchOnlyButton->setCheckable(true);
    m_showRemoteCheck =
        new QCheckBox(QStringLiteral("显示 Remote 分支"), this);
    m_showRemoteCheck->setObjectName("relationShowRemote");
    m_showRemoteCheck->setChecked(false);
    m_unmergedOnlyCheck =
        new QCheckBox(QStringLiteral("只显示未合并分支"), this);
    m_unmergedOnlyCheck->setObjectName("relationUnmergedOnly");
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName("relationSearch");
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索提交…"));
    m_searchEdit->setFixedWidth(160);
    const QString btnStyle =
        "QPushButton { background: #FFFFFF; color: #1F2937;"
        " border: 1px solid #D1D5DB; border-radius: 6px;"
        " padding: 4px 10px; font-size: 12px; }"
        "QPushButton:hover { background: #F3F4F6; }"
        "QPushButton:checked { background: #DBEAFE; color: #1D4ED8;"
        " border-color: #60A5FA; font-weight: 600; }";
    for (QPushButton *b :
         {m_showAllButton, m_recentButton, m_allBranchesButton,
          m_currentBranchOnlyButton}) {
        b->setStyleSheet(btnStyle);
    }
    toolRow->addWidget(m_showAllButton);
    toolRow->addWidget(m_recentButton);
    toolRow->addSpacing(12);
    toolRow->addWidget(m_allBranchesButton);
    toolRow->addWidget(m_currentBranchOnlyButton);
    toolRow->addSpacing(12);
    toolRow->addWidget(m_showRemoteCheck);
    toolRow->addWidget(m_unmergedOnlyCheck);
    toolRow->addWidget(m_searchEdit);
    toolRow->addStretch();
    layout->addLayout(toolRow);

    m_scene = new QGraphicsScene(this);
    m_view = new QGraphicsView(m_scene, this);
    m_view->setObjectName("relationView");
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setBackgroundBrush(QColor("#FAFAFA"));
    m_view->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_view->viewport()->installEventFilter(this);
    layout->addWidget(m_view, 1);

    m_detailLabel = new QLabel(QStringLiteral("点击提交或分支查看详情"), this);
    m_detailLabel->setObjectName("relationDetail");
    m_detailLabel->setWordWrap(true);
    m_detailLabel->setStyleSheet(
        "QLabel { background: #FFFFFF; border: 1px solid #E5E7EB;"
        " border-radius: 8px; padding: 8px; color: #111827;"
        " font-size: 12px; }");
    layout->addWidget(m_detailLabel);

    auto rebuild = [this]() { buildScene(); };
    connect(m_showAllButton, &QPushButton::toggled, this, [this](bool on) {
        if (on) {
            m_recentButton->setChecked(false);
            buildScene();
        }
    });
    connect(m_recentButton, &QPushButton::toggled, this, [this](bool on) {
        if (on) {
            m_showAllButton->setChecked(false);
            buildScene();
        }
    });
    connect(m_allBranchesButton, &QPushButton::toggled, this,
            [this](bool on) {
                if (on) {
                    m_currentBranchOnlyButton->setChecked(false);
                    buildScene();
                }
            });
    connect(m_currentBranchOnlyButton, &QPushButton::toggled, this,
            [this](bool on) {
                if (on) {
                    m_allBranchesButton->setChecked(false);
                    buildScene();
                }
            });
    connect(m_showRemoteCheck, &QCheckBox::toggled, this, rebuild);
    connect(m_unmergedOnlyCheck, &QCheckBox::toggled, this, rebuild);
    connect(m_searchEdit, &QLineEdit::textChanged, this, rebuild);
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this]() {
        const QList<QGraphicsItem *> selected = m_scene->selectedItems();
        if (selected.isEmpty()) {
            return;
        }
        QGraphicsItem *item = selected.first();
        const QString key = item->data(0).toString();
        if (key.isEmpty()) {
            return;
        }
        // v4-P6-17: branch tags and commit nodes show different detail.
        if (item->type() == RelationBranchTagItem::Type) {
            selectBranchTag(key, item->data(1).toBool());
            return;
        }
        m_selected = key;
        m_selectedBranch.clear();
        m_selectedBranchRemote = false;
        for (const auto &c : m_commits) {
            if (c.hash == key) {
                QStringList refs = c.localBranches + c.remoteBranches + c.tags;
                m_detailLabel->setText(
                    QStringLiteral("提交：%1\n说明：%2\n"
                                   "类型：%3\n分支：%4\n标签：%5")
                        .arg(c.hash,
                             c.message.isEmpty() ? QStringLiteral("(无)")
                                                 : c.message,
                             c.isMerge ? QStringLiteral("合并提交")
                                       : (c.isHead
                                              ? QStringLiteral("当前 HEAD")
                                              : QStringLiteral("普通提交")),
                             refs.isEmpty() ? QStringLiteral("(无)")
                                            : refs.join(QStringLiteral(", ")),
                             c.tags.isEmpty() ? QStringLiteral("(无)")
                                              : c.tags.join(QStringLiteral(", "))));
                break;
            }
        }
    });
}

void BranchRelationWidget::selectBranchTag(const QString &branch, bool isRemote) {
    m_selected.clear();
    m_selectedBranch = branch;
    m_selectedBranchRemote = isRemote;
    for (const BranchNode &n : m_nodes) {
        if (n.name == branch && n.isRemote == isRemote) {
            m_detailLabel->setText(
                QStringLiteral("分支：%1\n最新提交：%2\n"
                               "Upstream：%3\nAhead / Behind：%4 / %5\n"
                               "合并状态：%6\n当前分支：%7")
                    .arg(n.name, n.hash,
                         n.upstream.isEmpty() ? QStringLiteral("(无)")
                                              : n.upstream,
                         QString::number(n.ahead),
                         QString::number(n.behind),
                         n.merged ? QStringLiteral("已合并")
                                  : QStringLiteral("未合并"),
                         n.isHead ? QStringLiteral("是")
                                  : QStringLiteral("否")));
            return;
        }
    }
}

void BranchRelationWidget::setRepositoryPath(const QString &path) {
    m_repositoryPath = path;
}

int BranchRelationWidget::branchNodeCount() const {
    int count = 0;
    for (QGraphicsItem *item : m_scene->items()) {
        if (item->type() == RelationBranchTagItem::Type) {
            ++count;
        }
    }
    return count;
}

QStringList BranchRelationWidget::branchNames() const {
    QStringList names;
    for (QGraphicsItem *item : m_scene->items()) {
        if (item->type() == RelationBranchTagItem::Type) {
            names << item->data(0).toString();
        }
    }
    return names;
}

bool BranchRelationWidget::hasCurrentBranchNode() const {
    for (const BranchNode &n : m_nodes) {
        if (n.isHead) {
            return true;
        }
    }
    return false;
}

void BranchRelationWidget::refresh() {
    loadBranches();
    loadCommits();
    buildScene();
}

qreal BranchRelationWidget::logicalViewportWidth() const {
    if (m_view && m_view->viewport() && m_view->viewport()->width() > 0) {
        return m_view->viewport()->width();
    }
    return qMax<qreal>(720, width());
}

void BranchRelationWidget::scheduleViewportRelayout() {
    if (m_viewportRelayoutPending || !m_view || !m_view->viewport()
        || m_commits.isEmpty()) {
        return;
    }
    m_viewportRelayoutPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_viewportRelayoutPending = false;
        if (!m_view || !m_view->viewport()
            || m_view->viewport()->width() <= 0) {
            return;
        }
        const QSize viewportSize = m_view->viewport()->size();
        if (viewportSize == m_lastLaidOutViewportSize) {
            return;
        }
        m_lastLaidOutViewportSize = viewportSize;
        buildScene(true);
    });
}

bool BranchRelationWidget::eventFilter(QObject *watched, QEvent *event) {
    if (m_view && watched == m_view->viewport()
        && event->type() == QEvent::Resize) {
        scheduleViewportRelayout();
    }
    return QWidget::eventFilter(watched, event);
}

void BranchRelationWidget::loadBranches() {
    m_nodes.clear();
    if (m_repositoryPath.isEmpty()) {
        return;
    }
    QString out;
    if (!runGitRead(m_repositoryPath,
                    {"branch", "--format=%(refname:short)|%(objectname:short)|"
                     "%(HEAD)|%(upstream:short)|%(upstream:track)"},
                    &out)) {
        return;
    }
    QStringList mergedList;
    QString merged;
    if (runGitRead(m_repositoryPath, {"branch", "--merged", "HEAD"}, &merged)) {
        for (const QString &line :
             merged.split('\n', Qt::SkipEmptyParts)) {
            mergedList << line.trimmed();
        }
    }
    const QStringList lines =
        out.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList parts = line.split('|');
        if (parts.size() < 3) {
            continue;
        }
        BranchNode node;
        node.name = visualText(parts.at(0));
        node.hash = visualText(parts.at(1));
        if (node.name.isEmpty()) {
            continue;
        }
        node.isHead = parts.at(2) == QStringLiteral("*");
        if (node.isHead) {
            m_currentBranch = node.name;
        }
        node.upstream = parts.size() > 3 ? parts.at(3) : QString();
        const QString track =
            parts.size() > 4 ? parts.at(4) : QString();
        QRegularExpression aheadRe(QStringLiteral("ahead (\\d+)"));
        QRegularExpression behindRe(QStringLiteral("behind (\\d+)"));
        const QRegularExpressionMatch am = aheadRe.match(track);
        const QRegularExpressionMatch bm = behindRe.match(track);
        node.ahead = am.hasMatch() ? am.captured(1).toInt() : 0;
        node.behind = bm.hasMatch() ? bm.captured(1).toInt() : 0;
        node.merged = mergedList.contains(node.name);
        m_nodes.append(node);
    }
    QString remoteOut;
    if (runGitRead(m_repositoryPath,
                   {"branch", "-r",
                    "--format=%(refname:short)|%(objectname:short)"},
                   &remoteOut)) {
        for (const QString &line :
             remoteOut.split('\n', Qt::SkipEmptyParts)) {
            const QStringList parts = line.split('|');
            if (parts.size() < 2) {
                continue;
            }
            BranchNode node;
            node.name = visualText(parts.at(0));
            node.hash = visualText(parts.at(1));
            if (node.name.isEmpty()) {
                continue;
            }
            node.isRemote = true;
            node.merged = mergedList.contains(node.name);
            m_nodes.append(node);
        }
    }
}

QString BranchRelationWidget::headCommitHash() const {
    for (const auto &c : m_commits) {
        if (c.isHead) {
            return c.hash;
        }
    }
    return QString();
}

void BranchRelationWidget::loadCommits() {
    m_commits.clear();
    if (m_repositoryPath.isEmpty()) {
        return;
    }
    GitCommitGraphService service;
    const auto result = service.load(m_repositoryPath);
    m_commits = result.commits;
}

QString BranchRelationWidget::branchForCommit(const QString &hash) const {
    for (const auto &c : m_commits) {
        if (c.hash == hash) {
            if (!c.localBranches.isEmpty()) {
                return c.localBranches.first();
            }
            if (!c.remoteBranches.isEmpty()) {
                return c.remoteBranches.first();
            }
            break;
        }
    }
    return QString();
}

bool BranchRelationWidget::isRemoteBranch(const QString &branch) const {
    bool remoteMatch = false;
    for (const BranchNode &node : m_nodes) {
        if (node.name != branch) {
            continue;
        }
        if (!node.isRemote) {
            return false;
        }
        remoteMatch = true;
    }
    return remoteMatch;
}

bool BranchRelationWidget::branchLabelGeometryValid() const {
    if (!m_scene) {
        return true;
    }
    QList<QRectF> hashRects;
    QList<QRectF> badgeRects;
    QList<QRectF> nodeRects;
    QList<QRectF> messageRects;
    QList<QRectF> secondaryRects;
    for (QGraphicsItem *item : m_scene->items()) {
        if (item->data(ItemKindRole).toInt() == CommitHashItem) {
            hashRects.append(item->sceneBoundingRect());
        } else if (item->data(ItemKindRole).toInt() == BranchBadgeItem
                   || item->data(ItemKindRole).toInt() == TagBadgeItem) {
            badgeRects.append(item->sceneBoundingRect());
        } else if (item->data(ItemKindRole).toInt() == CommitNodeItem) {
            nodeRects.append(item->sceneBoundingRect());
        } else if (item->data(ItemKindRole).toInt() == CommitMessageItem) {
            messageRects.append(item->sceneBoundingRect());
        } else if (item->data(ItemKindRole).toInt() == CommitSecondaryItem) {
            secondaryRects.append(item->sceneBoundingRect());
        }
    }
    for (const QRectF &hash : hashRects) {
        for (const QRectF &badge : badgeRects) {
            if (hash.intersects(badge)) {
                return false;
            }
        }
    }
    for (const QRectF &node : nodeRects) {
        for (const QRectF &badge : badgeRects) {
            if (node.intersects(badge)) {
                return false;
            }
        }
    }
    for (const QRectF &message : messageRects) {
        for (const QRectF &badge : badgeRects) {
            if (message.intersects(badge)) {
                return false;
            }
        }
    }
    for (const QRectF &secondary : secondaryRects) {
        for (const QRectF &badge : badgeRects) {
            if (secondary.intersects(badge)) {
                return false;
            }
        }
    }
    for (int i = 0; i < badgeRects.size(); ++i) {
        for (int j = i + 1; j < badgeRects.size(); ++j) {
            if (badgeRects.at(i).intersects(badgeRects.at(j))) {
                return false;
            }
        }
    }
    return true;
}

int BranchRelationWidget::emptyVisualMetadataCountForTesting() const {
    int count = 0;
    for (QGraphicsItem *item : m_scene->items()) {
        const int kind = item->data(ItemKindRole).toInt();
        if (kind == CommitHashItem || kind == CommitMessageItem
            || kind == CommitSecondaryItem || kind == BranchBadgeItem
            || kind == TagBadgeItem) {
            if (item->data(0).toString().trimmed().isEmpty()) {
                ++count;
            }
        }
    }
    return count;
}

void BranchRelationWidget::buildScene(bool preserveSelection) {
    const QString selectedBefore = preserveSelection ? m_selected : QString();
    const QString selectedBranchBefore =
        preserveSelection ? m_selectedBranch : QString();
    const bool selectedBranchRemoteBefore =
        preserveSelection && m_selectedBranchRemote;
    const QString detailBefore = preserveSelection && m_detailLabel
                                      ? m_detailLabel->text()
                                      : QString();
    m_scene->clear();
    if (!preserveSelection) {
        m_selected.clear();
        m_selectedBranch.clear();
        m_selectedBranchRemote = false;
    }
    m_layoutMode = MultiBranchGraph;
    m_visibleLaneCount = 0;
    if (!preserveSelection) {
        m_detailLabel->setText(QStringLiteral("点击提交或分支查看详情"));
    }
    if (m_commits.isEmpty()) {
        auto *empty = m_scene->addSimpleText(
            QStringLiteral("暂无提交数据"));
        empty->setBrush(QColor("#6B7280"));
        empty->setPos(16, 16);
        emit graphLayoutRebuilt();
        return;
    }

    // Branch visibility filter.
    const bool currentOnly = m_currentBranchOnlyButton->isChecked();
    const bool unmergedOnly = m_unmergedOnlyCheck->isChecked();
    QStringList visibleBranches;
    QList<BranchNode> visibleNodes;
    for (const BranchNode &n : m_nodes) {
        if (n.isRemote && !m_showRemoteCheck->isChecked()) {
            continue;
        }
        if (unmergedOnly && n.merged && !n.isHead) {
            continue;
        }
        if (currentOnly && !n.isHead) {
            continue;
        }
        visibleBranches << n.name;
        visibleNodes << n;
    }
    const auto isVisibleRef = [&visibleNodes](const QString &name,
                                              bool isRemote) {
        for (const BranchNode &node : visibleNodes) {
            if (node.name == name && node.isRemote == isRemote) {
                return true;
            }
        }
        return false;
    };

    // Commit limit (recent view).
    const int limit =
        m_recentButton->isChecked() ? 30 : m_commits.size();

    // Lane assignment: HEAD branch first, others in first-seen order.
    QStringList laneBranches;
    if (!m_currentBranch.isEmpty()) {
        laneBranches << m_currentBranch;
    }
    QMap<QString, int> branchLane;
    QMap<QString, QList<QString>> commitBranches;  // hash -> branches
    for (const auto &c : m_commits) {
        QStringList bs = c.localBranches + c.remoteBranches;
        commitBranches[c.hash] = bs;
        for (const QString &b : bs) {
            if (!laneBranches.contains(b)) {
                laneBranches << b;
            }
        }
    }
    for (int i = 0; i < laneBranches.size(); ++i) {
        branchLane[laneBranches.at(i)] = i;
    }

    // Filter commits by visible branches / current-only chain.
    QSet<QString> keepHashes;
    if (currentOnly) {
        // Walk first-parent chain from HEAD.
        const QString headHash =
            headCommitHash();
        if (!headHash.isEmpty()) {
            const QMap<QString, CommitNode> byHash =
                [this]() {
                    QMap<QString, CommitNode> m;
                    for (const auto &c : m_commits) {
                        m[c.hash] = c;
                    }
                    return m;
                }();
            QString h = headHash;
            while (!h.isEmpty() && byHash.contains(h)) {
                keepHashes.insert(h);
                const auto &c = byHash[h];
                h = c.parents.isEmpty() ? QString() : c.parents.first();
            }
        }
    } else {
        // v4-P6-21 ISSUE-08: walk the parent DAG from every visible
        // branch/ref tip - intermediate commits WITHOUT branch labels
        // (e.g. F1/F2 below a feature tip) must stay in the graph, and
        // merge commits contribute ALL their parents.
        const QMap<QString, CommitNode> byHash =
            [this]() {
                QMap<QString, CommitNode> m;
                for (const auto &c : m_commits) {
                    m[c.hash] = c;
                }
                return m;
            }();
        QStringList queue;
        QSet<QString> seeded;
        for (const auto &c : m_commits) {
            QStringList visibleTips;
            for (const QString &b : c.localBranches) {
                if (isVisibleRef(b, false)) {
                    visibleTips << b;
                }
            }
            for (const QString &b : c.remoteBranches) {
                if (isVisibleRef(b, true)) {
                    visibleTips << b;
                }
            }
            for (const QString &b : visibleTips) {
                if (!seeded.contains(c.hash)) {
                    queue << c.hash;
                    seeded.insert(c.hash);
                    break;
                }
            }
        }
        while (!queue.isEmpty()) {
            const QString h = queue.takeFirst();
            if (keepHashes.contains(h)) {
                continue;
            }
            keepHashes.insert(h);
            const auto it = byHash.constFind(h);
            if (it == byHash.constEnd()) {
                continue;
            }
            for (const QString &p : it->parents) {
                if (!keepHashes.contains(p)) {
                    queue << p;
                }
            }
        }
    }
        // Always keep the HEAD chain so the current line is visible.
        const QString headHash =
            headCommitHash();
        if (!headHash.isEmpty()) {
            const QMap<QString, CommitNode> byHash =
                [this]() {
                    QMap<QString, CommitNode> m;
                    for (const auto &c : m_commits) {
                        m[c.hash] = c;
                    }
                    return m;
                }();
            QString h = headHash;
            while (!h.isEmpty() && byHash.contains(h)) {
                keepHashes.insert(h);
                h = byHash[h].parents.isEmpty() ? QString()
                                                : byHash[h].parents.first();
            }
        }

    // Search filter.
    const QString search = m_searchEdit->text().trimmed();
    QSet<QString> searchHashes;
    if (!search.isEmpty()) {
        for (const auto &c : m_commits) {
            if (c.message.contains(search, Qt::CaseInsensitive)
                || c.author.contains(search, Qt::CaseInsensitive)
                || c.shortHash.contains(search, Qt::CaseInsensitive)
                || c.hash.startsWith(search, Qt::CaseInsensitive)) {
                searchHashes.insert(c.hash);
            }
        }
    }

    // Build the visible commit list (newest first), then choose the layout
    // from the visible topology rather than the number of branch labels.
    struct MetadataBadge {
        QString text;
        bool isBranch = false;
        bool isRemote = false;
    };
    struct Placed {
        QString hash;
        int lane = 0;
        qreal y = 0;
        int badgeRows = 0;
    };
    QList<Placed> placed;
    QMap<QString, Placed> placedByHash;
    constexpr qreal kLaneWidth = 42;
    constexpr qreal kMultiGraphX = 56;
    constexpr qreal kMultiRowGap = 56;
    constexpr qreal kMultiTopPadding = 20;
    constexpr qreal kTimelineTopPadding = 42;
    constexpr qreal kTimelineContentGap = 18;
    QFont hashFont;
    hashFont.setPointSizeF(9);
    hashFont.setBold(true);
    const qreal hashWidth =
        QFontMetricsF(hashFont).horizontalAdvance(QStringLiteral("1234567"));
    QFont secondaryFont;
    secondaryFont.setPointSizeF(8);
    const qreal timelineContentBottom = qMax<qreal>(
        kNodeRadius + 1,
        14 + QFontMetricsF(secondaryFont).height());
    const qreal timelineRowAdvance = qMax<qreal>(
        58, timelineContentBottom + kNodeRadius + kTimelineContentGap);
    int usedLanes = 0;

    auto placeCommit = [&](const CommitNode &commit, int lane) {
        Placed placedCommit;
        placedCommit.hash = commit.hash;
        placedCommit.lane = lane;
        placed.append(placedCommit);
        placedByHash[commit.hash] = placedCommit;
        usedLanes = qMax(usedLanes, lane + 1);
    };

    for (const auto &commit : m_commits) {
        if (commit.hash.trimmed().isEmpty()
            || placedByHash.contains(commit.hash)
            || !keepHashes.contains(commit.hash)
            || (!search.isEmpty() && !searchHashes.contains(commit.hash))) {
            continue;
        }
        if (placed.size() >= limit) {
            break;
        }
        int lane = 0;
        const QStringList branches = commit.localBranches + commit.remoteBranches;
        bool laneFound = false;
        for (const QString &branch : branches) {
            if (branchLane.contains(branch)) {
                lane = branchLane[branch];
                laneFound = true;
                break;
            }
        }
        if (!laneFound && !commit.parents.isEmpty()) {
            const auto parent = placedByHash.constFind(commit.parents.first());
            if (parent != placedByHash.constEnd()) {
                lane = parent->lane;
            }
        }
        placeCommit(commit, lane);
    }

    if (placed.isEmpty()) {
        auto *empty = m_scene->addSimpleText(
            QStringLiteral("没有符合筛选条件的提交"));
        empty->setBrush(QColor("#6B7280"));
        empty->setPos(16, 16);
        emit graphLayoutRebuilt();
        return;
    }

    QMap<QString, const CommitNode *> commitsByHash;
    for (const CommitNode &commit : m_commits) {
        commitsByHash.insert(commit.hash, &commit);
    }
    QMap<QString, int> visibleChildCounts;
    QMap<QString, QStringList> adjacency;
    bool requiresMultiBranchGraph = false;
    for (const Placed &placedCommit : placed) {
        const CommitNode *commit = commitsByHash.value(placedCommit.hash, nullptr);
        if (!commit) {
            continue;
        }
        if (commit->isMerge) {
            requiresMultiBranchGraph = true;
        }
        int visibleParents = 0;
        for (const QString &parent : commit->parents) {
            if (!placedByHash.contains(parent)) {
                continue;
            }
            ++visibleParents;
            ++visibleChildCounts[parent];
            adjacency[placedCommit.hash].append(parent);
            adjacency[parent].append(placedCommit.hash);
        }
        if (visibleParents > 1) {
            requiresMultiBranchGraph = true;
        }
    }
    for (auto child = visibleChildCounts.cbegin();
         child != visibleChildCounts.cend(); ++child) {
        if (child.value() > 1) {
            requiresMultiBranchGraph = true;
        }
    }
    QSet<QString> visited;
    int components = 0;
    for (const Placed &start : placed) {
        if (visited.contains(start.hash)) {
            continue;
        }
        ++components;
        QStringList queue{start.hash};
        visited.insert(start.hash);
        while (!queue.isEmpty()) {
            const QString current = queue.takeFirst();
            for (const QString &neighbor : adjacency.value(current)) {
                if (!visited.contains(neighbor)) {
                    visited.insert(neighbor);
                    queue.append(neighbor);
                }
            }
        }
    }
    if (components > 1) {
        requiresMultiBranchGraph = true;
    }
    m_layoutMode = requiresMultiBranchGraph ? MultiBranchGraph : LinearHistory;
    if (m_layoutMode == LinearHistory) {
        for (Placed &placedCommit : placed) {
            placedCommit.lane = 0;
            placedByHash[placedCommit.hash] = placedCommit;
        }
        usedLanes = 1;
    }
    m_visibleLaneCount = usedLanes;

    const bool linearHistory = usesLinearHistoryLayout();
    const qreal viewportWidth = linearHistory ? logicalViewportWidth() : 0;
    const qreal graphX = linearHistory
                             ? qMax<qreal>(60, viewportWidth * 0.18)
                             : kMultiGraphX;
    const qreal rightPadding = linearHistory
                                   ? qMax<qreal>(36, viewportWidth * 0.06)
                                   : kScenePadding;
    const qreal timelineRight = linearHistory
                                    ? qMin(viewportWidth - rightPadding,
                                           graphX + viewportWidth * 0.50)
                                    : 0;
    const auto laneX = [graphX](int lane) {
        return graphX + lane * kLaneWidth;
    };

    const auto metadataForCommit = [&](const CommitNode &commit) {
        QList<MetadataBadge> badges;
        QSet<QString> seenBadges;
        const auto appendBranch = [&](const QString &name, bool remote) {
            const QString text = visualText(name);
            const QString key = QStringLiteral("branch:%1:%2")
                                    .arg(remote ? QStringLiteral("remote")
                                                : QStringLiteral("local"),
                                         text);
            if (!text.isEmpty() && !seenBadges.contains(key)
                && isVisibleRef(text, remote)) {
                badges.append({text, true, remote});
                seenBadges.insert(key);
            }
        };
        for (const QString &name : commit.localBranches) {
            appendBranch(name, false);
        }
        for (const QString &name : commit.remoteBranches) {
            appendBranch(name, true);
        }
        for (const QString &name : commit.tags) {
            const QString text = visualText(name);
            const QString key = QStringLiteral("tag:%1").arg(text);
            if (!text.isEmpty() && !seenBadges.contains(key)) {
                badges.append({text, false, false});
                seenBadges.insert(key);
            }
        }
        return badges;
    };
    QMap<QString, QList<MetadataBadge>> badgesByHash;
    for (const Placed &placedCommit : placed) {
        const CommitNode *commit = commitsByHash.value(placedCommit.hash, nullptr);
        if (commit) {
            badgesByHash.insert(placedCommit.hash, metadataForCommit(*commit));
        }
    }

    const qreal linearHashOffset = 14;
    const qreal linearMessageOffset = linearHashOffset + hashWidth + 16;
    const qreal linearMessageWidth = qMax<qreal>(
        80, timelineRight - (graphX + linearMessageOffset));
    qreal multiMetadataX = graphX + (usedLanes - 1) * kLaneWidth + 14;
    if (!linearHistory) {
        for (const Placed &placedCommit : placed) {
            qreal badgeRight = laneX(placedCommit.lane) + 14;
            for (const MetadataBadge &badge :
                 badgesByHash.value(placedCommit.hash)) {
                const QString display = badge.isBranch
                                            ? badge.text
                                            : tagBadgeText(badge.text);
                badgeRight += badgeWidthForText(display) + kBadgeGap;
            }
            multiMetadataX = qMax(multiMetadataX, badgeRight);
        }
        multiMetadataX += kScenePadding;
    }
    constexpr qreal kMultiSubjectWidth = 240;

    const qreal badgeStartX = graphX + 14;
    const auto badgeRowsFor = [&](const QList<MetadataBadge> &badges) {
        if (!linearHistory || badges.isEmpty()) {
            return badges.isEmpty() ? 0 : 1;
        }
        int rows = 1;
        qreal cursor = badgeStartX;
        for (const MetadataBadge &badge : badges) {
            const QString display = badge.isBranch ? badge.text
                                                    : tagBadgeText(badge.text);
            const qreal badgeWidth = badgeWidthForText(display);
            if (cursor > badgeStartX && cursor + badgeWidth > timelineRight) {
                ++rows;
                cursor = badgeStartX;
            }
            cursor += badgeWidth + kBadgeGap;
        }
        return rows;
    };
    if (linearHistory) {
        qreal nextNodeY = kTimelineTopPadding + kNodeRadius;
        for (Placed &placedCommit : placed) {
            placedCommit.badgeRows = badgeRowsFor(
                badgesByHash.value(placedCommit.hash));
            const qreal badgeStack = placedCommit.badgeRows == 0
                                         ? 0
                                         : placedCommit.badgeRows * kBadgeHeight
                                               + (placedCommit.badgeRows - 1)
                                                     * kBadgeGap;
            placedCommit.y = nextNodeY + badgeStack
                              + (placedCommit.badgeRows == 0
                                     ? 0
                                     : kBadgeToNodeGap);
            nextNodeY = placedCommit.y + timelineRowAdvance;
            placedByHash[placedCommit.hash] = placedCommit;
        }
    } else {
        qreal nextNodeY = kMultiTopPadding + kBadgeHeight + kBadgeGap + 5;
        for (Placed &placedCommit : placed) {
            placedCommit.y = nextNodeY;
            nextNodeY += kMultiRowGap;
            placedByHash[placedCommit.hash] = placedCommit;
        }
    }

    const QPen lanePen(QColor("#E2E8F0"), 1.0);
    const QPen headLanePen(QColor("#93C5FD"), 2.0);
    const qreal laneTop = placed.first().y - kNodeRadius;
    const qreal laneBottom = placed.last().y + kNodeRadius;
    for (int lane = 0; lane < usedLanes; ++lane) {
        const qreal x = laneX(lane);
        m_scene->addLine(QLineF(x, laneTop, x, laneBottom),
                         lane == 0 ? headLanePen : lanePen);
    }

    const auto addLogicalEdge = [this](const QLineF &line, const QPen &pen,
                                       const QString &child,
                                       const QString &parent, bool merge) {
        auto *edge = m_scene->addLine(line, pen);
        edge->setData(BranchRelationWidget::ItemKindRole,
                      BranchRelationWidget::GraphEdgeItem);
        edge->setData(0, child);
        edge->setData(1, parent);
        edge->setData(2, merge);
    };
    for (const Placed &placedCommit : placed) {
        const CommitNode *commit = commitsByHash.value(placedCommit.hash, nullptr);
        if (!commit) {
            continue;
        }
        for (const QString &parent : commit->parents) {
            const auto parentPlacement = placedByHash.constFind(parent);
            if (parentPlacement == placedByHash.constEnd()) {
                continue;
            }
            const qreal x1 = laneX(placedCommit.lane);
            const qreal y1 = placedCommit.y;
            const qreal x2 = laneX(parentPlacement->lane);
            const qreal y2 = parentPlacement->y;
            const bool sameLane = placedCommit.lane == parentPlacement->lane;
            const bool mergeEdge = commit->isMerge;
            const qreal middleX = (x1 + x2) / 2;
            const QPen edgePen(sameLane ? QColor("#94A3B8")
                                        : QColor("#A78BFA"),
                               1.2);
            if (sameLane) {
                addLogicalEdge(QLineF(x1, y1, x2, y2), edgePen,
                               placedCommit.hash, parent, mergeEdge);
            } else {
                addLogicalEdge(QLineF(x1, y1, middleX, y1), edgePen,
                               placedCommit.hash, parent, mergeEdge);
                m_scene->addLine(QLineF(middleX, y1, middleX, y2), edgePen);
                m_scene->addLine(QLineF(middleX, y2, x2, y2), edgePen);
            }
            if (mergeEdge) {
                m_scene->addEllipse(middleX - 2, y1 - 2, 4, 4,
                                    QPen(QColor("#7C3AED")),
                                    QBrush(QColor("#7C3AED")));
            }
        }
    }

    const auto appendBadge = [&](const MetadataBadge &badge, qreal x,
                                  qreal y) -> qreal {
        if (badge.isBranch) {
            QColor bg;
            QColor fg;
            QColor border;
            if (badge.text == m_currentBranch) {
                bg = QColor("#DBEAFE");
                fg = QColor("#1D4ED8");
                border = QColor("#60A5FA");
            } else if (badge.isRemote) {
                bg = QColor("#F5F3FF");
                fg = QColor("#6D28D9");
                border = QColor("#DDD6FE");
            } else {
                bool merged = false;
                for (const BranchNode &branch : m_nodes) {
                    if (branch.name == badge.text && !branch.isRemote) {
                        merged = branch.merged;
                        break;
                    }
                }
                bg = merged ? QColor("#F0FDF4") : QColor("#EFF6FF");
                fg = merged ? QColor("#15803D") : QColor("#1E40AF");
                border = merged ? QColor("#BBF7D0") : QColor("#BFDBFE");
            }
            auto *branchBadge = new RelationBranchTagItem(
                x, y, badge.text, bg, fg, border, this);
            branchBadge->setRemote(badge.isRemote);
            m_scene->addItem(branchBadge);
            return branchBadge->rect().width();
        }
        auto *tagBadge = new RelationTagBadgeItem(x, y, badge.text);
        m_scene->addItem(tagBadge);
        return tagBadge->rect().width();
    };

    for (const Placed &placedCommit : placed) {
        const CommitNode *commit = commitsByHash.value(placedCommit.hash, nullptr);
        if (!commit) {
            continue;
        }
        const qreal nodeX = laneX(placedCommit.lane);
        const qreal hashOffset = linearHistory
                                     ? linearHashOffset
                                     : multiMetadataX - nodeX;
        const qreal messageOffset = hashOffset + hashWidth + 16;
        const qreal messageWidth = linearHistory ? linearMessageWidth
                                                  : kMultiSubjectWidth;
        auto *node = new RelationCommitNodeItem(
            nodeX, placedCommit.y, *commit, commit->isHead, true,
            linearHistory, hashOffset,
            messageOffset, messageWidth, this);
        m_scene->addItem(node);

        const QList<MetadataBadge> badges = badgesByHash.value(placedCommit.hash);
        if (badges.isEmpty()) {
            continue;
        }
        if (!linearHistory) {
            qreal badgeX = nodeX + 14;
            const qreal badgeY = placedCommit.y - kBadgeHeight - kBadgeGap;
            for (const MetadataBadge &badge : badges) {
                const qreal width = appendBadge(badge, badgeX, badgeY);
                badgeX += width + kBadgeGap;
            }
            continue;
        }

        const qreal badgeStack = placedCommit.badgeRows * kBadgeHeight
                                  + qMax(0, placedCommit.badgeRows - 1)
                                        * kBadgeGap;
        qreal badgeY = placedCommit.y - kNodeRadius - kBadgeToNodeGap
                        - badgeStack;
        qreal badgeX = badgeStartX;
        int row = 0;
        for (const MetadataBadge &badge : badges) {
            const QString display = badge.isBranch ? badge.text
                                                    : tagBadgeText(badge.text);
            const qreal expectedWidth = badgeWidthForText(display);
            if (badgeX > badgeStartX
                && badgeX + expectedWidth > timelineRight) {
                ++row;
                badgeX = badgeStartX;
            }
            const qreal width = appendBadge(
                badge, badgeX, badgeY + row * (kBadgeHeight + kBadgeGap));
            badgeX += width + kBadgeGap;
        }
    }

    const QRectF content = m_scene->itemsBoundingRect();
    if (linearHistory) {
        const qreal sceneWidth = qMax(viewportWidth, timelineRight + rightPadding);
        const qreal sceneHeight = qMax(content.bottom() + kScenePadding,
                                        placed.last().y + timelineRowAdvance / 2);
        m_scene->setSceneRect(0, 0, sceneWidth, sceneHeight);
        m_view->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    } else if (!content.isEmpty()) {
        m_scene->setSceneRect(content.adjusted(-kScenePadding, -kScenePadding,
                                               kScenePadding, kScenePadding));
        m_view->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }

    if (preserveSelection) {
        bool restored = false;
        if (!selectedBranchBefore.isEmpty()) {
            for (QGraphicsItem *item : m_scene->items()) {
                if (item->type() == RelationBranchTagItem::Type
                    && item->data(0).toString() == selectedBranchBefore
                    && item->data(1).toBool() == selectedBranchRemoteBefore
                    && item->flags().testFlag(QGraphicsItem::ItemIsSelectable)) {
                    item->setSelected(true);
                    restored = true;
                    break;
                }
            }
        } else if (!selectedBefore.isEmpty()) {
            for (QGraphicsItem *item : m_scene->items()) {
                if (item->data(0).toString() == selectedBefore
                    && item->flags().testFlag(QGraphicsItem::ItemIsSelectable)) {
                    item->setSelected(true);
                    restored = true;
                    break;
                }
            }
        }
        if (!restored) {
            m_detailLabel->setText(detailBefore);
        }
    }
    emit graphLayoutRebuilt();
}

void BranchRelationWidget::showContextMenu(const QString &name,
                                           const QPoint &globalPos,
                                           bool remoteIdentity) {
    QMenu menu(this);
    // v4-P6-21R4 P1-04: a REMOTE branch label must never offer
    // destructive local actions - the operation context is bound to the
    // label's own identity, and deleting/merging/rebase on a remote ref
    // is not a supported safe operation.
    // v4-P6-21R5: remote-ness comes from the TYPED ref classification
    // (refs/remotes/... loaded by loadBranches), never from a display
    // text prefix - upstream/company/fork123 all classify correctly.
    bool isRemote = name.startsWith(QStringLiteral("refs/remotes/"));
    for (const BranchNode &n : m_nodes) {
        if (n.name == name) {
            isRemote = n.isRemote;
            break;
        }
    }
    // The clicked item's typed ref identity is authoritative. This also
    // handles a local branch and a remote-tracking ref with the same display
    // name without falling back to whichever BranchNode happened to appear
    // first.
    isRemote = remoteIdentity;
    QAction *switchAction = nullptr;
    QAction *deleteAction = nullptr;
    QAction *mergeAction = nullptr;
    QAction *rebaseAction = nullptr;
    if (isRemote) {
        menu.addAction(QStringLiteral("远程分支：只读，请先创建本地跟踪分支"))
            ->setEnabled(false);
    } else {
        switchAction = menu.addAction(QStringLiteral("切换分支"));
        deleteAction =
            menu.addAction(QStringLiteral("删除分支"));
        mergeAction =
            menu.addAction(QStringLiteral("合并到当前分支"));
        rebaseAction =
            menu.addAction(QStringLiteral("将当前分支 Rebase 到此分支"));
    }
    QAction *chosen = menu.exec(globalPos);
    if (switchAction && chosen == switchAction) {
        emit switchBranchRequested(name);
    } else if (deleteAction && chosen == deleteAction) {
        emit deleteBranchRequested(name);
    } else if (mergeAction && chosen == mergeAction) {
        emit mergeBranchRequested(name);
    } else if (rebaseAction && chosen == rebaseAction) {
        emit rebaseBranchRequested(name);
    }
}
