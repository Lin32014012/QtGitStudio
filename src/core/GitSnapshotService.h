#pragma once
#include "GitRepository.h"
#include "GitSnapshot.h"
#include <QObject>

class GitSnapshotService : public QObject {
    Q_OBJECT
public:
    explicit GitSnapshotService(QObject *parent = nullptr);
    ~GitSnapshotService();
    
    void setWorkingDirectory(const QString &dir);
    const QString &workingDirectory() const { return m_workingDirectory; }
    GitSnapshot createSnapshot();
    // v4-P6-21R5: virtual so tests can inject delayed completions.
    virtual void createSnapshotAsync(GitRepository::SnapshotCompletion completion);
    GitError lastError() const;
    
private:
    GitRepository *m_repository;
    QString m_workingDirectory;
};
