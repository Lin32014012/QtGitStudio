#pragma once
#include "CredentialStore.h"

// v4-P6-18 P1-1: Windows Credential Manager backend - tokens are kept
// in the OS credential vault (never in plain ini/json files).
// v4-P6-21R1: the target name is injectable so tests NEVER touch the
// production credential (QtGitStudio/GitHubToken).
// v4-P6-21R5 P1-05: injectable Win32 credential API seam - production
// uses the real CredWriteW/CredReadW/CredDeleteW; tests inject a fake
// to force Win32 error codes deterministically.
class ICredentialApi {
public:
    virtual ~ICredentialApi() = default;
    virtual bool write(void *cred, unsigned long flags) = 0;
    virtual bool read(const wchar_t *target, int type, void **cred) = 0;
    virtual bool remove(const wchar_t *target, int type, unsigned long flags) = 0;
    virtual unsigned long lastError() const = 0;
};

class CredentialStoreWin : public CredentialStore {
    Q_OBJECT
public:
    explicit CredentialStoreWin(QObject *parent = nullptr);
    // targetName override for tests; production keeps the default.
    explicit CredentialStoreWin(const QString &targetName,
                                QObject *parent = nullptr);
    // v4-P6-21R5 P1-05: injectable Win32 credential API (tests).
    void setCredentialApi(ICredentialApi *api) { m_api = api; }
    // v4-P6-21R4 P1-05: the actual persistence level used by the last
    // successful save (Session means NOT durable across logon sessions).
    enum class PersistMode { None, Enterprise, LocalMachine, Session };
    PersistMode lastPersistMode() const { return m_lastPersistMode; }
    bool save(const QString &username, const QString &token) override;
    bool read(QString *username, QString *token) override;
    bool remove() override;

private:
    QString m_targetName;
    PersistMode m_lastPersistMode = PersistMode::None;
    ICredentialApi *m_api = nullptr;
};
