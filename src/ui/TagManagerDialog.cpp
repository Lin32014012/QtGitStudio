#include "TagManagerDialog.h"
#include "ActionButtonFeedback.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>

TagManagerDialog::TagManagerDialog(GitTagService *service, QWidget *parent)
    : QDialog(parent)
    , m_service(service)
    , m_tagTree(new QTreeWidget(this))
    , m_nameEdit(new QLineEdit(this))
    , m_typeCombo(new QComboBox(this))
    , m_messageEdit(new QLineEdit(this))
    , m_targetEdit(new QLineEdit(this))
    , m_createButton(new QPushButton(QStringLiteral("创建标签"), this))
    , m_deleteButton(new QPushButton(QStringLiteral("删除标签"), this)) {
    setWindowTitle(QStringLiteral("标签管理"));
    setMinimumSize(560, 460);

    // Create-tag form.
    auto *form = new QGroupBox(QStringLiteral("创建标签"), this);
    auto *formLayout = new QFormLayout(form);
    m_nameEdit->setPlaceholderText(QStringLiteral("例如 v1.0"));
    m_typeCombo->addItem(QStringLiteral("轻量标签 (Lightweight)"));
    m_typeCombo->addItem(QStringLiteral("附注标签 (Annotated)"));
    m_messageEdit->setPlaceholderText(QStringLiteral("附注标签的说明文字"));
    m_targetEdit->setPlaceholderText(QStringLiteral("留空 = HEAD"));
    formLayout->addRow(QStringLiteral("名称"), m_nameEdit);
    formLayout->addRow(QStringLiteral("类型"), m_typeCombo);
    formLayout->addRow(QStringLiteral("说明"), m_messageEdit);
    formLayout->addRow(QStringLiteral("目标提交"), m_targetEdit);
    formLayout->addRow(m_createButton);

    // Tag list.
    m_tagTree->setColumnCount(4);
    m_tagTree->setHeaderLabels(
        {QStringLiteral("标签"), QStringLiteral("提交"),
         QStringLiteral("类型"), QStringLiteral("说明")});
    m_tagTree->setRootIsDecorated(false);
    m_tagTree->setSelectionMode(QAbstractItemView::SingleSelection);

    auto *listLabel = new QLabel(QStringLiteral("标签列表"), this);
    listLabel->setStyleSheet(
        "font-size: 13px; font-weight: bold; color: #333;");

    auto *buttonRow = new QHBoxLayout();
    buttonRow->addWidget(m_deleteButton);
    buttonRow->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(form);
    layout->addWidget(listLabel);
    layout->addWidget(m_tagTree, 1);
    layout->addLayout(buttonRow);

    ActionButtonFeedback::configure(
        m_createButton,
        {"#16A34A", "#15803D", "#166534"},
        {QStringLiteral("正在创建…"), QStringLiteral("创建成功"),
         QStringLiteral("创建失败")});
    ActionButtonFeedback::configure(
        m_deleteButton,
        {"#DC2626", "#B91C1C", "#991B1B"},
        {QStringLiteral("正在删除…"), QStringLiteral("删除成功"),
         QStringLiteral("删除失败")});

    connect(m_createButton, &QPushButton::clicked,
            this, &TagManagerDialog::onCreateTag);
    connect(m_deleteButton, &QPushButton::clicked,
            this, &TagManagerDialog::onDeleteTag);
    connect(m_typeCombo, &QComboBox::currentIndexChanged,
            this, &TagManagerDialog::onTagTypeChanged);

    onTagTypeChanged(0);
    reload();
}

void TagManagerDialog::reload() {
    if (!m_service) return;
    const GitTagService::ListResult result = m_service->listTags();
    if (!result.success) {
        m_tagTree->clear();
        QMessageBox::warning(this, QStringLiteral("读取失败"),
                             QStringLiteral("无法读取标签列表：%1")
                                 .arg(result.error));
        return;
    }
    refreshList(result.tags);
}

void TagManagerDialog::refreshList(const QList<GitTag> &tags) {
    m_tagTree->clear();
    for (const GitTag &tag : tags) {
        auto *item = new QTreeWidgetItem(m_tagTree);
        item->setText(0, tag.name);
        item->setText(1, tag.commitHash.left(7));
        item->setText(2, tag.annotated ? QStringLiteral("附注")
                                       : QStringLiteral("轻量"));
        item->setText(3, tag.message);
        item->setData(0, Qt::UserRole, tag.name);
    }
    for (int i = 0; i < 4; ++i) {
        m_tagTree->resizeColumnToContents(i);
    }
}

void TagManagerDialog::onTagTypeChanged(int index) {
    const bool annotated = (index == 1);
    m_messageEdit->setEnabled(annotated);
}

void TagManagerDialog::onCreateTag() {
    const QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("名称为空"),
                             QStringLiteral("请输入标签名称。"));
        return;
    }
    const bool annotated = (m_typeCombo->currentIndex() == 1);
    const QString message = m_messageEdit->text().trimmed();
    const QString target = m_targetEdit->text().trimmed();
    ActionButtonFeedback::showBusy(m_createButton, QStringLiteral("正在创建…"));
    // v4-P6-21R2 P0: the service outlives this dialog - guard the
    // completion with a QPointer so a late git result after the dialog
    // is destroyed is a safe no-op.
    const QPointer<TagManagerDialog> guard(this);
    m_service->createTagAsync(
        name, target, message, annotated,
        [this, name, guard](GitTagService::OperationResult result) {
            if (!guard) {
                return;
            }
            if (result.success) {
                ActionButtonFeedback::showSuccess(m_createButton,
                                                  QStringLiteral("创建成功"));
            } else {
                ActionButtonFeedback::showError(m_createButton,
                                                QStringLiteral("创建失败"));
                QMessageBox::warning(
                    this, QStringLiteral("创建失败"),
                    QStringLiteral("无法创建标签 %1：%2")
                        .arg(name, result.error.isEmpty()
                                       ? QStringLiteral("未知错误")
                                       : result.error));
            }
            m_nameEdit->clear();
            m_messageEdit->clear();
            m_targetEdit->clear();
            reload();
        });
}

void TagManagerDialog::onDeleteTag() {
    QTreeWidgetItem *item = m_tagTree->currentItem();
    if (!item) {
        QMessageBox::information(this, QStringLiteral("未选择标签"),
                                 QStringLiteral("请先在列表中选择要删除的标签。"));
        return;
    }
    const QString name = item->data(0, Qt::UserRole).toString();
    const QMessageBox::StandardButton choice = QMessageBox::question(
        this, QStringLiteral("确认删除"),
        QStringLiteral("确定删除标签 %1 吗？").arg(name));
    if (choice != QMessageBox::Yes) return;
    ActionButtonFeedback::showBusy(m_deleteButton, QStringLiteral("正在删除…"));
    // v4-P6-21R2 P0: same lifetime guard as create.
    const QPointer<TagManagerDialog> guard(this);
    m_service->deleteTagAsync(
        name,
        [this, name, guard](GitTagService::OperationResult result) {
            if (!guard) {
                return;
            }
            if (result.success) {
                ActionButtonFeedback::showSuccess(m_deleteButton,
                                                  QStringLiteral("删除成功"));
            } else {
                ActionButtonFeedback::showError(m_deleteButton,
                                                QStringLiteral("删除失败"));
                QMessageBox::warning(
                    this, QStringLiteral("删除失败"),
                    QStringLiteral("无法删除标签 %1：%2")
                        .arg(name, result.error.isEmpty()
                                       ? QStringLiteral("未知错误")
                                       : result.error));
            }
            reload();
        });
}
