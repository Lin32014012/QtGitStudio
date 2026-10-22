// v4-P5-1 GitHub Release Asset management MVP: list / upload / delete
// release assets.
//   - list:   GET    /repos/{owner}/{repo}/releases/{id}/assets
//   - upload: POST   uploads.github.com/repos/{owner}/{repo}/releases/{id}/assets?name=file
//   - delete: DELETE /repos/{owner}/{repo}/releases/assets/{asset_id}
// Upload uses the uploads.github.com endpoint with
// Authorization: Bearer <token> and Content-Type: application/octet-stream.
// The token always comes from the EXISTING GitHubAccountService session
// (never hardcoded). File reading is done up front (missing file is a
// clean error before any HTTP call). No batch upload / resume.
#ifndef GITHUBRELEASEASSETSERVICE_H
#define GITHUBRELEASEASSETSERVICE_H

#include <QObject>
#include <QString>
#include <QList>
#include <QByteArray>
#include <functional>

class QNetworkAccessManager;
class QNetworkRequest;
class QNetworkReply;

struct GitHubReleaseAsset {
    long long id = 0;
    QString name;
    long long size = 0;
    QString browserDownloadUrl;
    QString contentType;
};

class GitHubReleaseAssetService : public QObject {
    Q_OBJECT
public:
    enum class ErrorKind {
        None,
        TokenMissing,      // empty token (no HTTP call made)
        FileMissing,       // local file does not exist (no HTTP call)
        Forbidden,         // 401 / 403
        Network,           // transport failure
        RepositoryMissing, // 404
        Other,
    };

    struct ListResult {
        bool success = false;
        QList<GitHubReleaseAsset> assets;
        QString error;
        ErrorKind kind = ErrorKind::None;
    };
    using ListCompletion = std::function<void(const ListResult &)>;

    struct UploadResult {
        bool success = false;
        GitHubReleaseAsset asset;
        QString error;
        ErrorKind kind = ErrorKind::None;
    };
    using UploadCompletion = std::function<void(const UploadResult &)>;
    using ProgressCallback = std::function<void(qint64 sent, qint64 total)>;

    struct DeleteResult {
        bool success = false;
        QString error;
        ErrorKind kind = ErrorKind::None;
    };
    using DeleteCompletion = std::function<void(const DeleteResult &)>;

    explicit GitHubReleaseAssetService(QObject *parent = nullptr);

    // Injectable HTTP backend (tests use a fake manager).
    void setNetworkManager(QNetworkAccessManager *manager);

    // GET the asset list of a release.
    void fetchAssets(const QString &token, const QString &owner,
                     const QString &repo, long long releaseId,
                     ListCompletion completion);

    // POST the local file to uploads.github.com. Missing file -> clean
    // FileMissing error before any HTTP call. Progress (when emitted by
    // the backend) is forwarded to `progress`.
    void uploadAsset(const QString &token, const QString &owner,
                     const QString &repo, long long releaseId,
                     const QString &filePath, ProgressCallback progress,
                     UploadCompletion completion);

    // DELETE an asset by id.
    void deleteAsset(const QString &token, const QString &owner,
                     const QString &repo, long long assetId,
                     DeleteCompletion completion);

    // Pure JSON parser for the assets array:
    // [{"id":..,"name":..,"size":..,"browser_download_url":..,
    //   "content_type":..}]
    static QList<GitHubReleaseAsset> parseAssets(const QByteArray &json,
                                                 QString *error = nullptr);

private:
    static ErrorKind classifyStatus(int status);
    QNetworkAccessManager *ensureManager();
    static QNetworkRequest makeRequest(const QUrl &url, const QString &token,
                                       const QByteArray &contentType);

    QNetworkAccessManager *m_manager = nullptr;
    bool m_ownsManager = false;
};

#endif // GITHUBRELEASEASSETSERVICE_H
