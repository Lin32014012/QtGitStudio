#include "PracticeDialog.h"

#include "PracticeService.h"
#include "Theme.h"

#include <QDateTime>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace {

const char *kPassedKey = "practices/passed/";

QString statusPrefix(bool passed) {
    return passed ? QStringLiteral("\u2713  ") : QStringLiteral("\u25CB  ");
}

}  // namespace

PracticeDialog::PracticeDialog(QWidget *parent) : QDialog(parent) {
    setObjectName(QStringLiteral("practiceDialog"));
    setWindowTitle(QStringLiteral("动手练习"));
    setMinimumSize(880, 560);

    m_sandboxRoot = QDir(QStandardPaths::writableLocation(
                             QStandardPaths::AppLocalDataLocation))
                        .absoluteFilePath(QStringLiteral("practices"));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 14, 16, 14);
    outer->setSpacing(10);

    auto *heading = new QLabel(QStringLiteral("动手练习"), this);
    heading->setProperty("role", QStringLiteral("pageTitle"));
    outer->addWidget(heading);

    auto *intro = new QLabel(
        QStringLiteral("每关会在本机生成一个可以随便折腾的练习仓库，"
                       "做完回到这里点「检查完成」即可。"),
        this);
    intro->setProperty("role", QStringLiteral("bodyText"));
    intro->setWordWrap(true);
    outer->addWidget(intro);

    auto *splitter = new QSplitter(Qt::Horizontal, this);

    m_list = new QTreeWidget(splitter);
    m_list->setObjectName(QStringLiteral("practiceList"));
    m_list->setHeaderHidden(true);
    m_list->setRootIsDecorated(true);
    m_list->setMinimumWidth(240);
    splitter->addWidget(m_list);

    auto *right = new QWidget(splitter);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(14, 0, 0, 0);
    rightLayout->setSpacing(9);

    m_title = new QLabel(right);
    m_title->setObjectName(QStringLiteral("practiceTitle"));
    m_title->setProperty("role", QStringLiteral("cardTitle"));
    rightLayout->addWidget(m_title);

    m_goal = new QLabel(right);
    m_goal->setObjectName(QStringLiteral("practiceGoal"));
    m_goal->setProperty("role", QStringLiteral("bodyText"));
    m_goal->setWordWrap(true);
    rightLayout->addWidget(m_goal);

    m_hintButton = new QPushButton(QStringLiteral("显示提示"), right);
    m_hintButton->setProperty("role", QStringLiteral("ghost"));
    m_hintButton->setCursor(Qt::PointingHandCursor);
    rightLayout->addWidget(m_hintButton, 0, Qt::AlignLeft);

    m_hint = new QLabel(right);
    m_hint->setProperty("role", QStringLiteral("hint"));
    m_hint->setWordWrap(true);
    m_hint->hide();
    rightLayout->addWidget(m_hint);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(8);
    m_startButton = new QPushButton(QStringLiteral("开始练习"), right);
    m_startButton->setProperty("role", QStringLiteral("primary"));
    m_startButton->setCursor(Qt::PointingHandCursor);
    m_checkButton = new QPushButton(QStringLiteral("检查完成"), right);
    m_checkButton->setProperty("role", QStringLiteral("secondary"));
    m_checkButton->setCursor(Qt::PointingHandCursor);
    buttonRow->addWidget(m_startButton);
    buttonRow->addWidget(m_checkButton);
    buttonRow->addStretch(1);
    rightLayout->addLayout(buttonRow);

    m_result = new QLabel(right);
    m_result->setObjectName(QStringLiteral("practiceResult"));
    m_result->setProperty("role", QStringLiteral("hint"));
    m_result->setWordWrap(true);
    m_result->hide();
    rightLayout->addWidget(m_result);

    rightLayout->addStretch(1);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    outer->addWidget(splitter, 1);

    connect(m_list, &QTreeWidget::itemSelectionChanged,
            this, &PracticeDialog::updateSelectionUi);
    connect(m_hintButton, &QPushButton::clicked,
            this, &PracticeDialog::revealHint);
    connect(m_startButton, &QPushButton::clicked,
            this, &PracticeDialog::startPractice);
    connect(m_checkButton, &QPushButton::clicked,
            this, &PracticeDialog::checkPractice);

    buildList();
    updateSelectionUi();
}

void PracticeDialog::setSandboxRoot(const QString &root) {
    m_sandboxRoot = root;
}

QString PracticeDialog::sandboxRoot() const {
    return m_sandboxRoot;
}

void PracticeDialog::buildList() {
    m_list->clear();
    QHash<QString, QTreeWidgetItem *> chapters;
    for (const PracticeDefinition &item : PracticeService::catalog()) {
        QTreeWidgetItem *chapter = chapters.value(item.chapter);
        if (!chapter) {
            chapter = new QTreeWidgetItem(m_list);
            chapter->setText(0, item.chapter);
            chapter->setFlags(Qt::ItemIsEnabled);
            chapters.insert(item.chapter, chapter);
        }
        auto *row = new QTreeWidgetItem(chapter);
        row->setText(0, statusPrefix(isPassed(item.id)) + item.title);
        row->setData(0, Qt::UserRole, item.id);
        row->setToolTip(0, item.goal);
    }
    m_list->expandAll();
}

QTreeWidgetItem *PracticeDialog::itemFor(const QString &id) const {
    for (int c = 0; c < m_list->topLevelItemCount(); ++c) {
        QTreeWidgetItem *chapter = m_list->topLevelItem(c);
        for (int r = 0; r < chapter->childCount(); ++r) {
            QTreeWidgetItem *row = chapter->child(r);
            if (row->data(0, Qt::UserRole).toString() == id) {
                return row;
            }
        }
    }
    return nullptr;
}

bool PracticeDialog::selectPractice(const QString &id) {
    QTreeWidgetItem *row = itemFor(id);
    if (!row) {
        return false;
    }
    m_list->setCurrentItem(row);
    return true;
}

void PracticeDialog::reloadItemStates() {
    const QString keep = m_selectedId;
    buildList();
    if (!keep.isEmpty()) {
        selectPractice(keep);
    }
}

bool PracticeDialog::isPassed(const QString &id) const {
    QSettings settings;
    return settings.value(QString::fromLatin1(kPassedKey) + id, false).toBool();
}

void PracticeDialog::setPassed(const QString &id, bool passed) {
    QSettings settings;
    if (passed) {
        settings.setValue(QString::fromLatin1(kPassedKey) + id, true);
    } else {
        settings.remove(QString::fromLatin1(kPassedKey) + id);
    }
}

void PracticeDialog::updateSelectionUi() {
    QTreeWidgetItem *current = m_list->currentItem();
    if (!current || current->childCount() > 0 ||
        current->data(0, Qt::UserRole).toString().isEmpty()) {
        // A chapter node (or nothing) is selected: nothing to do.
        m_selectedId.clear();
        m_title->clear();
        m_goal->setText(QStringLiteral("从左侧选择一关开始。"));
        m_hint->hide();
        m_hintButton->hide();
        m_startButton->setEnabled(false);
        m_checkButton->setEnabled(false);
        return;
    }

    const QString newId = current->data(0, Qt::UserRole).toString();
    // Rebuilding the list (after a pass) re-selects the same row, which
    // fires this slot again. Only a genuine change of exercise should throw
    // away the sandbox the student already has open.
    const bool changed = (newId != m_selectedId);
    m_selectedId = newId;
    const PracticeDefinition definition = PracticeService::definition(m_selectedId);
    m_title->setText(QStringLiteral("%1 · %2").arg(definition.chapter, definition.title));
    m_goal->setText(definition.goal);
    m_hint->setText(QStringLiteral("提示：%1").arg(definition.hint));
    m_hintVisible = false;
    m_hint->hide();
    m_hintButton->setText(QStringLiteral("显示提示"));
    m_hintButton->show();
    m_startButton->setEnabled(true);
    m_checkButton->setEnabled(true);

    if (changed) {
        // A different exercise invalidates the previously opened sandbox.
        m_activeRepoPath.clear();
        m_result->hide();
    }
}

void PracticeDialog::revealHint() {
    m_hintVisible = !m_hintVisible;
    m_hint->setVisible(m_hintVisible);
    m_hintButton->setText(m_hintVisible ? QStringLiteral("隐藏提示")
                                        : QStringLiteral("显示提示"));
}

void PracticeDialog::startPractice() {
    if (m_selectedId.isEmpty()) {
        return;
    }
    QString repoPath;
    QString error;
    if (!PracticeService::generate(m_selectedId, m_sandboxRoot, &repoPath, &error)) {
        m_result->setText(QStringLiteral("无法创建练习仓库：%1").arg(error));
        m_result->setProperty("state", QStringLiteral("warn"));
        Theme::restyle(m_result);
        m_result->show();
        return;
    }
    m_activeRepoPath = repoPath;
    m_result->setText(QStringLiteral("练习仓库已就绪：%1\n在主窗口里操作完成后，回到这里点「检查完成」。")
                          .arg(repoPath));
    m_result->setProperty("state", QStringLiteral("info"));
    Theme::restyle(m_result);
    m_result->show();
    emit repositoryRequested(repoPath);
}

void PracticeDialog::checkPractice() {
    if (m_selectedId.isEmpty()) {
        return;
    }
    if (m_activeRepoPath.isEmpty()) {
        m_result->setText(QStringLiteral("还没开始这一关。先点「开始练习」生成练习仓库。"));
        m_result->setProperty("state", QStringLiteral("warn"));
        Theme::restyle(m_result);
        m_result->show();
        return;
    }

    const PracticeCheck check = PracticeService::check(m_selectedId, m_activeRepoPath);
    m_result->setText(check.detail);
    m_result->setProperty("state", check.passed ? QStringLiteral("ok")
                                               : QStringLiteral("warn"));
    Theme::restyle(m_result);
    m_result->show();

    if (check.passed != isPassed(m_selectedId)) {
        setPassed(m_selectedId, check.passed);
        reloadItemStates();
    }
}
