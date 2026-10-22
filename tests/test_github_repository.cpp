// v4-P2-4 GitHub Repository Integration tests. No real GitHub API is
// used: parsing is exercised directly with mock JSON, and the HTTP layer
// is a fake QNetworkAccessManager (see test_github_repository_fakes.h).
// Scenarios:
//   1. logged out (empty token) -> immediate unauthenticated result
//   2. mock repository list parsing (name/owner/clone_url)
//   3. clone URL propagation through fetchRepos (fake 200 response)
//   4. API failure handling (401 and network error)
#include <QtTest>
#include "GitHubRepositoryService.h"
#include "test_github_repository_fakes.h"

class TestGitHubRepository : public QObject {
    Q_OBJECT
private slots:
    void loggedOutImmediateResult() {
        GitHubRepositoryService service;
        FakeNetworkManager manager("{}", 200, QNetworkReply::NoError, "");
        service.setNetworkManager(&manager);
        bool done = false;
        service.fetchRepos(QString(), [&done](const GitHubRepositoryService::FetchResult &r) {
            QVERIFY(!r.success);
            QVERIFY(r.unauthenticated);
            QVERIFY(r.error.contains("登录"));
            done = true;
        });
        QVERIFY(done);  // synchronous for empty token - no HTTP call
    }

    void mockParsing() {
        QString err;
        const QList<GitHubRepository> repos =
            GitHubRepositoryService::parseRepos(kMockRepos, &err);
        QCOMPARE(repos.size(), 2);
        QCOMPARE(repos.at(0).name, QString("alpha"));
        QCOMPARE(repos.at(0).owner, QString("octocat"));
        QCOMPARE(repos.at(0).cloneUrl,
                 QString("https://github.com/octocat/alpha.git"));
        QCOMPARE(repos.at(1).name, QString("beta"));
        QCOMPARE(repos.at(1).cloneUrl,
                 QString("https://github.com/octocat/beta.git"));
    }

    void malformedJsonHandled() {
        QString err;
        const QList<GitHubRepository> repos =
            GitHubRepositoryService::parseRepos("not-json", &err);
        QVERIFY(repos.isEmpty());
        QVERIFY(!err.isEmpty());
    }

    void cloneUrlPropagationThroughFetch() {
        GitHubRepositoryService service;
        FakeNetworkManager manager(kMockRepos, 200, QNetworkReply::NoError, "");
        service.setNetworkManager(&manager);
        bool done = false;
        service.fetchRepos("ghp_fake_token",
                           [&done](const GitHubRepositoryService::FetchResult &r) {
                               QVERIFY(r.success);
                               QCOMPARE(r.repositories.size(), 2);
                               QCOMPARE(r.repositories.at(1).cloneUrl,
                                        QString("https://github.com/octocat/beta.git"));
                               done = true;
                           });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
    }

    void apiUnauthorizedHandled() {
        GitHubRepositoryService service;
        FakeNetworkManager manager("{\"message\":\"Bad credentials\"}", 401,
                                   QNetworkReply::NoError, "");
        service.setNetworkManager(&manager);
        bool done = false;
        service.fetchRepos("ghp_bad_token",
                           [&done](const GitHubRepositoryService::FetchResult &r) {
                               QVERIFY(!r.success);
                               QVERIFY(r.unauthenticated);
                               done = true;
                           });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
    }

    void networkErrorHandled() {
        GitHubRepositoryService service;
        FakeNetworkManager manager(QByteArray(), 0,
                                   QNetworkReply::ConnectionRefusedError,
                                   QStringLiteral("fake network failure"));
        service.setNetworkManager(&manager);
        bool done = false;
        service.fetchRepos("ghp_net_token",
                           [&done](const GitHubRepositoryService::FetchResult &r) {
                               QVERIFY(!r.success);
                               QVERIFY(!r.unauthenticated);
                               QVERIFY(r.error.contains("网络错误"));
                               done = true;
                           });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
    }
};

QTEST_MAIN(TestGitHubRepository)
#include "test_github_repository.moc"
