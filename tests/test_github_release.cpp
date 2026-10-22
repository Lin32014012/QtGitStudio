// v4-P4-4 GitHub Release Management tests. Mock HTTP only - never
// touches the real GitHub API.
//   1. JSON parsing (fixture)
//   2. Create request shape (URL / method / JSON body / auth header)
//   3. Token missing (no HTTP call, TokenMissing kind)
//   4. 403 (Forbidden kind)
//   5. Repository missing (404 -> RepositoryMissing kind)
#include <QtTest>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QBuffer>
#include <QTimer>
#include "GitHubReleaseService.h"

// --- fakes (same pattern as test_github_repository_fakes.h) ----------

class FakeReply : public QNetworkReply {
public:
    FakeReply(const QByteArray &body, int status,
              QNetworkReply::NetworkError err, const QString &errString,
              QObject *parent = nullptr)
        : QNetworkReply(parent), m_body(body), m_buffer(&m_body) {
        setHeader(QNetworkRequest::ContentLengthHeader, body.size());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        if (err != QNetworkReply::NoError) {
            setError(err, errString);
        }
        m_buffer.open(QIODevice::ReadOnly);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(0, this, &QNetworkReply::finished);
    }
    void abort() override {}
    qint64 bytesAvailable() const override {
        return m_buffer.bytesAvailable() + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxlen) override {
        return m_buffer.read(data, maxlen);
    }

private:
    QByteArray m_body;
    QBuffer m_buffer;
};

// Records every request (URL, verb, body, auth header) and serves a
// canned response.
class RecordingNetworkManager : public QNetworkAccessManager {
public:
    struct RequestRecord {
        QNetworkAccessManager::Operation operation;
        QUrl url;
        QByteArray body;
        QByteArray authorization;
    };

    RecordingNetworkManager(const QByteArray &body, int status,
                            QNetworkReply::NetworkError err,
                            const QString &errString,
                            QObject *parent = nullptr)
        : QNetworkAccessManager(parent), m_body(body), m_status(status),
          m_error(err), m_errorString(errString) {}

    QList<RequestRecord> requests() const { return m_requests; }

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &req,
                                 QIODevice *outgoingData) override {
        RequestRecord record;
        record.operation = op;
        record.url = req.url();
        record.authorization = req.rawHeader("Authorization");
        if (outgoingData) {
            record.body = outgoingData->readAll();
        }
        m_requests.append(record);
        return new FakeReply(m_body, m_status, m_error, m_errorString, this);
    }

private:
    QByteArray m_body;
    int m_status;
    QNetworkReply::NetworkError m_error;
    QString m_errorString;
    QList<RequestRecord> m_requests;
};

// --- fixtures ----------------------------------------------------------
// NOTE: plain escaped strings, NOT raw string literals. The Qt 6.11.1
// moc mis-parses raw strings containing "//" (URLs) followed by more
// lines and then reports "No relevant classes found" for the Q_OBJECT
// test class (silently producing an empty .moc).

const QByteArray kMockReleases =
    "[{\"id\":101,\"tag_name\":\"v1.0\",\"name\":\"First\",\"body\":\"notes one\","
    "\"html_url\":\"https://github.com/o/r/releases/tag/v1.0\","
    "\"draft\":false,\"prerelease\":false},"
    "{\"id\":102,\"tag_name\":\"v2.0-rc1\",\"name\":\"RC\",\"body\":\"\","
    "\"html_url\":\"https://github.com/o/r/releases/tag/v2.0-rc1\","
    "\"draft\":false,\"prerelease\":true},"
    "{\"id\":103,\"tag_name\":\"v3.0-wip\",\"name\":\"WIP\",\"body\":\"draft body\","
    "\"html_url\":\"https://github.com/o/r/releases/tag/v3.0-wip\","
    "\"draft\":true,\"prerelease\":false}]";

const QByteArray kMockSingleRelease =
    "{\"id\":201,\"tag_name\":\"v9.9\",\"name\":\"Nine\",\"body\":\"created\","
    "\"html_url\":\"https://github.com/o/r/releases/tag/v9.9\","
    "\"draft\":false,\"prerelease\":false}";

class TestGitHubRelease : public QObject {
    Q_OBJECT
private:
    // NOTE: templates must live INSIDE the class - moc fails to parse
    // class-scope Q_OBJECT after a file-scope template definition.
    template <typename Launch>
    GitHubReleaseService::ListResult awaitList(Launch launch) {
        GitHubReleaseService::ListResult out;
        QEventLoop loop;
        bool done = false;
        launch([&out, &done, &loop](const GitHubReleaseService::ListResult &r) {
            out = r;
            done = true;
            if (loop.isRunning()) loop.quit();
        });
        if (!done) {
            QTimer::singleShot(10000, &loop, &QEventLoop::quit);
            loop.exec();
        }
        return out;
    }

    template <typename Launch>
    GitHubReleaseService::CreateResult awaitCreate(Launch launch) {
        GitHubReleaseService::CreateResult out;
        QEventLoop loop;
        bool done = false;
        launch([&out, &done, &loop](const GitHubReleaseService::CreateResult &r) {
            out = r;
            done = true;
            if (loop.isRunning()) loop.quit();
        });
        if (!done) {
            QTimer::singleShot(10000, &loop, &QEventLoop::quit);
            loop.exec();
        }
        return out;
    }

private slots:
    void jsonParsing() {
        QString error;
        const QList<GitHubRelease> releases =
            GitHubReleaseService::parseReleases(kMockReleases, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(releases.size(), 3);
        QCOMPARE(releases.at(0).id, 101LL);
        QCOMPARE(releases.at(0).tagName, QString("v1.0"));
        QCOMPARE(releases.at(0).name, QString("First"));
        QCOMPARE(releases.at(0).body, QString("notes one"));
        QCOMPARE(releases.at(0).htmlUrl,
                 QString("https://github.com/o/r/releases/tag/v1.0"));
        QVERIFY(!releases.at(0).draft);
        QVERIFY(!releases.at(0).prerelease);
        QVERIFY(releases.at(1).prerelease);
        QVERIFY(releases.at(2).draft);

        // Invalid JSON -> error, empty list, no crash.
        QString err2;
        const QList<GitHubRelease> bad =
            GitHubReleaseService::parseReleases("{not json", &err2);
        QVERIFY(!err2.isEmpty());
        QVERIFY(bad.isEmpty());
    }

    void createRequestShape() {
        auto *manager = new RecordingNetworkManager(
            kMockSingleRelease, 201, QNetworkReply::NoError, QString());
        GitHubReleaseService service;
        service.setNetworkManager(manager);

        const GitHubReleaseService::CreateResult result = awaitCreate(
            [&service](const GitHubReleaseService::CreateCompletion &cb) {
                service.createRelease(QStringLiteral("ghp_test_token"),
                                      QStringLiteral("octocat"),
                                      QStringLiteral("Hello-World"),
                                      QStringLiteral("v9.9"),
                                      QStringLiteral("Nine"),
                                      QStringLiteral("created"),
                                      false, true, cb);
            });
        QVERIFY2(result.success, qPrintable(result.error));
        QCOMPARE(result.release.id, 201LL);
        QCOMPARE(result.release.tagName, QString("v9.9"));

        // Exactly one POST to the releases endpoint.
        const QList<RecordingNetworkManager::RequestRecord> requests =
            manager->requests();
        QCOMPARE(requests.size(), 1);
        QCOMPARE(requests.at(0).operation, QNetworkAccessManager::PostOperation);
        QCOMPARE(requests.at(0).url.toString(),
                 QString("https://api.github.com/repos/octocat/Hello-World/releases"));
        // Bearer token from the session (never hardcoded in the request).
        QCOMPARE(requests.at(0).authorization,
                 QByteArray("Bearer ghp_test_token"));
        // JSON payload fields.
        const QJsonDocument payload =
            QJsonDocument::fromJson(requests.at(0).body);
        QVERIFY(payload.isObject());
        const QJsonObject obj = payload.object();
        QCOMPARE(obj.value("tag_name").toString(), QString("v9.9"));
        QCOMPARE(obj.value("name").toString(), QString("Nine"));
        QCOMPARE(obj.value("body").toString(), QString("created"));
        QVERIFY(!obj.value("draft").toBool());
        QVERIFY(obj.value("prerelease").toBool());
        delete manager;
    }

    void tokenMissing() {
        auto *manager = new RecordingNetworkManager(
            kMockReleases, 200, QNetworkReply::NoError, QString());
        GitHubReleaseService service;
        service.setNetworkManager(manager);

        const GitHubReleaseService::ListResult list = awaitList(
            [&service](const GitHubReleaseService::ListCompletion &cb) {
                service.fetchReleases(QString(), QStringLiteral("o"),
                                      QStringLiteral("r"), cb);
            });
        QVERIFY(!list.success);
        QCOMPARE(list.kind, GitHubReleaseService::ErrorKind::TokenMissing);
        // No HTTP call was made.
        QVERIFY(manager->requests().isEmpty());

        const GitHubReleaseService::CreateResult create = awaitCreate(
            [&service](const GitHubReleaseService::CreateCompletion &cb) {
                service.createRelease(QString(), QStringLiteral("o"),
                                      QStringLiteral("r"),
                                      QStringLiteral("v1"),
                                      QString(), QString(), false, false, cb);
            });
        QVERIFY(!create.success);
        QCOMPARE(create.kind, GitHubReleaseService::ErrorKind::TokenMissing);
        QVERIFY(manager->requests().isEmpty());
        delete manager;
    }

    void forbidden() {
        auto *manager = new RecordingNetworkManager(
            QByteArray("{\"message\":\"Must have push access\"}"), 403,
            QNetworkReply::NoError, QString());
        GitHubReleaseService service;
        service.setNetworkManager(manager);

        const GitHubReleaseService::ListResult result = awaitList(
            [&service](const GitHubReleaseService::ListCompletion &cb) {
                service.fetchReleases(QStringLiteral("ghp_x"),
                                      QStringLiteral("o"),
                                      QStringLiteral("r"), cb);
            });
        QVERIFY(!result.success);
        QCOMPARE(result.kind, GitHubReleaseService::ErrorKind::Forbidden);
        QVERIFY(!result.error.isEmpty());
        delete manager;
    }

    void repositoryMissing() {
        auto *manager = new RecordingNetworkManager(
            QByteArray("{\"message\":\"Not Found\"}"), 404,
            QNetworkReply::NoError, QString());
        GitHubReleaseService service;
        service.setNetworkManager(manager);

        const GitHubReleaseService::ListResult result = awaitList(
            [&service](const GitHubReleaseService::ListCompletion &cb) {
                service.fetchReleases(QStringLiteral("ghp_x"),
                                      QStringLiteral("o"),
                                      QStringLiteral("nope"), cb);
            });
        QVERIFY(!result.success);
        QCOMPARE(result.kind,
                 GitHubReleaseService::ErrorKind::RepositoryMissing);
        QVERIFY(!result.error.isEmpty());
        delete manager;
    }
};

QTEST_MAIN(TestGitHubRelease)
#include "test_github_release.moc"
