#pragma once

#include "GitConflictModel.h"

#include <QObject>

#include <functional>

class GitCommandExecutor;

// Read-only conflict facts for a single repository session.  This service
// never modifies the worktree or index; content is loaded lazily by object id.
class GitConflictService : public QObject {
    Q_OBJECT
public:
    using ScanCompletion = std::function<void(ConflictScanResult)>;
    using ContentCompletion = std::function<void(ConflictFileContentResult)>;

    explicit GitConflictService(QObject *parent = nullptr);
    GitConflictService(GitCommandExecutor *executor, QObject *parent = nullptr);

    void setWorkingDirectory(const QString &path);
    QString workingDirectory() const;
    quint64 repositoryGeneration() const { return m_repositoryGeneration; }

    void setPreviewLimit(qint64 bytes);
    qint64 previewLimit() const { return m_previewLimit; }

    void scanConflictsAsync(ScanCompletion completion);
    void loadConflictContentAsync(const ConflictFile &file,
                                  ContentCompletion completion);

private:
    bool isSessionCurrent(quint64 generation, const QString &repository) const;
    void detectOperationAsync(ConflictSnapshot snapshot, quint64 generation,
                              const QString &repository,
                              ScanCompletion completion);

    GitCommandExecutor *m_executor = nullptr;
    QString m_workingDirectory;
    quint64 m_repositoryGeneration = 0;
    qint64 m_previewLimit = 2 * 1024 * 1024;
};
