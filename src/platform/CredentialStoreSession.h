#pragma once
#include "CredentialStore.h"

// Portable fallback credential backend used on platforms without a
// supported OS credential vault (Linux / macOS builds).
//
// Security first: the token is kept ONLY in process memory and is never
// written to ini/json/QSettings files. The consequence is that a GitHub
// login does not survive an application restart on these platforms; a
// warning is logged once so the behavior is explicit, not silent.
class CredentialStoreSession : public CredentialStore {
    Q_OBJECT
public:
    explicit CredentialStoreSession(QObject *parent = nullptr);
    bool save(const QString &username, const QString &token) override;
    bool read(QString *username, QString *token) override;
    bool remove() override;
    // Always false: this backend is not durable across restarts.
    static constexpr bool isPersistent() { return false; }

private:
    QString m_username;
    QString m_token;
    bool m_hasValue = false;
};
