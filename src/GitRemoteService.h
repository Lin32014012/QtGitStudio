// v4-P1-2 Remote Support: remote listing (git remote -v), fetch, pull,
// push. All Git invocations go through GitCommandExecutor (async QProcess
// with lifecycle management). No Merge UI and no auto-resolve of
// conflicts - pull conflicts are surfaced to the caller verbatim.
#ifndef GITREMOTESERVICE_H
#define GITREMOTESERVICE_H

#include <QObject>
#include <QString>
#include <QList>
#include <functional>

class GitCommandExecutor;

struct GitRemote {
    QString name;
    QString fetchUrl;
    QString pushUrl;
};

class GitRemoteService : public QObject {
    Q_OBJECT
public:
    enum class ErrorKind {
        None,
        NoRemote,
        Network,
        Permission,
        Other,
    };
    struct OperationResult {
        bool success = false;
        QString error;
        // pull/push/fetch classification helpers
        bool alreadyUpToDate = false;
        bool conflict = false;
        bool hasRemote = true;
        ErrorKind errorKind = ErrorKind::None;
    };
    // v4-P6-21 ISSUE-05: remotesAsync now carries success + error so
    // "no remotes configured" and "remote read failed" stay distinct.
    struct RemotesResult {
        QList<GitRemote> remotes;
        bool success = false;
        QString error;
    };
    using RemotesCompletion = std::function<void(const RemotesResult &)>;
    using OperationCompletion = std::function<void(const OperationResult &)>;

    explicit GitRemoteService(QObject *parent = nullptr);

    void setWorkingDirectory(const QString &path);
    QString workingDirectory() const;

    virtual void remotesAsync(RemotesCompletion completion);
    QList<GitRemote> remotesSync();

    virtual void fetchAsync(OperationCompletion completion);
    virtual void pullAsync(OperationCompletion completion);
    virtual void pushAsync(OperationCompletion completion);

private:
    QList<GitRemote> parseRemotes(const QByteArray &output) const;
    OperationResult classify(const QByteArray &output, const QString &stderrOutput,
                             int exitCode) const;

    GitCommandExecutor *m_executor;
};

#endif // GITREMOTESERVICE_H
