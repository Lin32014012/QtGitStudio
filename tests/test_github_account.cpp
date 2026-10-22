// v4-P2-2 GitHub Optional Account Support tests. No real GitHub account
// is used - an in-memory CredentialStore simulates persistence.
// Scenarios:
//   1. default state is LoggedOut
//   2. simulated login persists username correctly
//   3. token read failure never crashes
//   4. logout clears the token
//   5. restart recovery restores the saved session
#include <QtTest>
#include "GitHubAccountService.h"
#include "CredentialStore.h"

namespace {

// In-memory credential backend (test double).
class InMemoryCredentialStore : public CredentialStore {
public:
    InMemoryCredentialStore(QObject *parent = nullptr)
        : CredentialStore(parent) {}

    bool save(const QString &username, const QString &token) override {
        m_username = username;
        m_token = token;
        return true;
    }
    bool read(QString *username, QString *token) override {
        if (m_readFails || m_token.isEmpty()) {
            return false;  // read failure / nothing stored
        }
        if (username) *username = m_username;
        if (token) *token = m_token;
        return true;
    }
    bool remove() override {
        m_username.clear();
        m_token.clear();
        return true;
    }
    // Failure injection for the "read fails" scenario.
    void setReadFails(bool fails) { m_readFails = fails; }

private:
    QString m_username;
    QString m_token;
    bool m_readFails = false;
};

}  // namespace

class TestGitHubAccount : public QObject {
    Q_OBJECT
private slots:
    void defaultLoggedOut() {
        GitHubAccountService service;
        QCOMPARE(service.state(), GitHubAccountService::AccountState::LoggedOut);
        QVERIFY(!service.isLoggedIn());
        QVERIFY(service.username().isEmpty());
        QVERIFY(service.token().isEmpty());
    }

    void simulatedLoginPersistsUsername() {
        InMemoryCredentialStore store;
        GitHubAccountService service;
        service.setCredentialStore(&store);

        QVERIFY(service.login(QStringLiteral("octocat"),
                              QStringLiteral("ghp_test_token_123")));
        QVERIFY(service.isLoggedIn());
        QCOMPARE(service.username(), QStringLiteral("octocat"));
        QCOMPARE(service.token(), QStringLiteral("ghp_test_token_123"));
        // Persisted in the store (not in any plain file).
        QString storedUser, storedToken;
        QVERIFY(store.read(&storedUser, &storedToken));
        QCOMPARE(storedUser, QStringLiteral("octocat"));
        QCOMPARE(storedToken, QStringLiteral("ghp_test_token_123"));
    }

    void emptyTokenRejected() {
        InMemoryCredentialStore store;
        GitHubAccountService service;
        service.setCredentialStore(&store);
        QVERIFY(!service.login(QStringLiteral("octocat"), QString()));
        QVERIFY(!service.isLoggedIn());
    }

    void tokenReadFailureDoesNotCrash() {
        InMemoryCredentialStore store;
        store.setReadFails(false);
        store.save(QStringLiteral("octocat"), QStringLiteral("ghp_x"));
        GitHubAccountService service;
        service.setCredentialStore(&store);
        service.restore();
        QVERIFY(service.isLoggedIn());
        // Token fallback read works; forcing a failing store returns
        // empty without crashing.
        InMemoryCredentialStore brokenStore;
        brokenStore.setReadFails(true);
        GitHubAccountService service2;
        service2.setCredentialStore(&brokenStore);
        service2.restore();  // must not crash
        QVERIFY(!service2.isLoggedIn());
        QVERIFY(service2.token().isEmpty());
        // No store at all: everything stays safe.
        GitHubAccountService service3;
        service3.restore();
        QVERIFY(!service3.isLoggedIn());
    }

    void logoutClearsToken() {
        InMemoryCredentialStore store;
        GitHubAccountService service;
        service.setCredentialStore(&store);
        QVERIFY(service.login(QStringLiteral("octocat"), QStringLiteral("ghp_x")));
        QVERIFY(service.isLoggedIn());

        service.logout();
        QVERIFY(!service.isLoggedIn());
        QVERIFY(service.username().isEmpty());
        QVERIFY(service.token().isEmpty());
        // Store cleared too.
        QString u, t;
        QVERIFY(!store.read(&u, &t));
    }

    void restartRecovery() {
        InMemoryCredentialStore store;  // survives across "restarts"
        {
            GitHubAccountService service;
            service.setCredentialStore(&store);
            QVERIFY(service.login(QStringLiteral("octocat"),
                                  QStringLiteral("ghp_persist")));
        }
        // New service instance (simulated restart) restores the session.
        GitHubAccountService restarted;
        restarted.setCredentialStore(&store);
        restarted.restore();
        QVERIFY(restarted.isLoggedIn());
        QCOMPARE(restarted.username(), QStringLiteral("octocat"));
        QCOMPARE(restarted.token(), QStringLiteral("ghp_persist"));
    }
};

QTEST_MAIN(TestGitHubAccount)
#include "test_github_account.moc"
