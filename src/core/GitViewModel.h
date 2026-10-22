#pragma once
#include "GitSnapshotService.h"
#include "GitSnapshot.h"
#include <QObject>
#include <functional>

class GitViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QStringList branches READ branches NOTIFY branchesChanged)
    Q_PROPERTY(QString currentBranch READ currentBranch NOTIFY currentBranchChanged)
    Q_PROPERTY(QString lastCommitHash READ lastCommitHash NOTIFY commitFinished)
    Q_PROPERTY(QString lastCommitMessage READ lastCommitMessage NOTIFY commitFinished)
    
public:
    using RefreshCompletion = std::function<void(GitError)>;

    explicit GitViewModel(QObject *parent = nullptr);
    ~GitViewModel();
    
    void setWorkingDirectory(const QString &dir);
    // v4-P6-21R5: test seam - inject a delayed/fake snapshot service.
    void setSnapshotServiceForTest(GitSnapshotService *service) {
        m_service = service;
    }
    bool isLoading() const;
    QString error() const;
    GitSnapshot snapshot() const;
    QStringList branches() const;
    QString currentBranch() const;
    QString lastCommitHash() const;
    QString lastCommitMessage() const;
    quint64 sessionGenerationForTest() const {
        return m_sessionGeneration;
    }
    
    void setBranches(const QStringList &branches);
    void setCurrentBranch(const QString &branch);
    void setError(const QString &error);
    
    Q_INVOKABLE void refresh();
    void refreshAsync(RefreshCompletion completion);
    // v4-P6-21R5 P1-01: called by the host on repository switch -
    // invalidates in-flight refresh results and clears loading so the
    // NEW repository's refresh is never blocked or overwritten.
    void resetSession();
    
signals:
    void snapshotChanged();
    void loadingChanged();
    void errorChanged();
    void branchesChanged();
    void currentBranchChanged();
    void commitFinished();
    void commitError();
    
private:
    GitSnapshotService *m_service;
    GitSnapshot m_snapshot;
    bool m_loading = false;
    // v4-P6-21R5 P1-01: repository-session generation for loading.
    quint64 m_sessionGeneration = 0;
    QString m_error;
    QStringList m_branches;
    QString m_currentBranch;
    QString m_lastCommitHash;
    QString m_lastCommitMessage;
};
