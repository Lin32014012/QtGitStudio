#include "GitHubAccountDialog.h"
#include "GitHubAccountService.h"
#include "GitHubRepositoryWidget.h"
#include "ProjectController.h"

#include <QLabel>
#include <QPushButton>
#include <QPointer>
#include <QLineEdit>
#include <QInputDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QVBoxLayout>
#include <QHBoxLayout>

GitHubAccountDialog::GitHubAccountDialog(GitHubAccountService *service,
                                         ProjectController *projectController,
                                         QWidget *parent)
    : QDialog(parent)
    , m_service(service)
    , m_projectController(projectController) {
    setWindowTitle(QStringLiteral("GitHub 账户"));
    setMinimumWidth(400);

    auto *title = new QLabel(QStringLiteral("GitHub 账户"), this);
    title->setStyleSheet("font-size: 16px; font-weight: 600;");

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName("githubStatus");
    m_usernameLabel = new QLabel(this);
    m_usernameLabel->setObjectName("githubUsername");

    m_connectButton = new QPushButton(QStringLiteral("连接GitHub"), this);
    m_connectButton->setObjectName("githubConnectButton");
    m_disconnectButton = new QPushButton(QStringLiteral("退出登录"), this);
    m_disconnectButton->setObjectName("githubDisconnectButton");
    m_reposButton = new QPushButton(QStringLiteral("我的仓库"), this);
    m_reposButton->setObjectName("githubReposButton");
    auto *closeButton = new QPushButton(QStringLiteral("关闭"), this);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(title);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_usernameLabel);
    auto *buttonRow = new QHBoxLayout();
    buttonRow->addWidget(m_connectButton);
    buttonRow->addWidget(m_disconnectButton);
    buttonRow->addWidget(m_reposButton);
    buttonRow->addStretch();
    buttonRow->addWidget(closeButton);
    layout->addLayout(buttonRow);

    connect(m_connectButton, &QPushButton::clicked,
            this, &GitHubAccountDialog::connectAccount);
    connect(m_disconnectButton, &QPushButton::clicked,
            this, &GitHubAccountDialog::disconnectAccount);
    connect(m_reposButton, &QPushButton::clicked,
            this, &GitHubAccountDialog::openRepositoryList);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_service, &GitHubAccountService::stateChanged,
            this, &GitHubAccountDialog::onStateChanged);

    refreshUi();
}

void GitHubAccountDialog::openRepositoryList() {
    // v4-P2-4: lightweight repository list (login-required; the widget
    // itself shows 请先登录GitHub when logged out).
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("我的GitHub仓库"));
    dialog.setMinimumSize(520, 380);
    auto *layout = new QVBoxLayout(&dialog);
    auto *widget = new GitHubRepositoryWidget(m_service, m_projectController, &dialog);
    layout->addWidget(widget);
    dialog.exec();
}

void GitHubAccountDialog::refreshUi() {
    const bool loggedIn = m_service->isLoggedIn();
    m_statusLabel->setText(loggedIn
                               ? QStringLiteral("状态：已连接")
                               : QStringLiteral("状态：未连接"));
    m_statusLabel->setStyleSheet(loggedIn
                                     ? "color: #16A34A; font-weight: 600;"
                                     : "color: #6B7280; font-weight: 600;");
    m_usernameLabel->setText(loggedIn
                                 ? QStringLiteral("用户名：%1")
                                       .arg(m_service->username())
                                 : QStringLiteral("用户名：-"));
    m_connectButton->setVisible(!loggedIn);
    m_disconnectButton->setVisible(loggedIn);
}

void GitHubAccountDialog::onStateChanged() {
    refreshUi();
}

void GitHubAccountDialog::connectAccount() {
    // v4-P6-11: custom connect dialog - clear hint, a visible
    // "打开 GitHub" button (default browser -> token settings page) and
    // the original token input kept intact.
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("连接 GitHub"));
    dialog.setMinimumWidth(480);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("连接 GitHub"), &dialog);
    title->setStyleSheet("font-size: 15px; font-weight: 600;");
    layout->addWidget(title);

    auto *hint = new QLabel(
        QStringLiteral("先在 GitHub 登录并创建 Personal Access Token，"
                       "再粘贴到这里。Token 不会明文保存到文件。"),
        &dialog);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #4B5563; font-size: 12px;");
    layout->addWidget(hint);

    auto *tokenEdit = new QLineEdit(&dialog);
    tokenEdit->setObjectName("githubTokenEdit");
    tokenEdit->setPlaceholderText(
        QStringLiteral("粘贴 GitHub Personal Access Token…"));
    tokenEdit->setEchoMode(QLineEdit::Password);
    tokenEdit->setClearButtonEnabled(true);
    layout->addWidget(tokenEdit);

    auto *buttonRow = new QHBoxLayout();
    auto *openButton = new QPushButton(QStringLiteral("打开 GitHub"), &dialog);
    openButton->setObjectName("openGithubButton");
    openButton->setToolTip(
        QStringLiteral("在默认浏览器中打开 GitHub 的 Personal Access "
                       "Token 管理页"));
    openButton->setStyleSheet(
        "QPushButton { background: #F8FAFC; color: #1E40AF;"
        " border: 1px solid #BFDBFE; border-radius: 6px;"
        " padding: 8px 14px; font-size: 13px; font-weight: 600; }"
        "QPushButton:hover { background: #EFF6FF; }"
        "QPushButton:pressed { background: #DBEAFE; }");
    connect(openButton, &QPushButton::clicked, &dialog, [&dialog]() {
        QDesktopServices::openUrl(
            QUrl(QStringLiteral("https://github.com/settings/tokens")));
    });
    auto *cancelButton = new QPushButton(QStringLiteral("取消"), &dialog);
    auto *connectButton = new QPushButton(QStringLiteral("连接"), &dialog);
    connectButton->setDefault(true);
    connectButton->setStyleSheet(
        "QPushButton { background: #1F2937; color: white;"
        " border: none; border-radius: 6px; padding: 8px 18px;"
        " font-size: 13px; font-weight: 600; }"
        "QPushButton:hover { background: #111827; }");
    buttonRow->addWidget(openButton);
    buttonRow->addStretch();
    buttonRow->addWidget(cancelButton);
    buttonRow->addWidget(connectButton);
    layout->addLayout(buttonRow);

    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(connectButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString token = tokenEdit->text().trimmed();
    if (token.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("连接失败"),
                             QStringLiteral("Token 不能为空。"));
        return;
    }
    m_connectButton->setEnabled(false);
    m_connectButton->setText(QStringLiteral("验证中…"));
    // v4-P6-21 ISSUE-01: the service outlives this dialog - guard the
    // completion with a QPointer so a late network reply after the
    // dialog was closed never touches destroyed widgets.
    const QPointer<GitHubAccountDialog> guard(this);
    m_service->verifyToken(
        token, [guard, token](const GitHubAccountService::VerifyResult &result) {
            if (!guard) {
                return;  // dialog destroyed while verifying
            }
            guard->m_connectButton->setEnabled(true);
            guard->m_connectButton->setText(QStringLiteral("连接GitHub"));
            if (!result.success) {
                QMessageBox::warning(guard, QStringLiteral("验证失败"),
                                     result.error);
                return;
            }
            guard->refreshUi();
        });
}

void GitHubAccountDialog::disconnectAccount() {
    if (QMessageBox::question(
            this, QStringLiteral("退出登录"),
            QStringLiteral("确定要退出 GitHub 登录并清除保存的 Token 吗？"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    // v4-P6-21 ISSUE-07: only refresh the UI as logged-out when the
    // credential was really removed; otherwise surface the failure.
    if (!m_service->logout()) {
        QMessageBox::warning(
            this, QStringLiteral("退出登录失败"),
            QStringLiteral("无法从系统凭据管理器删除已保存的凭据。"));
        return;
    }
    refreshUi();
}
