#include "StashWidget.h"
#include "Theme.h"
#include "GitController.h"
#include "ActionButtonFeedback.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLineEdit>
#include <QPushButton>
#include <QPointer>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QMessageBox>

StashWidget::StashWidget(GitStashService *service, GitController *controller,
                         QWidget *parent)
    : QWidget(parent)
    , m_service(service)
    , m_controller(controller)
    , m_stashTree(new QTreeWidget(this))
    , m_messageEdit(new QLineEdit(this))
    , m_createButton(new QPushButton(QStringLiteral("创建 Stash"), this))
    , m_applyButton(new QPushButton(QStringLiteral("应用暂存"), this))
    , m_popButton(new QPushButton(QStringLiteral("恢复暂存"), this))
    , m_dropButton(new QPushButton(QStringLiteral("删除暂存"), this)) {
    // Create row.
    m_messageEdit->setPlaceholderText(
        QStringLiteral("Stash 说明（可选，留空使用默认说明）"));
    m_messageEdit->setClearButtonEnabled(true);
    auto *createRow = new QHBoxLayout();
    createRow->addWidget(m_messageEdit, 1);
    createRow->addWidget(m_createButton);

    // Stash list.
    m_stashTree->setColumnCount(4);
    m_stashTree->setHeaderLabels(
        {QStringLiteral("编号"), QStringLiteral("说明"),
         QStringLiteral("分支"), QStringLiteral("时间")});
    m_stashTree->setRootIsDecorated(false);
    m_stashTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_stashTree->setSelectionBehavior(QAbstractItemView::SelectRows);

    auto *listLabel = new QLabel(QStringLiteral("Stash 列表"), this);
    listLabel->setProperty("role", QStringLiteral("cardTitle"));


    auto *buttonRow = new QHBoxLayout();
    buttonRow->addWidget(m_applyButton);
    buttonRow->addWidget(m_popButton);
    buttonRow->addWidget(m_dropButton);
    buttonRow->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(createRow);
    layout->addWidget(listLabel);
    layout->addWidget(m_stashTree, 1);
    layout->addLayout(buttonRow);

    ActionButtonFeedback::configure(
        m_createButton,
        {"#16A34A", "#15803D", "#166534"},
        {QStringLiteral("正在创建…"), QStringLiteral("创建成功"),
         QStringLiteral("创建失败")});
    ActionButtonFeedback::configure(
        m_applyButton,
        {"#2563EB", "#1D4ED8", "#1E40AF"},
        {QStringLiteral("正在应用…"), QStringLiteral("应用成功"),
         QStringLiteral("应用失败")});
    ActionButtonFeedback::configure(
        m_popButton,
        {"#7C3AED", "#6D28D9", "#5B21B6"},
        {QStringLiteral("正在恢复…"), QStringLiteral("恢复成功"),
         QStringLiteral("恢复失败")});
    ActionButtonFeedback::configure(
        m_dropButton,
        {"#DC2626", "#B91C1C", "#991B1B"},
        {QStringLiteral("正在删除…"), QStringLiteral("删除成功"),
         QStringLiteral("删除失败")});

    connect(m_createButton, &QPushButton::clicked,
            this, &StashWidget::onCreateStash);
    connect(m_applyButton, &QPushButton::clicked,
            this, &StashWidget::onApplyStash);
    connect(m_popButton, &QPushButton::clicked,
            this, &StashWidget::onPopStash);
    connect(m_dropButton, &QPushButton::clicked,
            this, &StashWidget::onDropStash);

    reload();
}

void StashWidget::reload() {
    if (!m_service) return;
    const GitStashService::ListResult result = m_service->listStashes();
    if (!result.success) {
        m_stashTree->clear();
        QMessageBox::warning(this, QStringLiteral("读取失败"),
                             QStringLiteral("无法读取 Stash 列表：%1")
                                 .arg(result.error));
        return;
    }
    refreshList(result.stashes);
}

void StashWidget::refreshList(const QList<GitStash> &stashes) {
    m_stashTree->clear();
    for (const GitStash &stash : stashes) {
        auto *item = new QTreeWidgetItem(m_stashTree);
        item->setText(0, stash.id);
        item->setText(1, stash.message);
        item->setText(2, stash.branch);
        item->setText(3, stash.date);
        item->setData(0, Qt::UserRole, stash.id);
    }
    for (int i = 0; i < 4; ++i) {
        m_stashTree->resizeColumnToContents(i);
    }
}

QString StashWidget::selectedStashId() const {
    QTreeWidgetItem *item = m_stashTree->currentItem();
    return item ? item->data(0, Qt::UserRole).toString() : QString();
}

void StashWidget::runOperation(
    const QString &id,
    std::function<void(GitStashService::OperationCompletion)> launch,
    QPushButton *button, const QString &busyText, const QString &successText) {
    if (id.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("未选择 Stash"),
                                 QStringLiteral("请先在列表中选择一个 Stash。"));
        return;
    }
    ActionButtonFeedback::showBusy(button, busyText);
    // v4-P6-21 ISSUE-02: the service outlives this widget - guard the
    // completion with QPointers so a late git process finishing after
    // the stash window was closed never touches destroyed widgets.
    const QPointer<StashWidget> guard(this);
    const QPointer<QPushButton> guardButton(button);
    launch([guard, guardButton, successText](GitStashService::OperationResult result) {
        if (!guard) {
            return;  // widget destroyed while the operation ran
        }
        if (result.success) {
            if (guardButton) {
                ActionButtonFeedback::showSuccess(guardButton, successText);
            }
        } else {
            // v4-P6-18: keep raw git errors (e.g. "could not write
            // index") out of the button - short label + real detail in
            // a message box.
            if (guardButton) {
                ActionButtonFeedback::showError(
                    guardButton, QStringLiteral("操作失败"));
            }
            QMessageBox::warning(guard, QStringLiteral("Stash 操作失败"),
                                 result.error);
        }
        // v4-P3-5: refresh through the existing pipeline.
        if (guard->m_controller) {
            guard->m_controller->refreshAsync();
        }
        guard->reload();
    });
}

void StashWidget::onCreateStash() {
    const QString message = m_messageEdit->text().trimmed();
    ActionButtonFeedback::showBusy(m_createButton, QStringLiteral("正在创建…"));
    // v4-P6-21 ISSUE-02: same lifetime guard as runOperation.
    const QPointer<StashWidget> guard(this);
    const QPointer<QPushButton> guardButton(m_createButton);
    m_service->createStashAsync(
        message,
        [guard, guardButton](GitStashService::OperationResult result) {
            if (!guard) {
                return;  // widget destroyed while creating
            }
            if (result.success) {
                if (guardButton) {
                    ActionButtonFeedback::showSuccess(
                        guardButton, QStringLiteral("创建成功"));
                }
            } else {
                if (guardButton) {
                    ActionButtonFeedback::showError(
                        guardButton, result.error.isEmpty()
                                         ? QStringLiteral("创建失败")
                                         : result.error);
                }
            }
            if (guard->m_controller) {
                guard->m_controller->refreshAsync();
            }
            guard->m_messageEdit->clear();
            guard->reload();
        });
}

void StashWidget::onApplyStash() {
    const QString id = selectedStashId();
    runOperation(id,
                 [this, id](GitStashService::OperationCompletion cb) {
                     m_service->applyStashAsync(id, cb);
                 },
                 m_applyButton, QStringLiteral("正在应用…"),
                 QStringLiteral("Apply 成功"));
}

void StashWidget::onPopStash() {
    const QString id = selectedStashId();
    runOperation(id,
                 [this, id](GitStashService::OperationCompletion cb) {
                     m_service->popStashAsync(id, cb);
                 },
                 m_popButton, QStringLiteral("正在 Pop…"),
                 QStringLiteral("Pop 成功"));
}

void StashWidget::onDropStash() {
    const QString id = selectedStashId();
    if (id.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("未选择 Stash"),
                                 QStringLiteral("请先在列表中选择一个 Stash。"));
        return;
    }
    const QMessageBox::StandardButton choice = QMessageBox::question(
        this, QStringLiteral("确认删除"),
        QStringLiteral("确定删除 %1 吗？此操作不可恢复。").arg(id));
    if (choice != QMessageBox::Yes) return;
    runOperation(id,
                 [this, id](GitStashService::OperationCompletion cb) {
                     m_service->dropStashAsync(id, cb);
                 },
                 m_dropButton, QStringLiteral("正在删除…"),
                 QStringLiteral("删除成功"));
}
