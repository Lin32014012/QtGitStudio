// v4-P3-1 Commit Graph MVP: reads commit history via git log and
// parses it into CommitNode records (hash, parents, refs, message,
// author, time). Pure parsing is static so tests never need git for
// the parse step itself.
#ifndef GITCOMMITGRAPHSERVICE_H
#define GITCOMMITGRAPHSERVICE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QDateTime>

class GitCommandExecutor;

struct CommitNode {
    QString hash;
    QString shortHash;
    QStringList parents;
    QStringList refs;        // all refs (HEAD excluded)
    bool isHead = false;     // "HEAD ->" marker in %D
    bool isMerge = false;    // v4-P3-3: more than one parent
    QStringList localBranches;   // v4-P3-3: non-remote branches
    QStringList remoteBranches;  // v4-P3-3: origin/... branches
    QStringList tags;            // v4-P3-4: refs/tags/* names
    QString message;
    QString author;
    QDateTime time;
};

class GitCommitGraphService : public QObject {
    Q_OBJECT
public:
    struct LoadResult {
        bool success = false;
        QList<CommitNode> commits;  // newest first (git log order)
        QString error;
    };

    explicit GitCommitGraphService(QObject *parent = nullptr);

    // Load commit history for `repoPath` (synchronous). Empty repository
    // yields success=true with an empty list (never a crash).
    LoadResult load(const QString &repoPath);

    // Pure parser for `git log --decorate=full
    // --pretty=format:%H%x09%P%x09%an%x09%aI%x09%s%x09%d` output.
    // Exposed for unit testing without git.
    static QList<CommitNode> parseLog(const QByteArray &logOutput);

    // Parse a full %d ref field. Only refs/heads/* and refs/remotes/*
    // carry enough identity for local vs remote classification.
    static void parseRefs(const QString &field,
                          QStringList *localBranches,
                          QStringList *remoteBranches,
                          QStringList *tags,
                          bool *isHead);

private:
    GitCommandExecutor *m_executor = nullptr;
};

#endif // GITCOMMITGRAPHSERVICE_H
