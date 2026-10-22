// v4-P2-4 GitHub Repository Integration (lightweight): lists the logged
// in user's repositories via GET /user/repos. Parsing is a pure function
// (unit-testable without network); the HTTP layer is injectable so tests
// never touch the real GitHub API.
#ifndef GITHUBREPOSITORYSERVICE_H
#define GITHUBREPOSITORYSERVICE_H

#include <QObject>
#include <QString>
#include <QList>
#include <functional>

class QNetworkAccessManager;
class QByteArray;

struct GitHubRepository {
    QString name;
    QString owner;
    QString cloneUrl;
};

class GitHubRepositoryService : public QObject {
    Q_OBJECT
public:
    struct FetchResult {
        bool success = false;
        QList<GitHubRepository> repositories;
        QString error;
        bool unauthenticated = false;  // 401 / missing token
    };
    using FetchCompletion = std::function<void(const FetchResult &)>;

    explicit GitHubRepositoryService(QObject *parent = nullptr);

    // Injectable HTTP backend (tests use a fake manager).
    void setNetworkManager(QNetworkAccessManager *manager);

    // GET /user/repos with `token`. Empty token -> immediate
    // unauthenticated result (no HTTP call).
    void fetchRepos(const QString &token, FetchCompletion completion);

    // Pure JSON parser: [{"name":..,"owner":{"login":..},"clone_url":..}]
    static QList<GitHubRepository> parseRepos(const QByteArray &json,
                                              QString *error = nullptr);

private:
    QNetworkAccessManager *m_manager = nullptr;
    bool m_ownsManager = false;
};

#endif // GITHUBREPOSITORYSERVICE_H
