#include "GitHubRepositoryService.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

GitHubRepositoryService::GitHubRepositoryService(QObject *parent)
    : QObject(parent) {
}

void GitHubRepositoryService::setNetworkManager(QNetworkAccessManager *manager) {
    if (m_ownsManager && m_manager) {
        m_manager->deleteLater();
    }
    m_manager = manager;
    m_ownsManager = false;
}

QList<GitHubRepository> GitHubRepositoryService::parseRepos(
    const QByteArray &json, QString *error) {
    QList<GitHubRepository> out;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) *error = QStringLiteral("响应解析失败：%1")
                                .arg(parseError.errorString());
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
        GitHubRepository repo;
        repo.name = obj.value(QStringLiteral("name")).toString();
        repo.owner = obj.value(QStringLiteral("owner"))
                         .toObject()
                         .value(QStringLiteral("login"))
                         .toString();
        repo.cloneUrl = obj.value(QStringLiteral("clone_url")).toString();
        if (!repo.name.isEmpty() && !repo.cloneUrl.isEmpty()) {
            out.append(repo);
        }
    }
    return out;
}

void GitHubRepositoryService::fetchRepos(const QString &token,
                                         FetchCompletion completion) {
    if (token.trimmed().isEmpty()) {
        FetchResult out;
        out.success = false;
        out.unauthenticated = true;
        out.error = QStringLiteral("请先登录 GitHub");
        completion(out);
        return;
    }
    if (!m_manager) {
        m_manager = new QNetworkAccessManager(this);
        m_ownsManager = true;
    }

    QNetworkRequest request(
        QUrl(QStringLiteral("https://api.github.com/user/repos?per_page=100")));
    // v4-P2-5: Bearer scheme (matches GET /user verification).
    request.setRawHeader("Authorization",
                         ("Bearer " + token.trimmed()).toUtf8());
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "QtGitStudio");

    QNetworkReply *reply = m_manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, completion]() {
        FetchResult out;
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 401 || status == 403) {
            out.success = false;
            out.unauthenticated = true;
            out.error = QStringLiteral("GitHub 认证失败，请重新登录");
        } else if (reply->error() != QNetworkReply::NoError) {
            out.success = false;
            out.error = QStringLiteral("网络错误：%1").arg(reply->errorString());
        } else {
            const QByteArray body = reply->readAll();
            QString parseErr;
            out.repositories = parseRepos(body, &parseErr);
            out.success = true;
            if (parseErr.isEmpty() && out.repositories.isEmpty()) {
                // 200 with empty list is a valid "no repositories" state.
            }
        }
        reply->deleteLater();
        completion(out);
    });
}
