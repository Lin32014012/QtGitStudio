#include "BranchTreeWidget.h"

#include <QApplication>
#include <QHash>
#include <QPainter>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QTreeWidgetItemIterator>

namespace {
class BranchTreeDelegate : public QStyledItemDelegate {
public:
    explicit BranchTreeDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent) {}

    void paint(
        QPainter *painter,
        const QStyleOptionViewItem &option,
        const QModelIndex &index) const override {
        QStyleOptionViewItem itemOption(option);
        initStyleOption(&itemOption, index);

        const bool current = index.data(BranchTreeWidget::CurrentBranchRole).toBool();
        if (current) {
            const QString tag = QStringLiteral("当前");
            const int tagWidth = itemOption.fontMetrics.horizontalAdvance(tag) + 18;
            const int decorationWidth = itemOption.decorationSize.width() + 8;
            const int textWidth = qMax(0, itemOption.rect.width() - tagWidth - decorationWidth - 18);
            itemOption.text = itemOption.fontMetrics.elidedText(
                itemOption.text,
                Qt::ElideRight,
                textWidth);

            QStyle *style = itemOption.widget
                ? itemOption.widget->style()
                : QApplication::style();
            style->drawControl(QStyle::CE_ItemViewItem, &itemOption, painter, itemOption.widget);

            const QRect tagRect(
                itemOption.rect.right() - tagWidth - 10,
                itemOption.rect.center().y() - 11,
                tagWidth,
                22);
            painter->save();
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor("#DBEAFE"));
            painter->drawRoundedRect(tagRect, 7, 7);
            painter->setPen(QColor("#1D4ED8"));
            painter->drawText(tagRect, Qt::AlignCenter, tag);
            painter->restore();
            return;
        }

        QStyledItemDelegate::paint(painter, itemOption, index);
    }

    QSize sizeHint(
        const QStyleOptionViewItem &option,
        const QModelIndex &index) const override {
        QSize hint = QStyledItemDelegate::sizeHint(option, index);
        hint.setHeight(36);
        return hint;
    }
};

QIcon branchIcon(bool current) {
    QPixmap pixmap(14, 14);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(current ? "#2563EB" : "#6B7280"), 1.6));
    painter.setBrush(current ? QBrush(QColor("#2563EB")) : Qt::NoBrush);
    painter.drawEllipse(QRectF(2.5, 2.5, 9, 9));
    return QIcon(pixmap);
}

QString directoryKey(const QStringList &parts, int lastIndex) {
    return parts.mid(0, lastIndex + 1).join('/');
}
}

BranchTreeWidget::BranchTreeWidget(QWidget *parent)
    : QTreeWidget(parent) {
    setColumnCount(1);
    setHeaderHidden(true);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setRootIsDecorated(true);
    setIndentation(20);
    setTextElideMode(Qt::ElideRight);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setUniformRowHeights(true);
    setItemDelegate(new BranchTreeDelegate(this));
}

void BranchTreeWidget::setBranchData(
    const QStringList &branches,
    const QString &currentBranch,
    const QString &preferredSelection) {
    const QString previousSelection = selectedBranch();
    const QSet<QString> expandedPaths = expandedDirectoryPaths();
    m_branches = branches;
    m_branches.removeDuplicates();
    m_branches.sort(Qt::CaseInsensitive);
    m_currentBranch = currentBranch;

    const QString selection = preferredSelection.isEmpty()
        ? previousSelection
        : preferredSelection;
    rebuild(selection, expandedPaths);
}

void BranchTreeWidget::setCurrentBranch(
    const QString &currentBranch,
    const QString &branchToReveal) {
    m_currentBranch = currentBranch;
    QTreeWidgetItemIterator iterator(this);
    while (*iterator) {
        QTreeWidgetItem *item = *iterator;
        if (item->data(0, NodeKindRole).toInt() == static_cast<int>(NodeKind::Branch)) {
            applyBranchAppearance(
                item,
                item->data(0, BranchNameRole).toString() == currentBranch);
        }
        ++iterator;
    }

    const QString reveal = branchToReveal.isEmpty() ? currentBranch : branchToReveal;
    if (QTreeWidgetItem *item = branchItem(reveal)) {
        expandParents(item);
        scrollToItem(item, QAbstractItemView::PositionAtCenter);
    }
}

void BranchTreeWidget::setFilterText(const QString &filterText) {
    const QString normalized = filterText.trimmed();
    if (normalized == m_filterText) {
        return;
    }

    const QString previousSelection = selectedBranch();
    if (m_filterText.isEmpty() && !normalized.isEmpty()) {
        m_expandedBeforeSearch = expandedDirectoryPaths();
    }
    m_filterText = normalized;
    const QSet<QString> expanded = m_filterText.isEmpty()
        ? m_expandedBeforeSearch
        : QSet<QString>();
    rebuild(previousSelection, expanded);
}

QString BranchTreeWidget::selectedBranch() const {
    QTreeWidgetItem *item = currentItem();
    if (!item || item->data(0, NodeKindRole).toInt() != static_cast<int>(NodeKind::Branch)) {
        return {};
    }
    return item->data(0, BranchNameRole).toString();
}

QTreeWidgetItem *BranchTreeWidget::branchItem(const QString &branchName) const {
    QTreeWidgetItemIterator iterator(const_cast<BranchTreeWidget *>(this));
    while (*iterator) {
        QTreeWidgetItem *item = *iterator;
        if (item->data(0, BranchNameRole).toString() == branchName) {
            return item;
        }
        ++iterator;
    }
    return nullptr;
}

QSet<QString> BranchTreeWidget::expandedDirectoryPaths() const {
    QSet<QString> paths;
    QTreeWidgetItemIterator iterator(const_cast<BranchTreeWidget *>(this));
    while (*iterator) {
        QTreeWidgetItem *item = *iterator;
        const NodeKind kind = static_cast<NodeKind>(item->data(0, NodeKindRole).toInt());
        if ((kind == NodeKind::Root || kind == NodeKind::Directory) && item->isExpanded()) {
            paths.insert(item->data(0, DirectoryPathRole).toString());
        }
        ++iterator;
    }
    return paths;
}

void BranchTreeWidget::rebuild(
    const QString &preferredSelection,
    const QSet<QString> &expandedPaths) {
    const QSignalBlocker blocker(this);
    clear();

    QStringList visibleBranches;
    for (const QString &branch : m_branches) {
        if (m_filterText.isEmpty() || branch.contains(m_filterText, Qt::CaseInsensitive)) {
            visibleBranches.append(branch);
        }
    }

    auto *root = new QTreeWidgetItem(this);
    root->setText(0, QStringLiteral("本地分支 %1").arg(visibleBranches.size()));
    root->setData(0, NodeKindRole, static_cast<int>(NodeKind::Root));
    root->setData(0, DirectoryPathRole, QString());
    root->setData(0, BranchCountRole, visibleBranches.size());
    root->setFlags(root->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsEditable));
    root->setForeground(0, QColor("#374151"));
    QFont rootFont = root->font(0);
    rootFont.setBold(true);
    root->setFont(0, rootFont);

    QHash<QString, int> directoryCounts;
    for (const QString &branch : visibleBranches) {
        const QStringList parts = branch.split('/');
        for (int index = 0; index + 1 < parts.size(); ++index) {
            ++directoryCounts[directoryKey(parts, index)];
        }
    }

    QHash<QString, QTreeWidgetItem *> directories;
    QTreeWidgetItem *selectedItem = nullptr;
    for (const QString &branch : visibleBranches) {
        const QStringList parts = branch.split('/');
        QTreeWidgetItem *parent = root;
        for (int index = 0; index + 1 < parts.size(); ++index) {
            const QString path = directoryKey(parts, index);
            QTreeWidgetItem *directory = directories.value(path);
            if (!directory) {
                directory = new QTreeWidgetItem(parent);
                directory->setText(
                    0,
                    QStringLiteral("%1  %2").arg(parts.at(index)).arg(directoryCounts.value(path)));
                directory->setData(0, NodeKindRole, static_cast<int>(NodeKind::Directory));
                directory->setData(0, DirectoryPathRole, path);
                directory->setData(0, BranchCountRole, directoryCounts.value(path));
                directory->setFlags(
                    directory->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsEditable));
                directory->setForeground(0, QColor("#374151"));
                QFont font = directory->font(0);
                font.setBold(true);
                directory->setFont(0, font);
                directory->setToolTip(0, path);
                directories.insert(path, directory);
            }
            parent = directory;
        }

        auto *item = new QTreeWidgetItem(parent);
        item->setText(0, parts.constLast());
        item->setData(0, BranchNameRole, branch);
        item->setData(0, NodeKindRole, static_cast<int>(NodeKind::Branch));
        item->setToolTip(0, branch);
        item->setFlags((item->flags() | Qt::ItemIsSelectable) & ~Qt::ItemIsEditable);
        applyBranchAppearance(item, branch == m_currentBranch);
        if (branch == preferredSelection) {
            selectedItem = item;
        }
    }

    root->setExpanded(true);
    if (m_filterText.isEmpty()) {
        for (auto iterator = directories.cbegin(); iterator != directories.cend(); ++iterator) {
            iterator.value()->setExpanded(expandedPaths.contains(iterator.key()));
        }
    } else {
        expandAll();
    }

    if (QTreeWidgetItem *current = branchItem(m_currentBranch)) {
        expandParents(current);
    }
    if (selectedItem) {
        expandParents(selectedItem);
        setCurrentItem(selectedItem);
        scrollToItem(selectedItem, QAbstractItemView::PositionAtCenter);
    } else if (QTreeWidgetItem *current = branchItem(m_currentBranch)) {
        setCurrentItem(current);
        scrollToItem(current, QAbstractItemView::PositionAtCenter);
    }
}

void BranchTreeWidget::expandParents(QTreeWidgetItem *item) {
    for (QTreeWidgetItem *parent = item ? item->parent() : nullptr;
         parent;
         parent = parent->parent()) {
        parent->setExpanded(true);
    }
}

void BranchTreeWidget::applyBranchAppearance(QTreeWidgetItem *item, bool current) {
    item->setData(0, CurrentBranchRole, current);
    item->setIcon(0, branchIcon(current));
    item->setForeground(0, QColor(current ? "#1D4ED8" : "#111827"));
    item->setBackground(0, current ? QBrush(QColor("#EFF6FF")) : QBrush(Qt::NoBrush));
    QFont font = item->font(0);
    font.setBold(current);
    item->setFont(0, font);
}
