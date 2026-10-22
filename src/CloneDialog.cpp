#include "CloneDialog.h"
#include "ActionButtonFeedback.h"
#include "ProjectController.h"

#include <QDir>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPointer>
#include <QPlainTextEdit>
#include <QTimer>
#include <QVBoxLayout>

CloneDialog::CloneDialog(ProjectController *controller, QWidget *parent)
    : QDialog(parent), m_controller(controller) {
    setWindowTitle("克隆仓库");
    setModal(true);
    resize(640, 520);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    m_sourceEdit = new QLineEdit(this);
    m_sourceEdit->setObjectName("cloneSourceEdit");
    m_sourceEdit->setPlaceholderText("HTTPS、SSH 地址或本地仓库路径");
    form->addRow("仓库地址或本地路径：", m_sourceEdit);

    auto *targetRow = new QWidget(this);
    auto *targetRowLayout = new QHBoxLayout(targetRow);
    targetRowLayout->setContentsMargins(0, 0, 0, 0);
    m_targetParentEdit = new QLineEdit(targetRow);
    m_targetParentEdit->setObjectName("cloneTargetParentEdit");
    m_targetParentEdit->setPlaceholderText("选择目标父目录");
    m_browseButton = new QPushButton("浏览…", targetRow);
    m_browseButton->setObjectName("cloneBrowseButton");
    targetRowLayout->addWidget(m_targetParentEdit);
    targetRowLayout->addWidget(m_browseButton);
    form->addRow("目标父目录：", targetRow);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setObjectName("cloneNameEdit");
    m_nameEdit->setPlaceholderText("最终目录名称");
    form->addRow("目录名称：", m_nameEdit);
    layout->addLayout(form);

    m_targetPreview = new QLabel(this);
    m_targetPreview->setObjectName("cloneTargetPreview");
    m_targetPreview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_targetPreview->setWordWrap(true);
    layout->addWidget(m_targetPreview);

    m_statusLabel = new QLabel("等待输入克隆信息", this);
    m_statusLabel->setObjectName("cloneStatusLabel");
    layout->addWidget(m_statusLabel);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName("cloneErrorLabel");
    m_errorLabel->setStyleSheet("color: #B91C1C; background: #FEF2F2; padding: 8px; border-radius: 4px;");
    m_errorLabel->setWordWrap(true);
    m_errorLabel->hide();
    layout->addWidget(m_errorLabel);

    m_errorDetails = new QPlainTextEdit(this);
    m_errorDetails->setObjectName("cloneErrorDetails");
    m_errorDetails->setReadOnly(true);
    m_errorDetails->setMinimumHeight(150);
    m_errorDetails->hide();
    layout->addWidget(m_errorDetails);

    m_copyErrorButton = new QPushButton("复制错误详情", this);
    m_copyErrorButton->setObjectName("cloneCopyErrorButton");
    m_copyErrorButton->setToolTip("复制 Git 路径、参数、退出信息与完整输出");
    m_copyErrorButton->hide();
    layout->addWidget(m_copyErrorButton, 0, Qt::AlignLeft);
    layout->addStretch();

    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    m_cancelButton = new QPushButton("取消", this);
    m_cancelButton->setObjectName("cloneCancelButton");
    m_startButton = new QPushButton("开始克隆", this);
    m_startButton->setObjectName("cloneStartButton");
    m_startButton->setDefault(true);
    ActionButtonFeedback::configure(
        m_startButton,
        {"#475569", "#334155", "#1E293B"},
        {"正在克隆…", "克隆成功", "克隆失败"});
    buttonLayout->addWidget(m_cancelButton);
    buttonLayout->addWidget(m_startButton);
    layout->addLayout(buttonLayout);

    connect(m_browseButton, &QPushButton::clicked, this, &CloneDialog::browseTargetParent);
    connect(m_sourceEdit, &QLineEdit::textChanged, this, &CloneDialog::onInputChanged);
    connect(m_targetParentEdit, &QLineEdit::textChanged, this, &CloneDialog::onInputChanged);
    connect(m_nameEdit, &QLineEdit::textChanged, this, &CloneDialog::onInputChanged);
    connect(m_startButton, &QPushButton::clicked, this, &CloneDialog::startClone);
    connect(m_cancelButton, &QPushButton::clicked, this, &CloneDialog::reject);
    connect(m_copyErrorButton, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_errorDetails->toPlainText());
    });
    updateState();
}

QString CloneDialog::targetPath() const {
    if (m_targetParentEdit->text().trimmed().isEmpty() || m_nameEdit->text().trimmed().isEmpty()) {
        return QString();
    }
    return QDir(m_targetParentEdit->text().trimmed()).filePath(m_nameEdit->text().trimmed());
}

void CloneDialog::browseTargetParent() {
    const QString directory = QFileDialog::getExistingDirectory(this, "选择目标父目录", QDir::rootPath());
    if (!directory.isEmpty()) {
        m_targetParentEdit->setText(directory);
    }
}

QString CloneDialog::suggestedRepositoryName() const {
    QString source = m_sourceEdit->text().trimmed();
    source.replace('\\', '/');
    while (source.endsWith('/')) {
        source.chop(1);
    }
    QString name = source.section('/', -1);
    if (name.contains(':')) {
        name = name.section(':', -1);
    }
    if (name.endsWith(".git", Qt::CaseInsensitive)) {
        name.chop(4);
    }
    return name;
}

void CloneDialog::onInputChanged() {
    clearCloneFailure();
    updateState();
}

void CloneDialog::clearCloneFailure() {
    if (!m_hasCloneFailure) {
        return;
    }
    m_hasCloneFailure = false;
    m_errorDetails->clear();
    m_errorDetails->hide();
    m_copyErrorButton->hide();
}

void CloneDialog::updateState() {
    const QString suggestion = suggestedRepositoryName();
    const QString previousSuggestion = m_lastSuggestedName;
    m_lastSuggestedName = suggestion;
    if (!suggestion.isEmpty() &&
        (m_nameEdit->text().isEmpty() || m_nameEdit->text() == previousSuggestion)) {
        m_nameEdit->setText(suggestion);
    }

    const QString source = m_sourceEdit->text().trimmed();
    const QString parent = m_targetParentEdit->text().trimmed();
    const QString name = m_nameEdit->text().trimmed();
    const QString target = targetPath();
    m_targetPreview->setText(target.isEmpty() ? "目标路径：--" : "目标路径：" + QDir::toNativeSeparators(target));

    QString validationError;
    if (source.isEmpty()) {
        validationError = "请输入仓库地址或本地源路径";
    } else if (parent.isEmpty()) {
        validationError = "请选择目标父目录";
    } else if (!QDir(parent).exists()) {
        validationError = "目标父目录不存在";
    } else if (name.isEmpty()) {
        validationError = "请输入最终目录名称";
    } else if (QFileInfo::exists(target)) {
        validationError = "目标目录已存在：" + QDir::toNativeSeparators(target);
    }

    ActionButtonFeedback::setReady(m_startButton, validationError.isEmpty() && !m_cloneInProgress);
    if (!validationError.isEmpty() && !source.isEmpty()) {
        m_errorLabel->setText(validationError);
        m_errorLabel->show();
    } else if (!m_cloneInProgress && !m_hasCloneFailure) {
        m_errorLabel->clear();
        m_errorLabel->hide();
    }
}

void CloneDialog::setCloneInProgress(bool inProgress) {
    m_cloneInProgress = inProgress;
    m_sourceEdit->setEnabled(!inProgress);
    m_targetParentEdit->setEnabled(!inProgress);
    m_nameEdit->setEnabled(!inProgress);
    m_browseButton->setEnabled(!inProgress);
    m_cancelButton->setEnabled(!inProgress);
    ActionButtonFeedback::setReady(m_startButton, false);
}

void CloneDialog::startClone() {
    updateState();
    if (!m_startButton->isEnabled()) {
        return;
    }

    setCloneInProgress(true);
    ActionButtonFeedback::showBusy(m_startButton, "正在克隆…");
    clearCloneFailure();
    m_errorLabel->hide();
    m_statusLabel->setText("正在克隆仓库，请稍候…");
    const QPointer<CloneDialog> dialog(this);
    m_controller->cloneRepositoryDetailedAsync(
        m_sourceEdit->text().trimmed(),
        targetPath(),
        [dialog](ProjectController::CloneResult result) {
            if (!dialog) {
                return;
            }
            if (!result.success) {
                dialog->setCloneInProgress(false);
                ActionButtonFeedback::setReady(dialog->m_startButton, true);
                ActionButtonFeedback::showError(dialog->m_startButton, "克隆失败", 1300);
                dialog->m_statusLabel->setText("克隆失败，请检查输入后重试");
                dialog->m_hasCloneFailure = true;
                dialog->m_errorLabel->setText(result.summary);
                dialog->m_errorLabel->show();
                dialog->m_errorDetails->setPlainText(result.details());
                dialog->m_errorDetails->show();
                dialog->m_copyErrorButton->show();
                return;
            }

            ActionButtonFeedback::showSuccess(dialog->m_startButton, "克隆成功", 600);
            dialog->m_statusLabel->setText("克隆完成");
            QTimer::singleShot(600, dialog, [dialog]() {
                if (dialog) {
                    dialog->accept();
                }
            });
        });
}
