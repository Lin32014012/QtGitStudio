#include "CredentialStoreSession.h"

#include <QDebug>

CredentialStoreSession::CredentialStoreSession(QObject *parent)
    : CredentialStore(parent) {}

bool CredentialStoreSession::save(const QString &username,
                                  const QString &token) {
    static bool warned = false;
    if (!warned) {
        warned = true;
        qWarning("CredentialStoreSession: no OS credential vault backend on "
                 "this platform; the GitHub token is kept in memory only and "
                 "will not persist after the application exits.");
    }
    m_username = username;
    m_token = token;
    m_hasValue = true;
    return true;
}

bool CredentialStoreSession::read(QString *username, QString *token) {
    if (!m_hasValue) {
        return false;
    }
    if (username) {
        *username = m_username;
    }
    if (token) {
        *token = m_token;
    }
    return true;
}

bool CredentialStoreSession::remove() {
    m_username.clear();
    m_token.clear();
    m_hasValue = false;
    return true;
}
