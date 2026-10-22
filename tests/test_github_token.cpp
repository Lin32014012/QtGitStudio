// v4-P2-5 GitHub User Token Support tests. No real GitHub account or
// API: the HTTP layer is the fake QNetworkAccessManager from
// test_github_repository_fakes.h; persistence uses an in-memory
// CredentialStore. Scenarios:
//   1. empty token -> verification rejected immediately
//   2. mock verification success (200 + login -> username persisted)
//   3. mock verification failure (401 -> error, state stays logged out)
//   4. clearing the token (logout -> store cleared)
//   5. repository list blocked while logged out
#include <QtTest>
#include <QNetworkReply>
#include "GitHubAccountService.h"
#include "GitHubRepositoryService.h"
#include "CredentialStore.h"
#include "test_github_repository_fakes.h"

namespace {

// In-memory credential backend (test double).
class InMemoryTokenStore : public CredentialStore {
public:
    InMemoryTokenStore(QObject *parent = nullptr)
        : CredentialStore(parent) {}

    bool save(const QString &username, const QString &token) override {
        m_username = username;
        m_token = token;
        return true;
    }
    bool read(QString *username, QString *token) override {
        if (m_token.isEmpty()) return false;
        if (username) *username = m_username;
        if (token) *token = m_token;
        return true;
    }
    bool remove() override {
        m_username.clear();
        m_token.clear();
        return true;
    }

    QString storedUsername() const { return m_username; }
    QString storedToken() const { return m_token; }

private:
    QString m_username;
    QString m_token;
};

const QByteArray kUserJson =
    R"({"login":"octocat","id":1,"name":"The Octocat"})";

}  // namespace

class TestGitHubToken : public QObject {
    Q_OBJECT
private slots:
    void emptyTokenRejected() {
        GitHubAccountService service;
        FakeNetworkManager manager("{}", 200, QNetworkReply::NoError, "");
        service.setNetworkManager(&manager);
        bool done = false;
        service.verifyToken(QString(),
                            [&done](const GitHubAccountService::VerifyResult &r) {
                                QVERIFY(!r.success);
                                QVERIFY(r.error.contains("空"));
                                done = true;
                            });
        QVERIFY(done);  // synchronous - no HTTP call for empty token
        QVERIFY(!service.isLoggedIn());
    }

    void mockVerificationSuccess() {
        InMemoryTokenStore store;
        GitHubAccountService service;
        service.setCredentialStore(&store);
        FakeNetworkManager manager(kUserJson, 200, QNetworkReply::NoError, "");
        service.setNetworkManager(&manager);

        bool done = false;
        service.verifyToken("ghp_valid_token",
                            [&done](const GitHubAccountService::VerifyResult &r) {
                                QVERIFY(r.success);
                                QCOMPARE(r.username, QString("octocat"));
                                done = true;
                            });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        // Username came from the API and the session was persisted.
        QVERIFY(service.isLoggedIn());
        QCOMPARE(service.username(), QString("octocat"));
        QCOMPARE(service.token(), QString("ghp_valid_token"));
        QCOMPARE(store.storedUsername(), QString("octocat"));
        QCOMPARE(store.storedToken(), QString("ghp_valid_token"));
    }

    void mockVerificationFailure() {
        InMemoryTokenStore store;
        GitHubAccountService service;
        service.setCredentialStore(&store);
        FakeNetworkManager manager("{\"message\":\"Bad credentials\"}", 401,
                                   QNetworkReply::NoError, "");
        service.setNetworkManager(&manager);

        bool done = false;
        service.verifyToken("ghp_bad_token",
                            [&done](const GitHubAccountService::VerifyResult &r) {
                                QVERIFY(!r.success);
                                QVERIFY(!r.error.isEmpty());
                                done = true;
                            });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        // State stays logged out; nothing persisted.
        QVERIFY(!service.isLoggedIn());
        QVERIFY(store.storedToken().isEmpty());
    }

    void clearTokenLogsOut() {
        InMemoryTokenStore store;
        GitHubAccountService service;
        service.setCredentialStore(&store);
        service.login(QStringLiteral("octocat"), QStringLiteral("ghp_x"));
        QVERIFY(service.isLoggedIn());
        QCOMPARE(store.storedToken(), QString("ghp_x"));

        service.logout();
        QVERIFY(!service.isLoggedIn());
        QVERIFY(service.token().isEmpty());
        QVERIFY(store.storedToken().isEmpty());
        QVERIFY(store.storedUsername().isEmpty());
    }

    void repositoryListBlockedWhenLoggedOut() {
        GitHubRepositoryService repoService;
        // A network manager that would answer 200 - must NOT be hit
        // because the caller is logged out.
        FakeNetworkManager manager(kMockRepos, 200, QNetworkReply::NoError, "");
        repoService.setNetworkManager(&manager);

        bool done = false;
        repoService.fetchRepos(QString(),
                               [&done](const GitHubRepositoryService::FetchResult &r) {
                                   QVERIFY(!r.success);
                                   QVERIFY(r.unauthenticated);
                                   QVERIFY(r.error.contains("登录"));
                                   done = true;
                               });
        QVERIFY(done);  // synchronous rejection, no HTTP request
    }
};

QTEST_MAIN(TestGitHubToken)
#include "test_github_token.moc"
