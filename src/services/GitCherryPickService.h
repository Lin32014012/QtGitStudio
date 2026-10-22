// v4-P4-1 Cherry-pick Management MVP: apply a commit onto the current
// branch with conflict detection and abort.
//   - cherry-pick: git cherry-pick <hash>
//   - abort:       git cherry-pick --abort
//   - conflicts:   git status --porcelain (unmerged entries)
// Conflicts are NEVER auto-resolved - they are surfaced with the
// conflicted file list for manual handling / abort.
#ifndef GITCHERRYPICKSERVICE_H
#define GITCHERRYPICKSERVICE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <functional>

class GitCommandExecutor;

struct CherryPickResult {
    bool success = false;
    bool conflict = false;
    QString message;         // git output / error detail
    QStringList conflictedFiles;
};

class GitCherryPickService : public QObject {
    Q_OBJECT
public:
    using CherryPickCompletion = std::function<void(const CherryPickResult &)>;

    explicit GitCherryPickService(QObject *parent = nullptr);

    void setWorkingDirectory(const QString &path);
    QString workingDirectory() const;

    // git cherry-pick <hash>. Completion is always called once.
    void cherryPickAsync(const QString &hash, CherryPickCompletion completion);

    // git cherry-pick --abort. True on success.
    void abortAsync(std::function<void(bool, const QString &)> completion);

    // Pure parser for `git status --porcelain` output: returns paths of
    // unmerged (conflicted) entries. Exposed for unit testing.
    static QStringList parseConflictedFiles(const QByteArray &output);

private:
    GitCommandExecutor *m_executor;
};

#endif // GITCHERRYPICKSERVICE_H
