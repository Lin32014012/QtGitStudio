#include "CommitHistoryWidget.h"
#include "Theme.h"
#include "GitCommitGraphService.h"
#include "GitCommitDiffService.h"
#include "DiffViewerWidget.h"
#include "DiffPreviewDialog.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFontDatabase>
#include <QDateTime>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QStyle>
#include <QIcon>

CommitHistoryWidget::CommitHistoryWidget(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    // Tool row: refresh / search / copy actions.
    auto *toolRow = new QHBoxLayout();
    auto *refreshButton = new QPushButton(QStringLiteral("刷新"), this);
    refreshButton->setObjectName("historyRefreshButton");
    refreshButton->setProperty("role", QStringLiteral("secondary"));
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName("historySearch");
    m_searchEdit->setPlaceholderText(
        QStringLiteral("搜索提交：标题 / Hash / 作者…"));
    m_searchEdit->setClearButtonEnabled(true);
    auto *copyHashButton = new QPushButton(QStringLiteral("复制 Hash"), this);
    copyHashButton->setObjectName("historyCopyHash");
    auto *copyMsgButton =
        new QPushButton(QStringLiteral("复制提交信息"), this);
    copyMsgButton->setObjectName("historyCopyMessage");
    for (QPushButton *b : {copyHashButton, copyMsgButton}) {
        b->setProperty("role", QStringLiteral("ghost"));
    }
    toolRow->addWidget(refreshButton);
    toolRow->addWidget(m_searchEdit, 1);
    toolRow->addWidget(copyHashButton);
    toolRow->addWidget(copyMsgButton);
    layout->addLayout(toolRow);

    // v4-P6-15: vertical splitter - list 35% / detail+files 25% /
    // diff preview 40%.
    // v4-P6-20: list gets more room (45/25/30) and a usable minimum
    // height so the history is never squeezed to a single row.
    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->setObjectName("historySplitter");

    // Upper: commit list.
    m_commitTree = new QTreeWidget(splitter);
    m_commitTree->setObjectName("historyCommitTree");
    m_commitTree->setColumnCount(5);
    m_commitTree->setHeaderLabels(
        {QStringLiteral("Hash"), QStringLiteral("标记"),
         QStringLiteral("提交说明"), QStringLiteral("作者"),
         QStringLiteral("时间")});
    // v4-P6-20: column sizing - Hash fixed 90, badge fixed 80, message
    // STRETCHES (gets all remaining space), author/time interactive
    // with sane defaults. Never all-fixed (message would be crushed).
    QHeaderView *historyHeader = m_commitTree->header();
    historyHeader->setSectionResizeMode(0, QHeaderView::Fixed);
    historyHeader->setSectionResizeMode(1, QHeaderView::Fixed);
    historyHeader->setSectionResizeMode(2, QHeaderView::Stretch);
    historyHeader->setSectionResizeMode(3, QHeaderView::Interactive);
    historyHeader->setSectionResizeMode(4, QHeaderView::Interactive);
    m_commitTree->setColumnWidth(0, 90);
    m_commitTree->setColumnWidth(1, 80);
    m_commitTree->setColumnWidth(3, 120);
    m_commitTree->setColumnWidth(4, 150);
    m_commitTree->setRootIsDecorated(false);
    m_commitTree->setAlternatingRowColors(true);
    m_commitTree->setUniformRowHeights(true);
    m_commitTree->setMinimumHeight(220);  // v4-P6-20: history must stay readable
    // v4-P6-16: soft light-blue selection (was a deep blue that hurt
    // readability); distinct hover / selected / normal states.
    splitter->addWidget(m_commitTree);

    // Middle: detail + changed files.
    auto *middle = new QWidget(splitter);
    auto *middleLayout = new QVBoxLayout(middle);
    middleLayout->setContentsMargins(0, 0, 0, 0);
    middleLayout->setSpacing(4);
    m_detailLabel = new QLabel(QStringLiteral("选择提交查看详情"), middle);
    m_detailLabel->setObjectName("historyDetail");
    m_detailLabel->setWordWrap(true);
    m_detailLabel->setProperty("role", QStringLiteral("monoBox"));
    middleLayout->addWidget(m_detailLabel, 1);
    m_fileList = new QListWidget(middle);
    m_fileList->setObjectName("historyFileList");
    middleLayout->addWidget(m_fileList, 1);
    splitter->addWidget(middle);

    // Lower: diff preview with enlarge button.
    auto *lower = new QWidget(splitter);
    auto *lowerLayout = new QVBoxLayout(lower);
    lowerLayout->setContentsMargins(0, 0, 0, 0);
    lowerLayout->setSpacing(4);
    auto *diffHeader = new QHBoxLayout();
    auto *diffTitle = new QLabel(QStringLiteral("Diff 预览"), lower);
    diffTitle->setProperty("role", QStringLiteral("panelLabel"));
    m_enlargeButton =
        new QPushButton(QStringLiteral("放大预览"), lower);
    m_enlargeButton->setObjectName("historyEnlargeDiff");
    m_enlargeButton->setEnabled(false);
    m_enlargeButton->setProperty("role", QStringLiteral("primary"));
    diffHeader->addWidget(diffTitle);
    diffHeader->addStretch();
    diffHeader->addWidget(m_enlargeButton);
    lowerLayout->addLayout(diffHeader);
    m_diffViewer = new DiffViewerWidget(lower);
    m_diffViewer->setObjectName("historyDiffViewer");
    lowerLayout->addWidget(m_diffViewer, 1);
    splitter->addWidget(lower);

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 2);
    layout->addWidget(splitter, 1);

    connect(refreshButton, &QPushButton::clicked,
            this, &CommitHistoryWidget::refresh);
    connect(m_searchEdit, &QLineEdit::textChanged, this,
            [this](const QString &) {
                loadCommitList();
                selectCommitByHash(m_currentHash);
            });
    connect(m_commitTree, &QTreeWidget::itemSelectionChanged,
            this, &CommitHistoryWidget::onCommitSelected);
    connect(m_fileList, &QListWidget::itemSelectionChanged, this,
            [this]() { onFileClicked(m_fileList->currentRow()); });
    connect(m_enlargeButton, &QPushButton::clicked,
            this, &CommitHistoryWidget::openFullDiff);
    connect(copyHashButton, &QPushButton::clicked, this, [this]() {
        const QString hash = selectedCommitHash();
        if (!hash.isEmpty()) {
            QApplication::clipboard()->setText(hash);
        }
    });
    connect(copyMsgButton, &QPushButton::clicked, this, [this]() {
        if (m_currentHash.isEmpty()) {
            return;
        }
        const auto result =
            GitCommitGraphService().load(m_repositoryPath);
        for (const CommitNode &c : result.commits) {
            if (c.hash == m_currentHash) {
                QApplication::clipboard()->setText(c.message);
                break;
            }
        }
    });
}

int CommitHistoryWidget::rowCount() const {
    return m_commitTree->topLevelItemCount();
}

QString CommitHistoryWidget::selectedCommitHash() const {
    const QList<QTreeWidgetItem *> selected =
        m_commitTree->selectedItems();
    if (selected.isEmpty()) {
        return QString();
    }
    return selected.first()->data(0, Qt::UserRole).toString();
}

void CommitHistoryWidget::setRepositoryPath(const QString &path) {
    m_repositoryPath = path;
}

void CommitHistoryWidget::refresh() {
    loadCommitList();
    selectCommitByHash(m_currentHash);
}

void CommitHistoryWidget::loadCommitList() {
    m_commitTree->clear();
    m_fileList->clear();
    m_detailLabel->setText(QStringLiteral("选择提交查看详情"));
    // v4-P6-17: friendly empty-diff placeholder instead of a blank panel.
    m_diffViewer->showMessage(QStringLiteral("请选择文件查看 Diff"));
    m_enlargeButton->setEnabled(false);
    m_currentHash.clear();
    m_currentDiffText.clear();
    m_currentFileName.clear();

    if (m_repositoryPath.isEmpty()) {
        m_detailLabel->setText(QStringLiteral("未设置仓库路径"));
        return;
    }
    const GitCommitGraphService::LoadResult result =
        GitCommitGraphService().load(m_repositoryPath);
    if (!result.success) {
        m_detailLabel->setText(
            QStringLiteral("加载提交历史失败：%1").arg(result.error));
        return;
    }
    const QString query = m_searchEdit->text().trimmed();
    for (const CommitNode &node : result.commits) {
        if (!query.isEmpty()
            && !node.message.contains(query, Qt::CaseInsensitive)
            && !node.hash.contains(query, Qt::CaseInsensitive)
            && !node.author.contains(query, Qt::CaseInsensitive)) {
            continue;
        }
        auto *item = new QTreeWidgetItem(m_commitTree);
        item->setText(0, node.shortHash);
        // v4-P6-17: type badge column - HEAD / TAG / MERGE.
        QStringList badges;
        if (node.isHead) {
            badges << QStringLiteral("HEAD");
        }
        // v4-P6-18: tag badges use [v1.0] brackets so they stand out.
        for (const QString &tag : node.tags) {
            badges << QStringLiteral("[%1]").arg(tag);
        }
        if (node.isMerge) {
            badges << QStringLiteral("MERGE");
        }
        const QString badgeText = badges.join(QStringLiteral(" "));
        item->setText(1, badgeText);
        if (node.isHead) {
            item->setForeground(1, QColor("#1D4ED8"));
        } else if (node.isMerge) {
            item->setForeground(1, QColor("#7C3AED"));
        } else if (!node.tags.isEmpty()) {
            item->setForeground(1, QColor("#15803D"));
        }
        item->setText(2, node.message);  // v4-P6-20: Stretch column; tooltip below carries the full text
        item->setToolTip(0, node.hash);  // v4-P6-20: full cell text in tooltip
        item->setToolTip(1, badgeText);
        item->setToolTip(2, node.message);
        item->setToolTip(3, node.author);
        item->setToolTip(4, node.time.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
        item->setText(3, node.author);
        item->setText(4, node.time.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
        item->setData(0, Qt::UserRole, node.hash);
        if (node.isHead) {
            QFont f = item->font(0);
            f.setBold(true);
            item->setFont(0, f);
        }
    }
    if (m_commitTree->topLevelItemCount() == 0) {
        m_detailLabel->setText(
            QStringLiteral("暂无提交历史\n\n完成第一次 Commit 后，"
                           "这里将显示提交详情和文件变化。"));
    }
}

void CommitHistoryWidget::selectCommitByHash(const QString &hash) {
    for (int i = 0; i < m_commitTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_commitTree->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == hash) {
            m_commitTree->setCurrentItem(item);
            onCommitSelected();
            return;
        }
    }
    // Fallback: HEAD commit (or first row).
    for (int i = 0; i < m_commitTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_commitTree->topLevelItem(i);
        if (item->font(0).bold()) {
            m_commitTree->setCurrentItem(item);
            onCommitSelected();
            return;
        }
    }
    if (m_commitTree->topLevelItemCount() > 0) {
        m_commitTree->setCurrentItem(m_commitTree->topLevelItem(0));
        onCommitSelected();
    }
}

void CommitHistoryWidget::onCommitSelected() {
    const QString hash = selectedCommitHash();
    if (hash.isEmpty()) {
        return;
    }
    m_currentHash = hash;
    m_fileList->clear();
    m_diffViewer->showMessage(QStringLiteral("请选择文件查看 Diff"));
    m_enlargeButton->setEnabled(false);
    m_currentDiffText.clear();
    m_currentFileName.clear();

    const auto graphResult = GitCommitGraphService().load(m_repositoryPath);
    const CommitNode *node = nullptr;
    for (const CommitNode &c : graphResult.commits) {
        if (c.hash == hash) {
            node = &c;
            break;
        }
    }
    if (!node) {
        return;
    }
    QStringList refs;
    if (node->isHead) refs << QStringLiteral("HEAD");
    refs << node->localBranches << node->remoteBranches << node->tags;
    // v4-P6-15: two-column style detail card (not one long line).
    m_detailLabel->setText(
        QStringLiteral("完整 Hash：%1\n提交说明：%2\n作者：%3\n"
                       "时间：%4\n父提交：%5\n分支引用：%6\n标签：%7")
            .arg(node->hash,
                 node->message,
                 node->author,
                 node->time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                 node->parents.isEmpty()
                     ? QStringLiteral("(无)")
                     : node->parents.join(QStringLiteral(", ")),
                 (node->localBranches + node->remoteBranches).isEmpty()
                     ? QStringLiteral("(无)")
                     : (node->localBranches + node->remoteBranches)
                           .join(QStringLiteral(", ")),
                 node->tags.isEmpty() ? QStringLiteral("(无)")
                                      : node->tags.join(QStringLiteral(", "))));

    const GitCommitDiffService::LoadResult diffResult =
        GitCommitDiffService().load(m_repositoryPath, hash);
    if (!diffResult.success) {
        m_fileList->addItem(
            QStringLiteral("读取变更文件失败：%1").arg(diffResult.error));
        return;
    }
    if (diffResult.diff.files.isEmpty()) {
        m_fileList->addItem(QStringLiteral("该提交没有可显示的文件变化"));
        return;
    }
    // v4-P6-17: per-file status (A/M/D) parsed from the raw diff text
    // (the service keeps `files` as plain paths - view-layer only).
    struct FileEntry {
        QString path;
        QString status;
    };
    QList<FileEntry> entries;
    const QStringList blocks =
        diffResult.diff.diffText.split(QStringLiteral("diff --git "));
    for (int i = 1; i < blocks.size(); ++i) {
        const QStringList lines = blocks.at(i).split('\n');
        if (lines.isEmpty()) {
            continue;
        }
        QString path = lines.first().section(' ', 1, 1).mid(2);
        if (path.isEmpty()) {
            path = lines.first().section(' ', 0, 0).mid(2);
        }
        int plus = 0;
        int minus = 0;
        for (const QString &l : lines) {
            if (l.startsWith(QStringLiteral("+++"))
                || l.startsWith(QStringLiteral("---"))
                || l.startsWith(QStringLiteral("@@"))) {
                continue;
            }
            if (l.startsWith(QLatin1Char('+'))) {
                ++plus;
            } else if (l.startsWith(QLatin1Char('-'))) {
                ++minus;
            }
        }
        const QString status =
            (plus > 0 && minus > 0)
                ? QStringLiteral("M")
                : (plus > 0 ? QStringLiteral("A")
                            : (minus > 0 ? QStringLiteral("D")
                                         : QStringLiteral("?")));
        entries.append({path, status});
    }
    const QIcon fileIcon =
        Theme::icon(QStringLiteral("file.svg"), QStyle::SP_FileIcon);
    for (const FileEntry &e : entries) {
        auto *item = new QListWidgetItem(
            fileIcon,
            QStringLiteral("%1  %2").arg(e.status, e.path),
            m_fileList);
        const QColor statusColor =
            e.status == QStringLiteral("A")   ? QColor("#16A34A")
            : e.status == QStringLiteral("D") ? QColor("#DC2626")
            : e.status == QStringLiteral("M") ? QColor("#2563EB")
                                              : QColor("#6B7280");
        item->setForeground(statusColor);
    }
}

void CommitHistoryWidget::onFileClicked(int row) {
    if (row < 0 || m_currentHash.isEmpty()) {
        return;
    }
    const QString raw = m_fileList->item(row)->text();
    if (raw.startsWith(QStringLiteral("该提交没有"))
        || raw.startsWith(QStringLiteral("读取变更文件失败"))) {
        return;
    }
    // v4-P6-17: strip the "X  " status prefix added by the list.
    QString file = raw;
    if (file.size() > 3 && file.at(1) == QLatin1Char(' ')
        && file.at(2) == QLatin1Char(' ')) {
        file = file.mid(3);
    }
    m_currentFileName = file;
    // Reuse the existing diff service; show the whole-commit diff text
    // (the service returns the full diff for the commit).
    const GitCommitDiffService::LoadResult diffResult =
        GitCommitDiffService().load(m_repositoryPath, m_currentHash);
    if (!diffResult.success) {
        m_diffViewer->showMessage(diffResult.error);
        m_enlargeButton->setEnabled(false);
        return;
    }
    m_currentDiffText = diffResult.diff.diffText;
    m_diffViewer->showTextDiff(m_currentDiffText);
    m_enlargeButton->setEnabled(!m_currentDiffText.isEmpty());
}

void CommitHistoryWidget::openFullDiff() {
    if (m_currentDiffText.isEmpty()) {
        return;
    }
    DiffPreviewDialog dialog(m_currentDiffText, m_currentHash,
                             m_currentFileName, this);
    dialog.exec();
}
