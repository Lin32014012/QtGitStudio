// v4-P2-2 GitHub account settings dialog: shows login state (未连接 /
// 已连接 + username) with [连接GitHub] / [退出登录] actions. Only state
// management - no OAuth flow, no API calls.
#ifndef GITHUBACCOUNTDIALOG_H
#define GITHUBACCOUNTDIALOG_H

#include <QDialog>

class QLabel;
class QPushButton;
class QLineEdit;
class GitHubAccountService;
class ProjectController;

class GitHubAccountDialog : public QDialog {
    Q_OBJECT
public:
    explicit GitHubAccountDialog(GitHubAccountService *service,
                                 ProjectController *projectController,
                                 QWidget *parent = nullptr);

private slots:
    void connectAccount();
    void disconnectAccount();
    void onStateChanged();
    void openRepositoryList();

private:
    void refreshUi();

    GitHubAccountService *m_service;
    ProjectController *m_projectController;
    QLabel *m_statusLabel;
    QLabel *m_usernameLabel;
    QPushButton *m_connectButton;
    QPushButton *m_disconnectButton;
    QPushButton *m_reposButton;
};

#endif // GITHUBACCOUNTDIALOG_H
