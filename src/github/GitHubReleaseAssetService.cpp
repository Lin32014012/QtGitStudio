#include "GitHubReleaseAssetService.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <QUrlQuery>

GitHubReleaseAssetService::GitHubReleaseAssetService(QObject *parent)
    : QObject(parent) {
}

void GitHubReleaseAssetService::setNetworkManager(
    QNetworkAccessManager *manager) {
    if (m_ownsManager && m_manager) {
        m_manager->deleteLater();
    }
    m_manager = manager;
    m_ownsManager = false;
}

QNetworkAccessManager *GitHubReleaseAssetService::ensureManager() {
    if (!m_manager) {
        m_manager = new QNetworkAccessManager(this);
        m_ownsManager = true;
    }
    return m_manager;
}

GitHubReleaseAssetService::ErrorKind GitHubReleaseAssetService::classifyStatus(
    int status) {
    if (status == 401 || status == 403) return ErrorKind::Forbidden;
    if (status == 404) return ErrorKind::RepositoryMissing;
    if (status >= 400) return ErrorKind::Other;
    return ErrorKind::None;
}

QNetworkRequest GitHubReleaseAssetService::makeRequest(
    const QUrl &url, const QString &token, const QByteArray &contentType) {
    QNetworkRequest request(url);
    // v4-P5-1: token always from the account session (never hardcoded).
    request.setRawHeader("Authorization",
                         ("Bearer " + token.trimmed()).toUtf8());
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "QtGitStudio");
    if (!contentType.isEmpty()) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    }
    return request;
}

QList<GitHubReleaseAsset> GitHubReleaseAssetService::parseAssets(
    const QByteArray &json, QString *error) {
    QList<GitHubReleaseAsset> out;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) {
            *error = QStringLiteral("响应解析失败：%1")
                         .arg(parseError.errorString());
        }
        return out;
    }
    if (!doc.isArray()) {
        if (error) *error = QStringLiteral("响应格式不是数组");
        return out;
    }
    const QJsonArray array = doc.array();
    for (const QJsonValue &value : array) {
        if (!value.isObject()) continue;
        const QJsonObject obj = value.toObject();
        GitHubReleaseAsset asset;
        asset.id = obj.value(QStringLiteral("id")).toDouble(-1);
        asset.name = obj.value(QStringLiteral("name")).toString();
        asset.size = obj.value(QStringLiteral("size")).toDouble(0);
        asset.browserDownloadUrl =
            obj.value(QStringLiteral("browser_download_url")).toString();
        asset.contentType =
            obj.value(QStringLiteral("content_type")).toString();
        if (asset.id > 0 && !asset.name.isEmpty()) {
            out.append(asset);
        }
    }
    return out;
}

void GitHubReleaseAssetService::fetchAssets(const QString &token,
                                            const QString &owner,
                                            const QString &repo,
                                            long long releaseId,
                                            ListCompletion completion) {
    if (token.trimmed().isEmpty()) {
        ListResult out;
        out.success = false;
        out.kind = ErrorKind::TokenMissing;
        out.error = QStringLiteral("请先登录 GitHub（缺少访问令牌）");
        completion(out);
        return;
    }
    const QUrl url(QStringLiteral(
                       "https://api.github.com/repos/%1/%2/releases/%3/assets")
                       .arg(owner, repo)
                       .arg(releaseId));
    QNetworkReply *reply = ensureManager()->get(makeRequest(url, token, {}));
    connect(reply, &QNetworkReply::finished, this, [this, reply, completion]() {
        ListResult out;
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        out.kind = classifyStatus(status);
        if (reply->error() != QNetworkReply::NoError
            && out.kind == ErrorKind::None) {
            out.success = false;
            out.kind = ErrorKind::Network;
            out.error = QStringLiteral("网络错误：%1").arg(reply->errorString());
        } else if (out.kind != ErrorKind::None) {
            out.success = false;
            switch (out.kind) {
            case ErrorKind::Forbidden:
                out.error = QStringLiteral("GitHub 权限不足（403），请检查令牌权限");
                break;
            case ErrorKind::RepositoryMissing:
                out.error = QStringLiteral("仓库不存在或无权访问（404）");
                break;
            default:
                out.error = QStringLiteral("GitHub API 错误（HTTP %1）")
                                .arg(status);
                break;
            }
        } else {
            const QByteArray body = reply->readAll();
            QString parseErr;
            out.assets = parseAssets(body, &parseErr);
            out.success = true;
            if (!parseErr.isEmpty()) {
                out.success = false;
                out.kind = ErrorKind::Other;
                out.error = parseErr;
            }
        }
        reply->deleteLater();
        completion(out);
    });
}

void GitHubReleaseAssetService::uploadAsset(const QString &token,
                                            const QString &owner,
                                            const QString &repo,
                                            long long releaseId,
                                            const QString &filePath,
                                            ProgressCallback progress,
                                            UploadCompletion completion) {
    if (token.trimmed().isEmpty()) {
        UploadResult out;
        out.success = false;
        out.kind = ErrorKind::TokenMissing;
        out.error = QStringLiteral("请先登录 GitHub（缺少访问令牌）");
        completion(out);
        return;
    }
    QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        UploadResult out;
        out.success = false;
        out.kind = ErrorKind::FileMissing;
        out.error = QStringLiteral("文件不存在：%1").arg(filePath);
        completion(out);
        return;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        UploadResult out;
        out.success = false;
        out.kind = ErrorKind::FileMissing;
        out.error = QStringLiteral("无法读取文件：%1").arg(filePath);
        completion(out);
        return;
    }
    const QByteArray content = file.readAll();
    file.close();

    // uploads.github.com endpoint with the name query parameter.
    QUrl url(QStringLiteral(
                 "https://uploads.github.com/repos/%1/%2/releases/%3/assets")
                 .arg(owner, repo)
                 .arg(releaseId));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("name"), info.fileName());
    url.setQuery(query);

    QNetworkReply *reply = ensureManager()->post(
        makeRequest(url, token, "application/octet-stream"), content);
    if (progress) {
        connect(reply, &QNetworkReply::uploadProgress, this,
                [progress](qint64 sent, qint64 total) {
                    progress(sent, total);
                });
    }
    connect(reply, &QNetworkReply::finished, this, [this, reply, completion]() {
        UploadResult out;
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        out.kind = classifyStatus(status);
        if (reply->error() != QNetworkReply::NoError
            && out.kind == ErrorKind::None) {
            out.success = false;
            out.kind = ErrorKind::Network;
            out.error = QStringLiteral("网络错误：%1").arg(reply->errorString());
        } else if (out.kind != ErrorKind::None) {
            out.success = false;
            switch (out.kind) {
            case ErrorKind::Forbidden:
                out.error = QStringLiteral("GitHub 权限不足（403），请检查令牌权限");
                break;
            case ErrorKind::RepositoryMissing:
                out.error = QStringLiteral("仓库不存在或无权访问（404）");
                break;
            default:
                out.error = QStringLiteral("GitHub API 错误（HTTP %1）")
                                .arg(status);
                break;
            }
        } else {
            const QByteArray body = reply->readAll();
            QString parseErr;
            // Upload response is a single asset object.
            const QList<GitHubReleaseAsset> parsed =
                parseAssets(QByteArray("[" + body + "]"), &parseErr);
            if (!parsed.isEmpty()) {
                out.asset = parsed.first();
            }
            out.success = true;
            if (!parseErr.isEmpty()) {
                out.success = false;
                out.kind = ErrorKind::Other;
                out.error = parseErr;
            }
        }
        reply->deleteLater();
        completion(out);
    });
}

void GitHubReleaseAssetService::deleteAsset(const QString &token,
                                            const QString &owner,
                                            const QString &repo,
                                            long long assetId,
                                            DeleteCompletion completion) {
    if (token.trimmed().isEmpty()) {
        DeleteResult out;
        out.success = false;
        out.kind = ErrorKind::TokenMissing;
        out.error = QStringLiteral("请先登录 GitHub（缺少访问令牌）");
        completion(out);
        return;
    }
    const QUrl url(QStringLiteral(
                       "https://api.github.com/repos/%1/%2/releases/assets/%3")
                       .arg(owner, repo)
                       .arg(assetId));
    QNetworkReply *reply = ensureManager()->deleteResource(
        makeRequest(url, token, {}));
    connect(reply, &QNetworkReply::finished, this, [this, reply, completion]() {
        DeleteResult out;
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        out.kind = classifyStatus(status);
        if (reply->error() != QNetworkReply::NoError
            && out.kind == ErrorKind::None) {
            out.success = false;
            out.kind = ErrorKind::Network;
            out.error = QStringLiteral("网络错误：%1").arg(reply->errorString());
        } else if (out.kind != ErrorKind::None) {
            out.success = false;
            switch (out.kind) {
            case ErrorKind::Forbidden:
                out.error = QStringLiteral("GitHub 权限不足（403），请检查令牌权限");
                break;
            case ErrorKind::RepositoryMissing:
                out.error = QStringLiteral("仓库不存在或无权访问（404）");
                break;
            default:
                out.error = QStringLiteral("GitHub API 错误（HTTP %1）")
                                .arg(status);
                break;
            }
        } else {
            // 204 No Content on success.
            out.success = (status == 204 || status == 200);
            if (!out.success) {
                out.kind = ErrorKind::Other;
                out.error = QStringLiteral("GitHub API 错误（HTTP %1）")
                                .arg(status);
            }
        }
        reply->deleteLater();
        completion(out);
    });
}
