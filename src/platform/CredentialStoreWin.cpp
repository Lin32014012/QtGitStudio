#include "CredentialStoreWin.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#include <wchar.h>
#endif

// v4-P6-18 P1-1: Win32 Credential Manager (advapi32).
// Generic credential entry "QtGitStudio/GitHubToken".
// v4-P6-21R1: the target name is injectable (tests use an isolated
// namespace) and save() falls back across Persist types because
// CRED_PERSIST_ENTERPRISE fails with ERROR_NO_SUCH_LOGON_SESSION (1312)
// in non-interactive logon sessions (CI / service context).
// v4-P6-21R4 P1-05: fallback is RESTRICTED to a whitelist of
// persistence-capability errors; the actual persistence mode is
// recorded and queryable.
namespace {
const wchar_t kProductionTarget[] = L"QtGitStudio/GitHubToken";

// Errors for which weakening the persistence level is a reasonable
// recovery (the session lacks the capability for a stronger level).
// ERROR_NO_SUCH_LOGON_SESSION (1312): no interactive logon session
// token for the calling thread - enterprise persistence unavailable.
bool isFallbackEligibleError(DWORD error) {
    switch (error) {
    case ERROR_NO_SUCH_LOGON_SESSION:  // 1312
        return true;
    default:
        return false;  // unknown/API-contract errors fail immediately
    }
}
}

CredentialStoreWin::CredentialStoreWin(QObject *parent)
    : CredentialStore(parent)
    , m_targetName(QString::fromWCharArray(kProductionTarget)) {}

CredentialStoreWin::CredentialStoreWin(const QString &targetName,
                                       QObject *parent)
    : CredentialStore(parent)
    , m_targetName(targetName) {}

bool CredentialStoreWin::save(const QString &username, const QString &token) {
#ifdef Q_OS_WIN
    const std::wstring uw = username.toStdWString();
    const std::wstring tw = token.toStdWString();
    const std::wstring target = m_targetName.toStdWString();
    CREDENTIALW cred = {};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<wchar_t *>(target.c_str());
    cred.UserName = const_cast<wchar_t *>(uw.c_str());
    cred.CredentialBlobSize = static_cast<DWORD>(tw.size() * sizeof(wchar_t));
    cred.CredentialBlob =
        reinterpret_cast<BYTE *>(const_cast<wchar_t *>(tw.c_str()));
    // v4-P6-21 ISSUE-07 + R1: try persistence modes from most to least
    // demanding. ENTERPRISE is the desktop default; LOCAL_MACHINE and
    // SESSION cover non-interactive logon sessions (error 1312).
    // v4-P6-21R4 P1-05: ONLY whitelisted persistence-capability errors
    // may fall back - an unknown error fails immediately instead of
    // silently degrading persistence.
    const DWORD kPersistModes[] = {CRED_PERSIST_ENTERPRISE,
                                   CRED_PERSIST_LOCAL_MACHINE,
                                   CRED_PERSIST_SESSION};
    m_lastPersistMode = PersistMode::None;
    DWORD lastError = 0;
    for (DWORD mode : kPersistModes) {
        cred.Persist = mode;
        // v4-P6-21R5 P1-05: injected fake API (tests) or real Win32.
        const BOOL ok = m_api ? m_api->write(&cred, 0) : CredWriteW(&cred, 0);
        if (ok != FALSE) {
            m_lastPersistMode = (mode == CRED_PERSIST_ENTERPRISE)
                ? PersistMode::Enterprise
                : (mode == CRED_PERSIST_LOCAL_MACHINE)
                    ? PersistMode::LocalMachine
                    : PersistMode::Session;
            return true;
        }
        lastError = m_api ? m_api->lastError() : GetLastError();
        if (!isFallbackEligibleError(lastError)) {
            break;  // unknown error - do NOT degrade persistence
        }
    }
    // Diagnostics only - never log the token itself.
    wchar_t buf[256] = {};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, lastError, 0,
                   buf, 255, nullptr);
    qWarning("CredentialStoreWin::save failed (error %lu: %ls, target len=%zu)",
             lastError, buf, target.size());
    return false;
#else
    Q_UNUSED(username);
    Q_UNUSED(token);
    return false;
#endif
}

bool CredentialStoreWin::read(QString *username, QString *token) {
#ifdef Q_OS_WIN
    const std::wstring target = m_targetName.toStdWString();
    PCREDENTIALW cred = nullptr;
    if (CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred) != FALSE
        && cred) {
        if (username) {
            *username = QString::fromWCharArray(cred->UserName);
        }
        if (token && cred->CredentialBlob && cred->CredentialBlobSize > 0) {
            const int chars =
                static_cast<int>(cred->CredentialBlobSize / sizeof(wchar_t));
            *token =
                QString::fromWCharArray(
                    reinterpret_cast<const wchar_t *>(cred->CredentialBlob),
                    chars);
        }
        CredFree(cred);
        return token ? !token->isEmpty() : true;
    }
    return false;
#else
    Q_UNUSED(username);
    Q_UNUSED(token);
    return false;
#endif
}

bool CredentialStoreWin::remove() {
#ifdef Q_OS_WIN
    const std::wstring target = m_targetName.toStdWString();
    const BOOL ok = CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0);
    if (!ok) {
        const DWORD err = GetLastError();
        if (err == ERROR_NOT_FOUND) {
            return true;  // nothing stored - removal is a no-op success
        }
        wchar_t buf[256] = {};
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, err, 0,
                       buf, 255, nullptr);
        qWarning("CredentialStoreWin::remove failed (error %lu: %ls)",
                 err, buf);
        return false;
    }
    return true;
#else
    return false;
#endif
}
