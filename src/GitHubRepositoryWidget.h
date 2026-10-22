// v4-P2-4 GitHub repository list widget. Shows 我的GitHub仓库列表
// (name / clone address / Clone button). Clone reuses the existing
// ProjectController clone API - never reimplements cloning. Logged-out
// state shows 请先登录GitHub.
#ifndef GITHUBREPOSITORYWIDGET_H
#define GITHUBREPOSITORYWIDGET_H

#include <QWidget>
#include "GitHubRepositoryService.h"

class QLabel;
class QListWidget;
class QPushButton;
class ProjectController;
class GitHubAccountService;

class GitHubRepositoryWidget : public QWidget {
    Q_OBJECT
public:
    explicit GitHubRepositoryWidget(GitHubAccountService *accountService,
                                    ProjectController *projectController,
                                    QWidget *parent = nullptr);

    // Fetch and render the list (logged-in only).
    void refresh();

signals:
    void repositoryCloned(const QString &path);

private slots:
    void onCloneClicked();
    void onAccountStateChanged();

private:
    void showLoggedOut();
    void renderRepositories(const QList<GitHubRepository> &repos);

    GitHubAccountService *m_accountService;
    ProjectController *m_projectController;
    GitHubRepositoryService m_repoService;
    QLabel *m_statusLabel;
    QListWidget *m_listWidget;
    QPushButton *m_refreshButton;
};

#endif // GITHUBREPOSITORYWIDGET_H
