#include "GitHubReleaseService.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

GitHubReleaseService::GitHubReleaseService(QObject *parent)
    : QObject(parent) {
}

void GitHubReleaseService::setNetworkManager(QNetworkAccessManager *manager) {
    if (m_ownsManager && m_manager) {
        m_manager->deleteLater();
    }
    m_manager = manager;
    m_ownsManager = false;
}

QNetworkAccessManager *GitHubReleaseService::ensureManager() {
    if (!m_manager) {
        m_manager = new QNetworkAccessManager(this);
        m_ownsManager = true;
    }
    return m_manager;
}

GitHubReleaseService::ErrorKind GitHubReleaseService::classifyStatus(
    int status) {
    if (status == 401 || status == 403) return ErrorKind::Forbidden;
    if (status == 404) return ErrorKind::RepositoryMissing;
    if (status >= 400) return ErrorKind::Other;
    return ErrorKind::None;
}

QNetworkRequest GitHubReleaseService::makeRequest(const QUrl &url,
                                                  const QString &token,
                                                  const QByteArray &body) {
    QNetworkRequest request(url);
    // v4-P4-4: token always from the account session (never hardcoded).
    request.setRawHeader("Authorization",
                         ("Bearer " + token.trimmed()).toUtf8());
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "QtGitStudio");
    if (!body.isEmpty()) {
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          "application/json");
    }
    return request;
}

QList<GitHubRelease> GitHubReleaseService::parseReleases(
    const QByteArray &json, QString *error) {
    QList<GitHubRelease> out;
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
        GitHubRelease release;
        release.id = obj.value(QStringLiteral("id")).toDouble(-1);
        release.tagName = obj.value(QStringLiteral("tag_name")).toString();
        release.name = obj.value(QStringLiteral("name")).toString();
        release.body = obj.value(QStringLiteral("body")).toString();
        release.htmlUrl = obj.value(QStringLiteral("html_url")).toString();
        release.draft = obj.value(QStringLiteral("draft")).toBool(false);
        release.prerelease =
            obj.value(QStringLiteral("prerelease")).toBool(false);
        if (release.id > 0 && !release.tagName.isEmpty()) {
            out.append(release);
        }
    }
    return out;
}

void GitHubReleaseService::fetchReleases(const QString &token,
                                         const QString &owner,
                                         const QString &repo,
                                         ListCompletion completion) {
    if (token.trimmed().isEmpty()) {
        ListResult out;
        out.success = false;
        out.kind = ErrorKind::TokenMissing;
        out.error = QStringLiteral("请先登录 GitHub（缺少访问令牌）");
        completion(out);
        return;
    }
    const QUrl url(QStringLiteral("https://api.github.com/repos/%1/%2/releases")
                       .arg(owner, repo));
    QNetworkReply *reply = ensureManager()->get(makeRequest(url, token));
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
            out.releases = parseReleases(body, &parseErr);
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

void GitHubReleaseService::createRelease(const QString &token,
                                         const QString &owner,
                                         const QString &repo,
                                         const QString &tagName,
                                         const QString &name,
                                         const QString &body,
                                         bool draft, bool prerelease,
                                         CreateCompletion completion) {
    if (token.trimmed().isEmpty()) {
        CreateResult out;
        out.success = false;
        out.kind = ErrorKind::TokenMissing;
        out.error = QStringLiteral("请先登录 GitHub（缺少访问令牌）");
        completion(out);
        return;
    }
    if (tagName.trimmed().isEmpty()) {
        CreateResult out;
        out.success = false;
        out.kind = ErrorKind::Other;
        out.error = QStringLiteral("请选择或输入标签名称");
        completion(out);
        return;
    }
    QJsonObject payload;
    payload.insert(QStringLiteral("tag_name"), tagName.trimmed());
    payload.insert(QStringLiteral("name"),
                   name.trimmed().isEmpty() ? tagName.trimmed()
                                            : name.trimmed());
    payload.insert(QStringLiteral("body"), body);
    payload.insert(QStringLiteral("draft"), draft);
    payload.insert(QStringLiteral("prerelease"), prerelease);
    const QByteArray payloadJson = QJsonDocument(payload).toJson(
        QJsonDocument::Compact);

    const QUrl url(QStringLiteral("https://api.github.com/repos/%1/%2/releases")
                       .arg(owner, repo));
    QNetworkReply *reply = ensureManager()->post(
        makeRequest(url, token, payloadJson), payloadJson);
    connect(reply, &QNetworkReply::finished, this, [this, reply, completion]() {
        CreateResult out;
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
            const QList<GitHubRelease> parsed =
                parseReleases(QByteArray("[" + body + "]"), &parseErr);
            if (!parsed.isEmpty()) {
                out.release = parsed.first();
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

void GitHubReleaseService::deleteRelease(const QString &token,
                                         const QString &owner,
                                         const QString &repo,
                                         long long releaseId,
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
                       "https://api.github.com/repos/%1/%2/releases/%3")
                       .arg(owner, repo)
                       .arg(releaseId));
    QNetworkReply *reply = ensureManager()->deleteResource(
        makeRequest(url, token));
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
