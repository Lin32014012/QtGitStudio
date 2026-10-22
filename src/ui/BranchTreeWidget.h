#pragma once

#include <QSet>
#include <QTreeWidget>

class BranchTreeWidget : public QTreeWidget {
    Q_OBJECT

public:
    enum class NodeKind {
        Root,
        Directory,
        Branch,
    };

    static constexpr int BranchNameRole = Qt::UserRole;
    static constexpr int NodeKindRole = Qt::UserRole + 1;
    static constexpr int DirectoryPathRole = Qt::UserRole + 2;
    static constexpr int BranchCountRole = Qt::UserRole + 3;
    static constexpr int CurrentBranchRole = Qt::UserRole + 4;

    explicit BranchTreeWidget(QWidget *parent = nullptr);

    void setBranchData(
        const QStringList &branches,
        const QString &currentBranch,
        const QString &preferredSelection = QString());
    void setCurrentBranch(
        const QString &currentBranch,
        const QString &branchToReveal = QString());
    void setFilterText(const QString &filterText);

    QString selectedBranch() const;
    QTreeWidgetItem *branchItem(const QString &branchName) const;
    QSet<QString> expandedDirectoryPaths() const;
    QString filterText() const { return m_filterText; }

private:
    void rebuild(const QString &preferredSelection, const QSet<QString> &expandedPaths);
    void expandParents(QTreeWidgetItem *item);
    void applyBranchAppearance(QTreeWidgetItem *item, bool current);

    QStringList m_branches;
    QString m_currentBranch;
    QString m_filterText;
    QSet<QString> m_expandedBeforeSearch;
};
