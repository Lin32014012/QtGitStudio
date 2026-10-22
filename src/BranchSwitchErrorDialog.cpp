#include "BranchSwitchErrorDialog.h"

#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

BranchSwitchErrorDialog::BranchSwitchErrorDialog(
    const QString &targetBranch,
    const QString &gitError,
    QWidget *parent)
    : QDialog(parent) {
    setObjectName("branchSwitchErrorDialog");
    setWindowTitle("无法切换分支");
    setModal(false);
    setMinimumWidth(620);
    setAttribute(Qt::WA_DeleteOnClose);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 20);
    layout->setSpacing(12);

    auto *title = new QLabel("无法切换分支", this);
    title->setStyleSheet("font-size: 19px; font-weight: 700; color: #991B1B;");
    layout->addWidget(title);

    auto *explanation = new QLabel(
        QString("当前仓库存在尚未提交的修改。切换到“%1”会覆盖部分文件，因此 Git 已阻止本次操作，以避免修改丢失。")
            .arg(targetBranch),
        this);
    explanation->setWordWrap(true);
    explanation->setStyleSheet("color: #111827;");
    layout->addWidget(explanation);

    const QStringList files = conflictingFiles(gitError);
    auto *filesLabel = new QLabel(this);
    filesLabel->setObjectName("branchSwitchConflictFiles");
    filesLabel->setText(
        files.isEmpty()
            ? QStringLiteral("Git 未返回可解析的冲突文件列表。")
            : QStringLiteral("可能被覆盖的文件：\n• ") + files.join("\n• "));
    filesLabel->setWordWrap(true);
    filesLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    filesLabel->setStyleSheet(
        "color: #7F1D1D; background: #FEF2F2; border: 1px solid #FECACA; "
        "border-radius: 6px; padding: 10px;");
    layout->addWidget(filesLabel);

    auto *actions = new QLabel(
        "建议操作：\n"
        "1. 先提交当前修改；\n"
        "2. 使用 Stash 临时保存修改；\n"
        "3. 取消切换。\n\n"
        "暂存当前修改功能将在后续版本提供，请先提交修改或取消切换。",
        this);
    actions->setWordWrap(true);
    actions->setStyleSheet("color: #374151;");
    layout->addWidget(actions);

    auto *rawToggle = new QToolButton(this);
    rawToggle->setObjectName("branchSwitchRawToggle");
    rawToggle->setText("查看 Git 原始错误");
    rawToggle->setCheckable(true);
    rawToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    rawToggle->setArrowType(Qt::RightArrow);
    layout->addWidget(rawToggle);

    auto *rawError = new QPlainTextEdit(this);
    rawError->setObjectName("branchSwitchRawError");
    rawError->setReadOnly(true);
    rawError->setPlainText(gitError.trimmed());
    rawError->setMinimumHeight(150);
    rawError->hide();
    layout->addWidget(rawError);

    auto *closeButton = new QPushButton("关闭", this);
    closeButton->setObjectName("branchSwitchCloseButton");
    closeButton->setMinimumHeight(38);
    layout->addWidget(closeButton, 0, Qt::AlignRight);

    connect(rawToggle, &QToolButton::toggled, this, [rawToggle, rawError](bool expanded) {
        rawToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        rawError->setVisible(expanded);
    });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
}

bool BranchSwitchErrorDialog::isOverwriteProtectionError(const QString &gitError) {
    return gitError.contains(
        "Your local changes to the following files would be overwritten by checkout",
        Qt::CaseInsensitive);
}

QStringList BranchSwitchErrorDialog::conflictingFiles(const QString &gitError) {
    QStringList files;
    bool collecting = false;
    const QStringList lines = gitError.split('\n');
    for (const QString &line : lines) {
        if (line.contains(
                "Your local changes to the following files would be overwritten by checkout",
                Qt::CaseInsensitive)) {
            collecting = true;
            continue;
        }
        if (!collecting) {
            continue;
        }

        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        if (trimmed.startsWith("Please ", Qt::CaseInsensitive) ||
            trimmed.compare("Aborting", Qt::CaseInsensitive) == 0 ||
            trimmed.startsWith("error:", Qt::CaseInsensitive)) {
            break;
        }
        files.append(trimmed);
    }
    files.removeDuplicates();
    return files;
}

QString BranchSwitchErrorDialog::diagnosticText(
    const QString &targetBranch,
    const QString &gitError) {
    const QStringList files = conflictingFiles(gitError);
    const QString fileText = files.isEmpty()
        ? QStringLiteral("（Git 未返回可解析的文件列表）")
        : files.join('\n');
    return QString(
               "切换分支失败\n\n"
               "无法切换分支\n"
               "当前仓库存在尚未提交的修改。切换到“%1”会覆盖部分文件，因此 Git 已阻止本次操作，以避免修改丢失。\n\n"
               "可能被覆盖的文件：\n%2\n\n"
               "建议操作：\n"
               "1. 先提交当前修改；\n"
               "2. 使用 Stash 临时保存修改；\n"
               "3. 取消切换。\n\n"
               "暂存当前修改功能将在后续版本提供，请先提交修改或取消切换。\n\n"
               "Git 原始错误：\n%3")
        .arg(targetBranch, fileText, gitError.trimmed());
}
