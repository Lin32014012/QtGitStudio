// v4-P4-4 GitHub Release Management MVP: list / create / delete GitHub
// releases via the REST API.
//   - list:   GET  /repos/{owner}/{repo}/releases
//   - create: POST /repos/{owner}/{repo}/releases
//   - delete: DELETE /repos/{owner}/{repo}/releases/{id}
// The token always comes from the EXISTING GitHubAccountService session
// (never hardcoded). Parsing is a pure function; the HTTP layer is
// injectable so tests never touch the real GitHub API.
// Uploading assets / Actions / CI / auto release notes are OUT of scope.
#ifndef GITHUBRELEASESERVICE_H
#define GITHUBRELEASESERVICE_H

#include <QObject>
#include <QString>
#include <QList>
#include <functional>

class QNetworkAccessManager;
class QNetworkRequest;
class QByteArray;

struct GitHubRelease {
    long long id = 0;
    QString tagName;
    QString name;
    QString body;
    QString htmlUrl;
    bool draft = false;
    bool prerelease = false;
};

class GitHubReleaseService : public QObject {
    Q_OBJECT
public:
    enum class ErrorKind {
        None,
        TokenMissing,      // empty token (no HTTP call made)
        Forbidden,         // 401 / 403
        Network,           // transport failure
        RepositoryMissing, // 404
        Other,
    };

    struct ListResult {
        bool success = false;
        QList<GitHubRelease> releases;
        QString error;
        ErrorKind kind = ErrorKind::None;
    };
    using ListCompletion = std::function<void(const ListResult &)>;

    struct CreateResult {
        bool success = false;
        GitHubRelease release;
        QString error;
        ErrorKind kind = ErrorKind::None;
    };
    using CreateCompletion = std::function<void(const CreateResult &)>;

    struct DeleteResult {
        bool success = false;
        QString error;
        ErrorKind kind = ErrorKind::None;
    };
    using DeleteCompletion = std::function<void(const DeleteResult &)>;

    explicit GitHubReleaseService(QObject *parent = nullptr);

    // Injectable HTTP backend (tests use a fake manager).
    void setNetworkManager(QNetworkAccessManager *manager);

    // GET /repos/{owner}/{repo}/releases with `token`.
    void fetchReleases(const QString &token, const QString &owner,
                       const QString &repo, ListCompletion completion);

    // POST /repos/{owner}/{repo}/releases.
    void createRelease(const QString &token, const QString &owner,
                       const QString &repo, const QString &tagName,
                       const QString &name, const QString &body,
                       bool draft, bool prerelease,
                       CreateCompletion completion);

    // DELETE /repos/{owner}/{repo}/releases/{id}.
    void deleteRelease(const QString &token, const QString &owner,
                       const QString &repo, long long releaseId,
                       DeleteCompletion completion);

    // Pure JSON parser for the releases array:
    // [{"id":..,"tag_name":..,"name":..,"body":..,"html_url":..,
    //   "draft":..,"prerelease":..}]
    static QList<GitHubRelease> parseReleases(const QByteArray &json,
                                              QString *error = nullptr);

private:
    static ErrorKind classifyStatus(int status);
    QNetworkAccessManager *ensureManager();
    static QNetworkRequest makeRequest(const QUrl &url, const QString &token,
                                       const QByteArray &body = QByteArray());

    QNetworkAccessManager *m_manager = nullptr;
    bool m_ownsManager = false;
};

#endif // GITHUBRELEASESERVICE_H
