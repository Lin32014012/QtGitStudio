// v4-P2-2 GitHubAccountService - optional GitHub account state
// management. Logging in is NEVER required for local workflows (open,
// commit, branch, remote, clone all work while logged out). The service
// owns the auth state; MainWindow only renders it. Token persistence goes
// through the injected CredentialStore (never plain files).
#ifndef GITHUBACCOUNTSERVICE_H
#define GITHUBACCOUNTSERVICE_H

#include <QObject>
#include <QString>
#include <functional>

class CredentialStore;
class QNetworkAccessManager;

class GitHubAccountService : public QObject {
    Q_OBJECT
public:
    enum class AccountState {
        LoggedOut,
        LoggedIn,
    };

    // v4-P2-5: token verification result.
    struct VerifyResult {
        bool success = false;
        QString username;  // from GET /user when successful
        QString error;
    };
    using VerifyCompletion = std::function<void(const VerifyResult &)>;

    explicit GitHubAccountService(QObject *parent = nullptr);

    // Injectable credential backend (tests use an in-memory store; the
    // production Windows Credential Manager backend plugs in here).
    void setCredentialStore(CredentialStore *store);

    // Injectable HTTP backend for token verification (tests use a fake
    // network manager; never touches the real GitHub API in tests).
    void setNetworkManager(QNetworkAccessManager *manager);

    AccountState state() const { return m_state; }
    bool isLoggedIn() const { return m_state == AccountState::LoggedIn; }
    QString username() const { return m_username; }

    // Try to restore a previously saved session from the store. Called at
    // startup; never throws/crashes when the store is absent or fails.
    void restore();

    // Log in with an explicit username + token. Persists via the store.
    // Returns false (state stays LoggedOut) when the token is empty.
    bool login(const QString &username, const QString &token);

    // Clear the stored token and reset to LoggedOut.
    bool logout();  // v4-P6-21: false = credential removal failed

    // Read the current token (for future API calls). Empty when logged
    // out or when the store read fails. Never crashes.
    QString token() const;

    // v4-P2-5: validate a user-provided Personal Access Token against
    // GET /user (Authorization: Bearer). On success the username from
    // the API is persisted via login() and returned; on failure the
    // state stays LoggedOut and the error is surfaced.
    void verifyToken(const QString &token, VerifyCompletion completion);

signals:
    void stateChanged();

private:
    CredentialStore *m_store = nullptr;
    QNetworkAccessManager *m_manager = nullptr;
    bool m_ownsManager = false;
    AccountState m_state = AccountState::LoggedOut;
    QString m_username;
    QString m_token;
};

#endif // GITHUBACCOUNTSERVICE_H
