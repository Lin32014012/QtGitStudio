#include "BisectWidget.h"
#include "Theme.h"
#include "GitController.h"
#include "ActionButtonFeedback.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QPointer>

BisectWidget::BisectWidget(GitBisectService *service, GitController *controller,
                           QWidget *parent)
    : QWidget(parent)
    , m_service(service)
    , m_controller(controller)
    , m_statusLabel(new QLabel(this))
    , m_currentLabel(new QLabel(this))
    , m_goodLabel(new QLabel(this))
    , m_badLabel(new QLabel(this))
    , m_badEdit(new QLineEdit(this))
    , m_goodEdit(new QLineEdit(this))
    , m_startButton(new QPushButton(QStringLiteral("开始 Bisect"), this))
    , m_goodButton(new QPushButton(QStringLiteral("Good提交"), this))
    , m_badButton(new QPushButton(QStringLiteral("Bad提交"), this))
    , m_resetButton(new QPushButton(QStringLiteral("重置二分查找"), this)) {
    // Status display.
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setProperty("role", QStringLiteral("infoBox"));

    m_currentLabel->setWordWrap(true);
    m_goodLabel->setWordWrap(true);
    m_badLabel->setWordWrap(true);

    // Start form (bad/good optional; empty = defaults).
    auto *form = new QGroupBox(QStringLiteral("开始 Bisect"), this);
    auto *formLayout = new QFormLayout(form);
    m_badEdit->setPlaceholderText(QStringLiteral("留空 = HEAD"));
    m_goodEdit->setPlaceholderText(QStringLiteral("留空 = 最早提交"));
    formLayout->addRow(QStringLiteral("Bad 提交"), m_badEdit);
    formLayout->addRow(QStringLiteral("Good 提交"), m_goodEdit);
    formLayout->addRow(m_startButton);

    auto *markRow = new QHBoxLayout();
    markRow->addWidget(m_goodButton);
    markRow->addWidget(m_badButton);
    markRow->addWidget(m_resetButton);
    markRow->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_currentLabel);
    layout->addWidget(m_goodLabel);
    layout->addWidget(m_badLabel);
    layout->addWidget(form);
    layout->addLayout(markRow);

    ActionButtonFeedback::configure(
        m_startButton,
        {"#2563EB", "#1D4ED8", "#1E40AF"},
        {QStringLiteral("正在开始…"), QStringLiteral("Bisect 已开始"),
         QStringLiteral("开始失败")});
    ActionButtonFeedback::configure(
        m_goodButton,
        {"#16A34A", "#15803D", "#166534"},
        {QStringLiteral("正在标记…"), QStringLiteral("Good 已标记"),
         QStringLiteral("标记失败")});
    ActionButtonFeedback::configure(
        m_badButton,
        {"#DC2626", "#B91C1C", "#991B1B"},
        {QStringLiteral("正在标记…"), QStringLiteral("Bad 已标记"),
         QStringLiteral("标记失败")});
    ActionButtonFeedback::configure(
        m_resetButton,
        {"#F59E0B", "#D97706", "#B45309"},
        {QStringLiteral("正在重置…"), QStringLiteral("已重置"),
         QStringLiteral("重置失败")});

    connect(m_startButton, &QPushButton::clicked,
            this, &BisectWidget::onStartBisect);
    connect(m_goodButton, &QPushButton::clicked,
            this, &BisectWidget::onMarkGood);
    connect(m_badButton, &QPushButton::clicked,
            this, &BisectWidget::onMarkBad);
    connect(m_resetButton, &QPushButton::clicked,
            this, &BisectWidget::onResetBisect);

    reload();
}

void BisectWidget::reload() {
    if (!m_service) return;
    // v4-P6-21R2 P0: guard with a QPointer - a late status result after
    // the widget is destroyed must be a safe no-op.
    const QPointer<BisectWidget> guard(this);
    m_service->statusAsync([this, guard](const BisectStatus &status) {
        if (!guard) {
            return;
        }
        applyStatus(status);
    });
}

void BisectWidget::applyStatus(const BisectStatus &status) {
    if (status.active) {
        m_statusLabel->setText(
            QStringLiteral("状态：Bisect 进行中 — %1")
                .arg(status.message.isEmpty()
                         ? QStringLiteral("等待标记")
                         : status.message));
        m_currentLabel->setText(
            QStringLiteral("当前测试提交：%1")
                .arg(status.currentCommit.left(7)));
        m_goodLabel->setText(
            QStringLiteral("Good 提交：%1")
                .arg(status.goodCommit.isEmpty()
                         ? QStringLiteral("-")
                         : status.goodCommit.left(7)));
        m_badLabel->setText(
            QStringLiteral("Bad 提交：%1")
                .arg(status.badCommit.isEmpty()
                         ? QStringLiteral("-")
                         : status.badCommit.left(7)));
        m_goodButton->setEnabled(true);
        m_badButton->setEnabled(true);
        m_resetButton->setEnabled(true);
    } else {
        m_statusLabel->setText(QStringLiteral("状态：无进行中的 Bisect"));
        m_currentLabel->setText(QStringLiteral("当前测试提交：-"));
        m_goodLabel->setText(QStringLiteral("Good 提交：-"));
        m_badLabel->setText(QStringLiteral("Bad 提交：-"));
        m_goodButton->setEnabled(false);
        m_badButton->setEnabled(false);
        m_resetButton->setEnabled(false);
    }
}

void BisectWidget::runOperation(
    const QString &busyText, const QString &successText,
    std::function<void(GitBisectService::BisectCompletion)> launch) {
    Q_UNUSED(busyText);
    // v4-P6-21R2 P0: the service outlives this widget - guard the
    // completion so a late git result after destruction is a no-op.
    const QPointer<BisectWidget> guard(this);
    launch([this, successText, guard](const BisectResult &result) {
        if (!guard) {
            return;
        }
        switch (result.outcome) {
        case BisectOutcome::Found:
            m_statusLabel->setText(
                QStringLiteral("定位结果：第一个 Bad 提交 = %1\n%2")
                    .arg(result.suggestedCommit.left(7),
                         result.message));
            break;
        case BisectOutcome::NotActive:
            QMessageBox::information(
                this, QStringLiteral("无进行中的 Bisect"),
                QStringLiteral("请先点击“开始 Bisect”。"));
            break;
        case BisectOutcome::Error:
            QMessageBox::warning(
                this, QStringLiteral("操作失败"),
                result.message.isEmpty() ? QStringLiteral("未知错误")
                                         : result.message);
            break;
        default:
            m_statusLabel->setText(
                QStringLiteral("%1\n%2")
                    .arg(successText, result.message.left(200)));
            break;
        }
        // v4-P4-3: refresh through the existing pipeline.
        if (m_controller) {
            m_controller->refreshAsync();
        }
        reload();
    });
}

void BisectWidget::onStartBisect() {
    runOperation(
        QStringLiteral("正在开始…"), QStringLiteral("Bisect 已开始"),
        [this](GitBisectService::BisectCompletion cb) {
            m_service->startAsync(m_badEdit->text().trimmed(),
                                  m_goodEdit->text().trimmed(), cb);
        });
}

void BisectWidget::onMarkGood() {
    runOperation(
        QStringLiteral("正在标记…"), QStringLiteral("Good 已标记"),
        [this](GitBisectService::BisectCompletion cb) {
            m_service->markGoodAsync(cb);
        });
}

void BisectWidget::onMarkBad() {
    runOperation(
        QStringLiteral("正在标记…"), QStringLiteral("Bad 已标记"),
        [this](GitBisectService::BisectCompletion cb) {
            m_service->markBadAsync(cb);
        });
}

void BisectWidget::onResetBisect() {
    runOperation(
        QStringLiteral("正在重置…"), QStringLiteral("已重置"),
        [this](GitBisectService::BisectCompletion cb) {
            m_service->resetAsync(cb);
        });
}
