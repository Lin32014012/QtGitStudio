// v4-P1-4 Merge Support: git merge with result classification
// (FastForward / MergeCommit / Conflict / Error) and git merge --abort.
// Conflicts are NEVER auto-resolved - they are surfaced for manual
// handling (UU files visible via git status).
#ifndef GITMERGESERVICE_H
#define GITMERGESERVICE_H

#include <QObject>
#include <QString>
#include <functional>

class GitCommandExecutor;

class GitMergeService : public QObject {
    Q_OBJECT
public:
    enum class MergeKind {
        None,
        FastForward,
        MergeCommit,
        Conflict,
        Error,
    };
    struct MergeResult {
        MergeKind kind = MergeKind::None;
        bool success = false;
        QString error;   // raw git output on failure/conflict
        bool conflict = false;
        bool fastForward = false;
    };
    using MergeCompletion = std::function<void(const MergeResult &)>;

    explicit GitMergeService(QObject *parent = nullptr);

    void setWorkingDirectory(const QString &path);
    QString workingDirectory() const;

    // git merge <branch>. Completion is always called once.
    void mergeAsync(const QString &branch, MergeCompletion completion);

    // git merge --abort. True on success.
    void abortAsync(std::function<void(bool, const QString &)> completion);

private:
    MergeKind classify(const QString &combined, int exitCode) const;

    GitCommandExecutor *m_executor;
};

#endif // GITMERGESERVICE_H
