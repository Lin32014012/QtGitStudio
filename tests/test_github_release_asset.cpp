// v4-P5-1 GitHub Release Asset Management tests. Mock HTTP only - never
// touches the real GitHub API.
//   1. JSON parsing (assets array fixture)
//   2. Upload request (uploads.github.com URL, POST, octet-stream,
//      Bearer header, file content body)
//   3. Delete request (DELETE + 204 success)
//   4. Token missing (no HTTP call)
//   5. File missing (clean error before any HTTP call)
// NOTE: fixtures use escaped strings, NOT raw string literals - the
// Qt 6.11.1 moc mis-parses raw strings containing "//" (URLs) plus more
// lines and then reports "No relevant classes found".
#include <QtTest>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QBuffer>
#include <QTimer>
#include <QTemporaryDir>
#include <QFile>
#include "GitHubReleaseAssetService.h"

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

// Records every request (URL, verb, body, auth header, content type).
class RecordingNetworkManager : public QNetworkAccessManager {
public:
    struct RequestRecord {
        QNetworkAccessManager::Operation operation;
        QUrl url;
        QByteArray body;
        QByteArray authorization;
        QByteArray contentType;
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
        record.contentType =
            req.header(QNetworkRequest::ContentTypeHeader).toByteArray();
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

// --- fixtures (escaped strings - see header note) ----------------------

const QByteArray kMockAssets =
    "[{\"id\":301,\"name\":\"app.exe\",\"size\":2048,"
    "\"browser_download_url\":\"https://github.com/o/r/releases/download/v1.0/app.exe\","
    "\"content_type\":\"application/octet-stream\"},"
    "{\"id\":302,\"name\":\"notes.md\",\"size\":128,"
    "\"browser_download_url\":\"https://github.com/o/r/releases/download/v1.0/notes.md\","
    "\"content_type\":\"text/markdown\"}]";

const QByteArray kMockUploadedAsset =
    "{\"id\":401,\"name\":\"payload.bin\",\"size\":5,"
    "\"browser_download_url\":\"https://github.com/o/r/releases/download/v1.0/payload.bin\","
    "\"content_type\":\"application/octet-stream\"}";

class TestGitHubReleaseAsset : public QObject {
    Q_OBJECT
private:
    // Templates must live INSIDE the class - moc mis-parses file-scope
    // templates before a Q_OBJECT class.
    template <typename Launch>
    GitHubReleaseAssetService::ListResult awaitList(Launch launch) {
        GitHubReleaseAssetService::ListResult out;
        QEventLoop loop;
        bool done = false;
        launch([&out, &done, &loop](const GitHubReleaseAssetService::ListResult &r) {
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
    GitHubReleaseAssetService::UploadResult awaitUpload(Launch launch) {
        GitHubReleaseAssetService::UploadResult out;
        QEventLoop loop;
        bool done = false;
        launch([&out, &done, &loop](const GitHubReleaseAssetService::UploadResult &r) {
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
    GitHubReleaseAssetService::DeleteResult awaitDelete(Launch launch) {
        GitHubReleaseAssetService::DeleteResult out;
        QEventLoop loop;
        bool done = false;
        launch([&out, &done, &loop](const GitHubReleaseAssetService::DeleteResult &r) {
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
        const QList<GitHubReleaseAsset> assets =
            GitHubReleaseAssetService::parseAssets(kMockAssets, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(assets.size(), 2);
        QCOMPARE(assets.at(0).id, 301LL);
        QCOMPARE(assets.at(0).name, QString("app.exe"));
        QCOMPARE(assets.at(0).size, 2048LL);
        QCOMPARE(assets.at(0).contentType,
                 QString("application/octet-stream"));
        QVERIFY(assets.at(0).browserDownloadUrl.contains("app.exe"));

        QString err2;
        const QList<GitHubReleaseAsset> bad =
            GitHubReleaseAssetService::parseAssets("{bad", &err2);
        QVERIFY(!err2.isEmpty());
        QVERIFY(bad.isEmpty());
    }

    void uploadRequestShape() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile file(dir.path() + "/payload.bin");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("hello");
        file.close();

        auto *manager = new RecordingNetworkManager(
            kMockUploadedAsset, 201, QNetworkReply::NoError, QString());
        GitHubReleaseAssetService service;
        service.setNetworkManager(manager);

        bool progressSeen = false;
        const GitHubReleaseAssetService::UploadResult result = awaitUpload(
            [&service, &dir, &progressSeen](
                const GitHubReleaseAssetService::UploadCompletion &cb) {
                service.uploadAsset(
                    QStringLiteral("ghp_test_token"),
                    QStringLiteral("octocat"), QStringLiteral("Hello-World"),
                    101LL, dir.path() + "/payload.bin",
                    [&progressSeen](qint64, qint64) { progressSeen = true; },
                    cb);
            });
        QVERIFY2(result.success, qPrintable(result.error));
        QCOMPARE(result.asset.id, 401LL);
        QCOMPARE(result.asset.name, QString("payload.bin"));

        // Exactly one POST to uploads.github.com with the name query.
        const QList<RecordingNetworkManager::RequestRecord> requests =
            manager->requests();
        QCOMPARE(requests.size(), 1);
        QCOMPARE(requests.at(0).operation, QNetworkAccessManager::PostOperation);
        QVERIFY(requests.at(0).url.toString().startsWith(
            "https://uploads.github.com/repos/octocat/Hello-World/releases/101/assets"));
        QVERIFY(requests.at(0).url.query().contains("name=payload.bin"));
        QCOMPARE(requests.at(0).authorization,
                 QByteArray("Bearer ghp_test_token"));
        QCOMPARE(requests.at(0).contentType,
                 QByteArray("application/octet-stream"));
        QCOMPARE(requests.at(0).body, QByteArray("hello"));
        Q_UNUSED(progressSeen);  // progress callback is wired; fake does
                                 // not emit uploadProgress
        delete manager;
    }

    void deleteRequestShape() {
        auto *manager = new RecordingNetworkManager(
            QByteArray(), 204, QNetworkReply::NoError, QString());
        GitHubReleaseAssetService service;
        service.setNetworkManager(manager);

        const GitHubReleaseAssetService::DeleteResult result = awaitDelete(
            [&service](const GitHubReleaseAssetService::DeleteCompletion &cb) {
                service.deleteAsset(QStringLiteral("ghp_test_token"),
                                    QStringLiteral("octocat"),
                                    QStringLiteral("Hello-World"), 301LL, cb);
            });
        QVERIFY2(result.success, qPrintable(result.error));

        const QList<RecordingNetworkManager::RequestRecord> requests =
            manager->requests();
        QCOMPARE(requests.size(), 1);
        QCOMPARE(requests.at(0).operation,
                 QNetworkAccessManager::DeleteOperation);
        QCOMPARE(requests.at(0).url.toString(),
                 QString("https://api.github.com/repos/octocat/Hello-World/releases/assets/301"));
        QCOMPARE(requests.at(0).authorization,
                 QByteArray("Bearer ghp_test_token"));
        delete manager;
    }

    void tokenMissing() {
        auto *manager = new RecordingNetworkManager(
            kMockAssets, 200, QNetworkReply::NoError, QString());
        GitHubReleaseAssetService service;
        service.setNetworkManager(manager);

        const GitHubReleaseAssetService::ListResult list = awaitList(
            [&service](const GitHubReleaseAssetService::ListCompletion &cb) {
                service.fetchAssets(QString(), QStringLiteral("o"),
                                    QStringLiteral("r"), 1LL, cb);
            });
        QVERIFY(!list.success);
        QCOMPARE(list.kind,
                 GitHubReleaseAssetService::ErrorKind::TokenMissing);
        QVERIFY(manager->requests().isEmpty());

        const GitHubReleaseAssetService::DeleteResult del = awaitDelete(
            [&service](const GitHubReleaseAssetService::DeleteCompletion &cb) {
                service.deleteAsset(QString(), QStringLiteral("o"),
                                    QStringLiteral("r"), 1LL, cb);
            });
        QVERIFY(!del.success);
        QCOMPARE(del.kind,
                 GitHubReleaseAssetService::ErrorKind::TokenMissing);
        QVERIFY(manager->requests().isEmpty());
        delete manager;
    }

    void fileMissing() {
        auto *manager = new RecordingNetworkManager(
            kMockUploadedAsset, 201, QNetworkReply::NoError, QString());
        GitHubReleaseAssetService service;
        service.setNetworkManager(manager);

        const GitHubReleaseAssetService::UploadResult result = awaitUpload(
            [&service](const GitHubReleaseAssetService::UploadCompletion &cb) {
                service.uploadAsset(
                    QStringLiteral("ghp_test_token"),
                    QStringLiteral("octocat"), QStringLiteral("Hello-World"),
                    101LL, "C:/does/not/exist.bin",
                    GitHubReleaseAssetService::ProgressCallback(), cb);
            });
        QVERIFY(!result.success);
        QCOMPARE(result.kind,
                 GitHubReleaseAssetService::ErrorKind::FileMissing);
        // No HTTP call was made.
        QVERIFY(manager->requests().isEmpty());
        delete manager;
    }
};

QTEST_MAIN(TestGitHubReleaseAsset)
#include "test_github_release_asset.moc"
