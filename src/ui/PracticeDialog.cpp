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

    // Count first so a chapter header can show 已通过/总数 without a second
    // pass over the tree.
    QHash<QString, QPair<int, int>> chapterTally;  // chapter -> (passed, total)
    QStringList chapterOrder;
    for (const PracticeDefinition &item : PracticeService::catalog()) {
        if (!chapterTally.contains(item.chapter)) {
            chapterTally.insert(item.chapter, qMakePair(0, 0));
            chapterOrder.append(item.chapter);
        }
        auto &tally = chapterTally[item.chapter];
        ++tally.second;
        if (isPassed(item.id)) {
            ++tally.first;
        }
    }

    QHash<QString, QTreeWidgetItem *> chapters;
    for (const QString &chapterName : chapterOrder) {
        auto *chapter = new QTreeWidgetItem(m_list);
        const auto tally = chapterTally.value(chapterName);
        chapter->setText(0, QStringLiteral("%1（%2/%3）")
                                .arg(chapterName)
                                .arg(tally.first)
                                .arg(tally.second));
        chapter->setFlags(Qt::ItemIsEnabled);
        chapters.insert(chapterName, chapter);
    }

    for (const PracticeDefinition &item : PracticeService::catalog()) {
        QTreeWidgetItem *chapter = chapters.value(item.chapter);
        if (!chapter) {
            continue;
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
    // Rebuilding redraws the ✓/○ marks and the chapter tallies. It must not
    // run the selection handler: that would clear the sandbox the student
    // still has open.
    const QString keepId = m_selectedId;
    const QString keepRepo = m_activeRepoPath;
    const bool hintWasVisible = m_hintVisible;
    const QString resultText = m_result->text();
    const QVariant resultState = m_result->property("state");
    const bool resultWasVisible = m_result->isVisible();

    m_list->blockSignals(true);
    buildList();
    if (!keepId.isEmpty()) {
        if (QTreeWidgetItem *row = itemFor(keepId)) {
            m_list->setCurrentItem(row);
        }
    }
    m_list->blockSignals(false);

    m_activeRepoPath = keepRepo;
    m_hintVisible = hintWasVisible;
    m_hint->setVisible(hintWasVisible);
    m_hintButton->setText(hintWasVisible ? QStringLiteral("隐藏提示")
                                         : QStringLiteral("显示提示"));
    if (resultWasVisible && !resultText.isEmpty()) {
        m_result->setText(resultText);
        m_result->setProperty("state", resultState);
        Theme::restyle(m_result);
        m_result->show();
    }
}

QString PracticeDialog::nextPracticeId(const QString &id) const {
    const auto catalog = PracticeService::catalog();
    for (int i = 0; i < catalog.size(); ++i) {
        if (catalog.at(i).id == id) {
            return (i + 1 < catalog.size()) ? catalog.at(i + 1).id : QString();
        }
    }
    return QString();
}

void PracticeDialog::showResult(const QString &text, bool ok) {
    m_result->setText(text);
    m_result->setProperty("state", ok ? QStringLiteral("ok") : QStringLiteral("warn"));
    Theme::restyle(m_result);
    m_result->show();
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
        showResult(QStringLiteral("无法创建练习仓库：%1").arg(error), false);
        return;
    }
    m_activeRepoPath = repoPath;
    // The strip in the main window now carries the verdict, so this note is
    // only about where the sandbox lives.
    showResult(QStringLiteral("练习仓库已就绪：%1").arg(repoPath), true);
    emit practiceStarted(m_selectedId, repoPath);
}

void PracticeDialog::checkPractice() {
    if (m_selectedId.isEmpty()) {
        return;
    }
    if (m_activeRepoPath.isEmpty()) {
        showResult(QStringLiteral("还没开始这一关。先点「开始练习」生成练习仓库。"), false);
        return;
    }

    const QString finishedId = m_selectedId;
    const PracticeCheck check = PracticeService::check(finishedId, m_activeRepoPath);

    if (!check.passed) {
        showResult(check.detail, false);
        // A previously earned pass can be lost (the sandbox was rebuilt).
        if (isPassed(finishedId)) {
            setPassed(finishedId, false);
            reloadItemStates();
            showResult(check.detail, false);
        }
        return;
    }

    setPassed(finishedId, true);
    const QString nextId = nextPracticeId(finishedId);
    const QString finishedTitle = PracticeService::definition(finishedId).title;

    reloadItemStates();

    if (nextId.isEmpty()) {
        showResult(QStringLiteral("\u2713 已通过「%1」。这是最后一关，全部完成。")
                       .arg(finishedTitle),
                   true);
        return;
    }

    const QString nextTitle = PracticeService::definition(nextId).title;
    if (selectPractice(nextId)) {
        // selectPractice runs the selection handler, which clears the old
        // sandbox and hides the result - say the verdict afterwards.
        showResult(QStringLiteral("\u2713 已通过「%1」。下一关：%2 —— 点「开始练习」生成沙箱。")
                       .arg(finishedTitle, nextTitle),
                   true);
    } else {
        showResult(QStringLiteral("\u2713 已通过「%1」。").arg(finishedTitle), true);
    }
}
