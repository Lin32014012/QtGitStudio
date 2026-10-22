// v4-P3-2 Commit Graph Diff Integration: fetches the changes of a
// single commit via `git show <commit>` and parses them into CommitDiff
// (hash, files, diff text, additions, deletions). Works for regular
// commits, the first (root) commit and merge commits (combined diff).
#ifndef GITCOMMITDIFFSERVICE_H
#define GITCOMMITDIFFSERVICE_H

#include <QObject>
#include <QString>
#include <QStringList>

class GitCommandExecutor;

struct CommitDiff {
    QString hash;
    QStringList files;   // changed file paths (deduplicated)
    QString diffText;    // raw diff output (no color)
    int additions = 0;   // lines added
    int deletions = 0;   // lines removed
};

class GitCommitDiffService : public QObject {
    Q_OBJECT
public:
    struct LoadResult {
        bool success = false;
        CommitDiff diff;
        QString error;
    };

    explicit GitCommitDiffService(QObject *parent = nullptr);

    // Load the diff of `hash` inside `repoPath` (synchronous). Invalid
    // hash / empty repo -> success=false with a message (never crashes).
    LoadResult load(const QString &repoPath, const QString &hash);

    // Pure parser over `git show` output (exposed for unit tests).
    static CommitDiff parseShow(const QString &hash, const QByteArray &showOutput);

private:
    GitCommandExecutor *m_executor = nullptr;
};

#endif // GITCOMMITDIFFSERVICE_H
