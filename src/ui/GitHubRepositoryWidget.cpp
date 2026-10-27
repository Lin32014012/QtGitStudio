#include "GitHubRepositoryWidget.h"
#include "Theme.h"
#include "GitHubAccountService.h"
#include "ProjectController.h"

#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QFileDialog>

GitHubRepositoryWidget::GitHubRepositoryWidget(
    GitHubAccountService *accountService,
    ProjectController *projectController,
    QWidget *parent)
    : QWidget(parent)
    , m_accountService(accountService)
    , m_projectController(projectController) {
    auto *title = new QLabel(QStringLiteral("我的GitHub仓库列表"), this);
    title->setProperty("role", QStringLiteral("sectionTitle"));


    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName("githubReposStatus");
    m_statusLabel->setWordWrap(true);

    m_listWidget = new QListWidget(this);
    m_listWidget->setObjectName("githubRepoList");

    m_refreshButton = new QPushButton(QStringLiteral("刷新列表"), this);
    m_refreshButton->setObjectName("githubReposRefresh");

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(title);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_listWidget, 1);
    layout->addWidget(m_refreshButton, 0, Qt::AlignRight);

    connect(m_refreshButton, &QPushButton::clicked,
            this, &GitHubRepositoryWidget::refresh);
    connect(m_listWidget, &QListWidget::itemDoubleClicked,
            this, &GitHubRepositoryWidget::onCloneClicked);
    connect(m_accountService, &GitHubAccountService::stateChanged,
            this, &GitHubRepositoryWidget::onAccountStateChanged);

    refresh();
}

void GitHubRepositoryWidget::showLoggedOut() {
    m_statusLabel->setText(QStringLiteral("请先登录GitHub"));
    m_statusLabel->setProperty("role", QStringLiteral("statusText"));
    m_statusLabel->setProperty("state", QStringLiteral("danger"));

    m_listWidget->clear();
}

void GitHubRepositoryWidget::onAccountStateChanged() {
    refresh();
}

void GitHubRepositoryWidget::renderRepositories(
    const QList<GitHubRepository> &repos) {
    m_listWidget->clear();
    for (const GitHubRepository &repo : repos) {
        auto *item = new QListWidgetItem(
            QStringLiteral("%1/%2\n克隆地址：%3")
                .arg(repo.owner, repo.name, repo.cloneUrl));
        item->setData(Qt::UserRole, repo.cloneUrl);
        item->setData(Qt::UserRole + 1, repo.name);
        item->setToolTip(repo.cloneUrl);
        m_listWidget->addItem(item);
    }
    m_statusLabel->setText(
        QStringLiteral("共 %1 个仓库（双击条目或使用右侧按钮克隆）")
            .arg(repos.size()));
    m_statusLabel->setProperty("role", QStringLiteral("statusText"));
    m_statusLabel->setProperty("state", QStringLiteral("ok"));

}

void GitHubRepositoryWidget::refresh() {
    if (!m_accountService->isLoggedIn()) {
        showLoggedOut();
        return;
    }
    m_statusLabel->setText(QStringLiteral("正在加载仓库列表…"));
    m_statusLabel->setProperty("role", QStringLiteral("statusText"));
    m_statusLabel->setProperty("state", QStringLiteral("plain"));

    m_repoService.fetchRepos(
        m_accountService->token(),
        [this](const GitHubRepositoryService::FetchResult &result) {
            if (!result.success) {
                m_statusLabel->setText(result.error);
                m_statusLabel->setProperty("role", QStringLiteral("statusText"));
                m_statusLabel->setProperty("state", QStringLiteral("danger"));

                m_listWidget->clear();
                return;
            }
            renderRepositories(result.repositories);
        });
}

void GitHubRepositoryWidget::onCloneClicked() {
    QListWidgetItem *item = m_listWidget->currentItem();
    if (!item) {
        return;
    }
    const QString cloneUrl = item->data(Qt::UserRole).toString();
    const QString repoName = item->data(Qt::UserRole + 1).toString();
    if (cloneUrl.isEmpty()) {
        return;
    }
    const QString targetParent = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择克隆目标目录（将创建 %1 子目录）")
                  .arg(repoName));
    if (targetParent.isEmpty()) {
        return;
    }
    const QString targetPath = targetParent + "/" + repoName;
    const ProjectController::CloneResult result =
        m_projectController->cloneRepositoryDetailed(cloneUrl, targetPath);
    if (!result.success) {
        QMessageBox::warning(this, QStringLiteral("克隆失败"),
                             result.summary);
        return;
    }
    emit repositoryCloned(targetPath);
    QMessageBox::information(this, QStringLiteral("克隆成功"),
                             QStringLiteral("已克隆到：%1").arg(targetPath));
}
