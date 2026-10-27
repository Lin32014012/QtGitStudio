#include "DiffViewerWidget.h"
#include "EncodingDiagnostic.h"

#include <QLabel>
#include <QTextEdit>
#include <QVBoxLayout>

DiffViewerWidget::DiffViewerWidget(QWidget *parent)
    : QWidget(parent) {
    m_statusLabel = new QLabel(QStringLiteral("预览"), this);
    m_statusLabel->setObjectName("diffStatusLabel");
    m_statusLabel->setProperty("role", QStringLiteral("panelLabel"));

    m_textEdit = new QTextEdit(this);
    m_textEdit->setObjectName("diffTextEdit");
    m_textEdit->setReadOnly(true);
    m_textEdit->setProperty("role", QStringLiteral("diffView"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_textEdit, 1);
}

void DiffViewerWidget::showTextDiff(const QString &text) {
    m_statusLabel->setText(QStringLiteral("差异预览"));
    m_textEdit->setPlainText(text);
}

void DiffViewerWidget::showBinaryDiagnosis(
    const QString &fileName, const EncodingDiagnostic &diag) {
    m_statusLabel->setText(QStringLiteral("二进制文件"));
    QString text;
    text += QStringLiteral("文件：%1\n\n").arg(fileName);
    text += diag.explanation();
    if (!diag.encodingName.isEmpty()) {
        text += QStringLiteral("\n\n检测：%1 编码").arg(diag.encodingName);
    }
    if (diag.hasNullBytes) {
        text += QStringLiteral("\n原因：文件包含大量空字节");
    }
    text += QStringLiteral("\n建议：转换为 UTF-8 后重新查看 Diff");
    m_textEdit->setPlainText(text);
}

void DiffViewerWidget::showMessage(const QString &message) {
    m_statusLabel->setText(QStringLiteral("预览"));
    m_textEdit->setPlainText(message);
}

void DiffViewerWidget::clearView() {
    m_statusLabel->setText(QStringLiteral("预览"));
    m_textEdit->clear();
}

QString DiffViewerWidget::currentText() const {
    return m_textEdit->toPlainText();
}
