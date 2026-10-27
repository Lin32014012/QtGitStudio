#include "CreateBranchDialog.h"
#include "Theme.h"

#include "ActionButtonFeedback.h"
#include "BranchTreeWidget.h"
#include "GitController.h"

#include <QCheckBox>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

CreateBranchDialog::CreateBranchDialog(
    GitController *controller,
    QWidget *parent,
    const QString &initialStartPoint)
    : QDialog(parent),
      m_controller(controller) {
    setObjectName("createBranchDialog");
    setWindowTitle("创建分支");
    setModal(true);
    setMinimumSize(620, 720);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(12);

    auto *title = new QLabel("创建本地分支", this);
    title->setProperty("role", QStringLiteral("pageTitle"));
    layout->addWidget(title);

    auto *description = new QLabel(
        "先选择真实的起点分支，再填写新分支名称。名称中的 / 会形成层级。",
        this);
    description->setWordWrap(true);
    description->setProperty("role", QStringLiteral("bodyText"));
    layout->addWidget(description);

    auto *startLabel = new QLabel("起点分支", this);
    startLabel->setProperty("role", QStringLiteral("panelLabel"));
    layout->addWidget(startLabel);

    m_startTree = new BranchTreeWidget(this);
    m_startTree->setObjectName("createBranchStartTree");
    m_startTree->setMinimumHeight(240);
    m_existingBranches = m_controller->viewModel()->branches();
    const QString currentBranch = m_controller->viewModel()->currentBranch();
    const QString preferredStart = m_existingBranches.contains(initialStartPoint)
        ? initialStartPoint
        : currentBranch;
    m_startTree->setBranchData(m_existingBranches, currentBranch, preferredStart);
    layout->addWidget(m_startTree, 1);

    auto *nameLabel = new QLabel("新分支名称", this);
    nameLabel->setProperty("role", QStringLiteral("panelLabel"));
    layout->addWidget(nameLabel);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setObjectName("createBranchNameEdit");
    m_nameEdit->setPlaceholderText("例如：feature/platform/new-ui");
    m_nameEdit->setClearButtonEnabled(true);
    m_nameEdit->setMinimumHeight(38);
    layout->addWidget(m_nameEdit);

    m_validationLabel = new QLabel(this);
    m_validationLabel->setObjectName("createBranchValidationLabel");
    m_validationLabel->setWordWrap(true);
    m_validationLabel->setMinimumHeight(20);
    layout->addWidget(m_validationLabel);

    m_switchCheck = new QCheckBox("创建后切换到新分支", this);
    m_switchCheck->setObjectName("createBranchSwitchCheck");
    m_switchCheck->setChecked(true);
    layout->addWidget(m_switchCheck);

    auto *previewFrame = new QFrame(this);
    previewFrame->setProperty("role", QStringLiteral("subtlePanel"));
    previewFrame->setAttribute(Qt::WA_StyledBackground, true);
    auto *previewLayout = new QVBoxLayout(previewFrame);
    previewLayout->setContentsMargins(14, 12, 14, 12);
    auto *previewTitle = new QLabel("创建预览", previewFrame);
    previewTitle->setProperty("role", QStringLiteral("panelLabel"));
    previewLayout->addWidget(previewTitle);
    m_previewLabel = new QLabel(previewFrame);
    m_previewLabel->setObjectName("createBranchPreview");
    m_previewLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_previewLabel->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_previewLabel->setProperty("role", QStringLiteral("bodyText"));
    previewLayout->addWidget(m_previewLabel);
    layout->addWidget(previewFrame);

    m_busyLabel = new QLabel("正在通过 Git 创建分支，请稍候…", this);
    m_busyLabel->setObjectName("createBranchBusyLabel");
    m_busyLabel->setProperty("role", QStringLiteral("bodyText"));
    m_busyLabel->hide();
    layout->addWidget(m_busyLabel);

    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    m_cancelButton = new QPushButton("取消", this);
    m_cancelButton->setObjectName("cancelCreateBranchButton");
    m_cancelButton->setMinimumHeight(40);
    m_cancelButton->setMinimumWidth(96);
    m_createButton = new QPushButton("创建分支", this);
    m_createButton->setObjectName("confirmCreateBranchButton");
    ActionButtonFeedback::configure(
        m_createButton,
        {"#16A34A", "#15803D", "#166534"},
        {"正在创建…", "创建成功", "创建失败"});
    m_createButton->setDefault(true);
    buttonLayout->addWidget(m_cancelButton);
    buttonLayout->addWidget(m_createButton);
    layout->addLayout(buttonLayout);

    m_validationTimer = new QTimer(this);
    m_validationTimer->setSingleShot(true);
    m_validationTimer->setInterval(140);

    connect(m_nameEdit, &QLineEdit::textChanged, this, &CreateBranchDialog::scheduleValidation);
    connect(
        m_startTree,
        &QTreeWidget::itemSelectionChanged,
        this,
        &CreateBranchDialog::scheduleValidation);
    connect(m_validationTimer, &QTimer::timeout, this, &CreateBranchDialog::validateBranchName);
    connect(m_createButton, &QPushButton::clicked, this, &CreateBranchDialog::createBranch);
    connect(m_cancelButton, &QPushButton::clicked, this, &CreateBranchDialog::reject);

    updatePreview();
    scheduleValidation();
    QTimer::singleShot(0, m_nameEdit, [this]() { m_nameEdit->setFocus(); });
}

void CreateBranchDialog::reject() {
    if (!m_busy) {
        QDialog::reject();
    }
}

void CreateBranchDialog::scheduleValidation() {
    ++m_validationGeneration;
    m_validationTimer->stop();
    m_nameValid = false;
    updatePreview();

    const QString localError = localValidationError();
    if (!localError.isEmpty()) {
        showValidationError(localError);
        updateCreateState();
        return;
    }

    m_validationLabel->setText("正在通过 Git 检查名称…");
    m_validationLabel->setProperty("role", QStringLiteral("bodyText"));
    updateCreateState();
    m_validationTimer->start();
}

void CreateBranchDialog::validateBranchName() {
    const int generation = m_validationGeneration;
    const QString branchName = m_nameEdit->text();
    const QPointer<CreateBranchDialog> guardedDialog(this);
    m_controller->validateBranchNameAsync(
        branchName,
        [guardedDialog, generation, branchName](GitController::OperationResult result) {
            if (!guardedDialog || guardedDialog->m_busy ||
                generation != guardedDialog->m_validationGeneration ||
                branchName != guardedDialog->m_nameEdit->text()) {
                return;
            }

            guardedDialog->m_nameValid = result.success;
            if (result.success) {
                guardedDialog->m_validationLabel->setText("名称可用");
                guardedDialog->m_validationLabel->setProperty("role", QStringLiteral("validation"));
                guardedDialog->m_validationLabel->setProperty("state", QStringLiteral("ok"));
                Theme::restyle(guardedDialog->m_validationLabel);
            } else {
                guardedDialog->showValidationError(
                    "分支名称格式无效。\nGit 校验：" + result.error.trimmed());
            }
            guardedDialog->updateCreateState();
        });
}

void CreateBranchDialog::createBranch() {
    // v4-P2-1.1: empty repo -> no start point required.
    if (m_busy || !m_nameValid
        || (startPoint().isEmpty()
            && m_controller->repoState() != GitController::RepoState::EmptyRepository)) {
        return;
    }

    const QString branchName = m_nameEdit->text();
    const QString selectedStartPoint = startPoint();
    const bool switchAfterCreation = m_switchCheck->isChecked();
    setBusy(true);
    emit creationStarted(branchName);

    const QPointer<CreateBranchDialog> guardedDialog(this);
    m_controller->createBranchAsync(
        branchName,
        selectedStartPoint,
        switchAfterCreation,
        [guardedDialog, branchName, switchAfterCreation](GitController::OperationResult result) {
            if (!guardedDialog) {
                return;
            }
            if (!result.success) {
                guardedDialog->setBusy(false);
                guardedDialog->showValidationError(result.error.trimmed());
                ActionButtonFeedback::showError(
                    guardedDialog->m_createButton,
                    "创建失败",
                    350);
                guardedDialog->updateCreateState();
                emit guardedDialog->branchCreationFailed(result.error);
                return;
            }

            guardedDialog->m_busyLabel->setText("分支创建成功");
            ActionButtonFeedback::showSuccess(
                guardedDialog->m_createButton,
                "创建成功",
                350);
            emit guardedDialog->branchCreated(branchName, switchAfterCreation);
            QTimer::singleShot(220, guardedDialog, [guardedDialog]() {
                if (guardedDialog) {
                    guardedDialog->QDialog::accept();
                }
            });
        });
}

QString CreateBranchDialog::localValidationError() const {
    const QString branchName = m_nameEdit->text();
    if (branchName.trimmed().isEmpty()) {
        return "分支名称不能为空。";
    }
    for (const QChar character : branchName) {
        if (character.category() == QChar::Other_Control) {
            return "分支名称不能包含换行或控制字符。";
        }
    }
    if (branchName != branchName.trimmed()) {
        return "分支名称前后不能包含空格。";
    }
    if (m_existingBranches.contains(branchName)) {
        return QString("分支“%1”已经存在。").arg(branchName);
    }
    // v4-P2-1.1: empty repositories have no start branch - creation is
    // still allowed (git switch -c without a start point).
    if (startPoint().isEmpty()
        && m_controller->repoState() != GitController::RepoState::EmptyRepository) {
        return "请选择一个真实的起点分支。";
    }
    return {};
}

QString CreateBranchDialog::startPoint() const {
    return m_startTree->selectedBranch();
}

void CreateBranchDialog::updatePreview() {
    const QString selectedStartPoint = startPoint();
    const QString startText = selectedStartPoint.isEmpty()
        ? QStringLiteral("（请选择真实分支）")
        : selectedStartPoint;

    QString hierarchy;
    const QString branchName = m_nameEdit->text().trimmed();
    if (branchName.isEmpty()) {
        hierarchy = QStringLiteral("（请输入名称）");
    } else {
        const QStringList parts = branchName.split('/', Qt::KeepEmptyParts);
        hierarchy = parts.first();
        for (int index = 1; index < parts.size(); ++index) {
            hierarchy += "\n" + QString((index - 1) * 3, ' ') + "└─ " + parts.at(index);
        }
    }

    m_previewLabel->setText(
        "起点分支：\n● " + startText + "\n\n名称层级：\n" + hierarchy);
}

void CreateBranchDialog::updateCreateState() {
    // v4-P2-1.1: empty repo -> no start point required.
    const bool startOk = !startPoint().isEmpty()
        || m_controller->repoState() == GitController::RepoState::EmptyRepository;
    const bool ready = !m_busy && m_nameValid && startOk;
    ActionButtonFeedback::setReady(m_createButton, ready);
}

void CreateBranchDialog::setBusy(bool busy) {
    m_busy = busy;
    if (busy) {
        ++m_validationGeneration;
        m_validationTimer->stop();
        ActionButtonFeedback::showBusy(m_createButton, "正在创建…");
    }
    m_startTree->setEnabled(!busy);
    m_nameEdit->setEnabled(!busy);
    m_switchCheck->setEnabled(!busy);
    m_cancelButton->setEnabled(!busy);
    m_busyLabel->setVisible(busy);
    if (!busy) {
        updateCreateState();
    }
}

void CreateBranchDialog::showValidationError(const QString &error) {
    m_validationLabel->setText(error);
    m_validationLabel->setProperty("role", QStringLiteral("validation"));
    m_validationLabel->setProperty("state", QStringLiteral("danger"));
    Theme::restyle(m_validationLabel);
}
