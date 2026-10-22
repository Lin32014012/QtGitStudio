#include "GitHubAccountService.h"
#include "CredentialStore.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>

GitHubAccountService::GitHubAccountService(QObject *parent)
    : QObject(parent) {
}

void GitHubAccountService::setNetworkManager(QNetworkAccessManager *manager) {
    if (m_ownsManager && m_manager) {
        m_manager->deleteLater();
    }
    m_manager = manager;
    m_ownsManager = false;
}

void GitHubAccountService::setCredentialStore(CredentialStore *store) {
    m_store = store;
}

void GitHubAccountService::restore() {
    if (!m_store) {
        return;  // no backend: stay logged out, no crash
    }
    QString storedUser;
    QString storedToken;
    if (m_store->read(&storedUser, &storedToken) && !storedToken.isEmpty()) {
        m_state = AccountState::LoggedIn;
        m_username = storedUser;
        m_token = storedToken;
        emit stateChanged();
    }
}

bool GitHubAccountService::login(const QString &username, const QString &token) {
    if (token.trimmed().isEmpty()) {
        return false;
    }
    // v4-P6-21 ISSUE-07: persistence is part of the login transaction.
    // A failed save must not commit a LoggedIn state the user believes
    // survives restarts - surface the failure instead.
    if (m_store && !m_store->save(username, token)) {
        return false;
    }
    m_state = AccountState::LoggedIn;
    m_username = username;
    m_token = token;
    emit stateChanged();
    return true;
}

bool GitHubAccountService::logout() {
    // v4-P6-21 ISSUE-07: only commit LoggedOut after the stored
    // credential was really removed - never claim deletion on failure.
    if (m_store && !m_store->remove()) {
        return false;
    }
    m_state = AccountState::LoggedOut;
    m_username.clear();
    m_token.clear();
    emit stateChanged();
    return true;
}

QString GitHubAccountService::token() const {
    if (m_state != AccountState::LoggedIn) {
        return QString();
    }
    if (!m_token.isEmpty()) {
        return m_token;
    }
    // Fall back to the store (read failure returns empty - no crash).
    if (m_store) {
        QString user;
        QString storedToken;
        if (m_store->read(&user, &storedToken)) {
            return storedToken;
        }
    }
    return QString();
}

void GitHubAccountService::verifyToken(const QString &token,
                                       VerifyCompletion completion) {
    if (token.trimmed().isEmpty()) {
        VerifyResult out;
        out.success = false;
        out.error = QStringLiteral("Token 不能为空");
        completion(out);
        return;
    }
    if (!m_manager) {
        m_manager = new QNetworkAccessManager(this);
        m_ownsManager = true;
    }

    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/user")));
    request.setRawHeader("Authorization",
                         ("Bearer " + token.trimmed()).toUtf8());
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "QtGitStudio");

    QNetworkReply *reply = m_manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, token, completion]() {
        VerifyResult out;
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200) {
            const QJsonDocument doc =
                QJsonDocument::fromJson(reply->readAll());
            out.username = doc.object().value(QStringLiteral("login")).toString();
            if (!out.username.isEmpty()) {
                // Persist the verified session (username comes from the
                // API, not from user input).
                // v4-P6-21 ISSUE-07: if the credential cannot be saved
                // the login must not be committed - surface the reason.
                if (login(out.username, token.trimmed())) {
                    out.success = true;
                } else {
                    out.error = QStringLiteral(
                        "GitHub 验证通过，但无法保存凭据到系统凭据管理器。");
                }
            } else {
                out.error = QStringLiteral("响应中缺少用户名");
            }
        } else if (status == 401 || status == 403) {
            out.error = QStringLiteral("Token 无效或已过期，请检查后重试");
        } else if (reply->error() != QNetworkReply::NoError) {
            out.error = QStringLiteral("网络错误：%1").arg(reply->errorString());
        } else {
            out.error = QStringLiteral("GitHub 验证失败（HTTP %1）").arg(status);
        }
        reply->deleteLater();
        completion(out);
    });
}
