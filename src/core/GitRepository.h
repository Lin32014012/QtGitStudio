#pragma once
#include "GitCommandExecutor.h"
#include "GitSnapshot.h"
#include "GitError.h"
#include <QObject>
#include <functional>

class GitRepository : public QObject {
    Q_OBJECT
public:
    using SnapshotCompletion = std::function<void(GitSnapshot, GitError)>;

    explicit GitRepository(QObject *parent = nullptr);
    ~GitRepository();
    
    void setWorkingDirectory(const QString &dir);
    QString workingDirectory() const;
    
    GitSnapshot status();
    GitSnapshot diff(const QString &commit = "HEAD");
    GitSnapshot branch();
    GitSnapshot snapshot();
    void snapshotAsync(SnapshotCompletion completion);
    bool initialize();
    
    GitError lastError() const { return m_lastError; }
    
private:
    GitCommandExecutor *m_executor;
    GitError m_lastError;
};
