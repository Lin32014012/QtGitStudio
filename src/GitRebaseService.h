// v4-P4-2 Rebase Management MVP: rebase the current branch onto a
// target branch, with up-to-date classification, conflict detection,
// continue and abort.
//   - rebase:   git rebase <targetBranch>
//   - continue: git rebase --continue (GIT_EDITOR=true, process-scoped)
//   - abort:    git rebase --abort
//   - status:   git status --porcelain (dirty / conflict files)
//   - progress: git rev-parse --git-path rebase-merge / rebase-apply
//               (path existence is checked by the service, never by
//               MainWindow)
// Conflicts are NEVER auto-resolved. Preconditions (valid repo, not
// detached, target exists, target != current, clean working tree, no
// rebase in progress) are checked inside the service; MainWindow only
// shows the confirm dialog and the results.
#ifndef GITREBASESERVICE_H
#define GITREBASESERVICE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>
#include "GitCommandExecutor.h"

class GitCommandExecutor;

enum class RebaseOutcome {
    Success,
    UpToDate,
    Conflict,
    InProgress,
    Rejected,
    Failed,
};

struct RebaseResult {
    RebaseOutcome outcome = RebaseOutcome::Failed;
    QString message;
    QStringList conflictedFiles;
};

class GitRebaseService : public QObject {
    Q_OBJECT
public:
    using RebaseCompletion = std::function<void(const RebaseResult &)>;
    using BoolCompletion = std::function<void(bool, const QString &)>;

    explicit GitRebaseService(QObject *parent = nullptr);

    void setWorkingDirectory(const QString &path);
    QString workingDirectory() const;

    // git rebase <targetBranch>, after precondition checks. Rejected
    // carries the human-readable reason in message.
    void rebaseAsync(const QString &targetBranch, RebaseCompletion completion);

    // git rebase --continue (non-interactive: GIT_EDITOR=true scoped to
    // the child process only). May return Conflict again.
    void continueAsync(RebaseCompletion completion);

    // git rebase --abort.
    void abortAsync(BoolCompletion completion);

    // True when rebase-merge or rebase-apply exists in the git dir.
    void isRebaseInProgressAsync(BoolCompletion completion);

private:
    bool preconditionsOk(const QString &targetBranch, QString *reason);
    void classify(const GitCommandExecutor::Result &result,
                  RebaseCompletion completion);
    void collectConflictedFiles(RebaseResult *out);

    GitCommandExecutor *m_executor;
    GitCommandExecutor *m_continueExecutor;
};

#endif // GITREBASESERVICE_H
