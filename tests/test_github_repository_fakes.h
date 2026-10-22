// Test fakes for GitHubRepositoryService tests. Kept in a separate header
// so the main test .cpp only contains the Q_OBJECT test class - moc
// fails to parse files that mix QObject subclasses without Q_OBJECT with
// a Q_OBJECT test class in the same translation unit.
#ifndef TEST_GITHUB_REPOSITORY_FAKES_H
#define TEST_GITHUB_REPOSITORY_FAKES_H

#include <QBuffer>
#include <QTimer>
#include <QNetworkReply>
#include <QNetworkAccessManager>

// Minimal fake reply delivering a canned body + status.
class FakeReply : public QNetworkReply {
public:
    FakeReply(const QByteArray &body, int status,
              QNetworkReply::NetworkError err, const QString &errString,
              QObject *parent = nullptr)
        : QNetworkReply(parent)
        , m_body(body)
        , m_buffer(&m_body) {
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

// Fake network manager serving a canned response for every request.
class FakeNetworkManager : public QNetworkAccessManager {
public:
    FakeNetworkManager(const QByteArray &body, int status,
                       QNetworkReply::NetworkError err,
                       const QString &errString, QObject *parent = nullptr)
        : QNetworkAccessManager(parent)
        , m_body(body), m_status(status), m_error(err), m_errorString(errString) {}

protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &, QIODevice *) override {
        return new FakeReply(m_body, m_status, m_error, m_errorString, this);
    }

private:
    QByteArray m_body;
    int m_status;
    QNetworkReply::NetworkError m_error;
    QString m_errorString;
};

const QByteArray kMockRepos =
    R"([
      {"name":"alpha","owner":{"login":"octocat"},"clone_url":"https://github.com/octocat/alpha.git"},
      {"name":"beta","owner":{"login":"octocat"},"clone_url":"https://github.com/octocat/beta.git"}
    ])";

#endif // TEST_GITHUB_REPOSITORY_FAKES_H
