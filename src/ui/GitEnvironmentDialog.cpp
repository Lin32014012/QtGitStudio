#include "GitEnvironmentDialog.h"
#include "GitEnvironmentService.h"
#include "GitCommandExecutor.h"

#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QTimer>
#include <QTime>
#include <QMessageBox>

GitEnvironmentDialog::GitEnvironmentDialog(GitEnvironmentService *service,
                                           QWidget *parent)
    : QDialog(parent)
    , m_service(service) {
    setWindowTitle(QStringLiteral("Git 环境管理"));
    setMinimumWidth(420);

    m_pathLabel = new QLabel(this);
    m_pathLabel->setObjectName("gitEnvPath");
    m_pathLabel->setWordWrap(true);
    m_versionLabel = new QLabel(this);
    m_versionLabel->setObjectName("gitEnvVersion");
    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName("gitEnvStatus");

    m_browseButton = new QPushButton(QStringLiteral("设置路径…"), this);
    m_browseButton->setObjectName("gitEnvBrowse");
    m_redetectButton = new QPushButton(QStringLiteral("重新检测"), this);
    m_redetectButton->setObjectName("gitEnvRedetect");
    m_closeButton = new QPushButton(QStringLiteral("关闭"), this);
    m_closeButton->setObjectName("gitEnvClose");

    auto *layout = new QVBoxLayout(this);
    auto *title = new QLabel(QStringLiteral("Git 环境"), this);
    title->setStyleSheet("font-size: 16px; font-weight: 600;");
    layout->addWidget(title);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_pathLabel);
    layout->addWidget(m_versionLabel);
    auto *buttonRow = new QHBoxLayout();
    buttonRow->addWidget(m_browseButton);
    buttonRow->addWidget(m_redetectButton);
    buttonRow->addStretch();
    buttonRow->addWidget(m_closeButton);
    layout->addLayout(buttonRow);

    connect(m_browseButton, &QPushButton::clicked,
            this, &GitEnvironmentDialog::browseGitPath);
    connect(m_redetectButton, &QPushButton::clicked,
            this, &GitEnvironmentDialog::reDetect);
    connect(m_closeButton, &QPushButton::clicked,
            this, &GitEnvironmentDialog::accept);

    refresh();
}

void GitEnvironmentDialog::refresh() {
    reDetect();
}

void GitEnvironmentDialog::accept() {
    if (m_detectionInProgress) {
        return;
    }
    QDialog::accept();
}

void GitEnvironmentDialog::commitSelection(
    const QString &previousGitPath) const {
    const QString validated = m_validatedPath.trimmed();
    if (result() == QDialog::Accepted && !validated.isEmpty()) {
        GitCommandExecutor::setDefaultGitExecutable(validated);
        return;
    }
    m_service->setGitPath(previousGitPath);
}

void GitEnvironmentDialog::applyResult(
    const GitEnvironmentService::GitEnvironment &env) {
    // v4-P6-21R6 P1-02: the candidate only becomes a VALIDATED path
    // when real detection succeeded on it.
    m_validatedPath = (env.available && !env.gitPath.isEmpty())
        ? env.gitPath
        : QString();
    if (env.available) {
        m_statusLabel->setText(QStringLiteral("检测状态：Git 可用"));
        m_statusLabel->setStyleSheet("color: #16A34A; font-weight: 600;");
    } else {
        m_statusLabel->setText(QStringLiteral("检测状态：Git 不可用"));
        m_statusLabel->setStyleSheet("color: #DC2626; font-weight: 600;");
    }
    m_pathLabel->setText(QStringLiteral("路径：%1").arg(
        env.gitPath.isEmpty() ? QStringLiteral("（自动检测）") : env.gitPath));
    m_versionLabel->setText(
        QStringLiteral("版本：%1").arg(
            env.version.isEmpty() ? QStringLiteral("未知") : env.version));
    if (!env.available && !env.error.isEmpty()) {
        m_versionLabel->setText(
            m_versionLabel->text() + QStringLiteral("\n错误：%1").arg(env.error));
    }
}

void GitEnvironmentDialog::browseGitPath() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择 Git 可执行文件"), QString(),
        QStringLiteral("git.exe;;所有文件 (*)"));
    if (path.isEmpty()) {
        return;
    }
    m_customPath = path;
    reDetect();
}

void GitEnvironmentDialog::reDetect() {
    // v4-P6-11: visible busy state FIRST, then run the (synchronous)
    // detection on the next event-loop tick so the dialog actually
    // repaints "检测中…" - previously the sync call finished in the
    // same tick and the click looked like it did nothing.
    // A previous successful candidate must not remain committable while a
    // newer candidate is still pending or has not passed detection.
    m_validatedPath.clear();
    m_detectionInProgress = true;
    m_redetectButton->setEnabled(false);
    m_closeButton->setEnabled(false);
    m_redetectButton->setText(QStringLiteral("检测中…"));
    m_statusLabel->setText(QStringLiteral("正在检测 Git 环境…"));
    m_statusLabel->setStyleSheet("color: #2563EB; font-weight: 600;");
    const QString path = m_customPath;
    QTimer::singleShot(0, this, [this, path]() {
        const GitEnvironmentService::GitEnvironment env =
            m_service->detectSync(path);
        applyResult(env);
        m_detectionInProgress = false;
        m_redetectButton->setEnabled(true);
        m_closeButton->setEnabled(true);
        m_redetectButton->setText(QStringLiteral("重新检测"));
        // Visible completion feedback even when nothing changed.
        m_statusLabel->setText(
            m_statusLabel->text()
            + QStringLiteral(" · 已完成检测 %1")
                  .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss"))));
        if (!env.available && !env.error.isEmpty()) {
            QMessageBox::warning(
                this, QStringLiteral("Git 环境检测"),
                QStringLiteral("未检测到可用的 Git：\n%1").arg(env.error));
        }
    });
}
