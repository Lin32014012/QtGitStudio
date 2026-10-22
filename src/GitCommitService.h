#pragma once
#include <QObject>
#include <QStringList>
#include "GitError.h"
#include <functional>

class GitCommandExecutor;

class GitCommitService : public QObject {
    Q_OBJECT
public:
    struct OperationResult {
        bool success = false;
        GitError error;
    };

    using FilesCompletion = std::function<void(QStringList, GitError)>;
    using OperationCompletion = std::function<void(OperationResult)>;

    explicit GitCommitService(QObject *parent = nullptr);
    ~GitCommitService();
    
    void setWorkingDirectory(const QString &dir);
    
    bool stageFiles(const QStringList &files);
    QStringList stagedFiles();
    bool unstageFiles(const QStringList &files);
    bool commit(const QString &message);
    void stageFilesAsync(const QStringList &files, OperationCompletion completion);
    virtual void stagedFilesAsync(FilesCompletion completion);
    virtual void unstageFilesAsync(const QStringList &files, OperationCompletion completion);
    void commitAsync(const QString &message, OperationCompletion completion);
    
    GitError lastError() const { return m_lastError; }
    // v4-P6-21R6 test seam: inject a fake/spy executor for race tests.
    void setExecutorForTest(GitCommandExecutor *executor) {
        m_executor = executor;
    }
    quint64 repositoryGenerationForTest() const {
        return m_repositoryGeneration;
    }
    
private:
    bool hasHead();
    bool runGit(const QStringList &args, int *exitCode = nullptr, QString *output = nullptr, QString *stderrOutput = nullptr);
    
    GitCommandExecutor *m_executor;
    GitError m_lastError;
    quint64 m_repositoryGeneration = 0;
};
