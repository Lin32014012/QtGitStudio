#include "GitCommitGraphService.h"
#include "GitCommandExecutor.h"

GitCommitGraphService::GitCommitGraphService(QObject *parent)
    : QObject(parent)
    , m_executor(new GitCommandExecutor(this)) {
}

GitCommitGraphService::LoadResult GitCommitGraphService::load(
    const QString &repoPath) {
    LoadResult result;
    m_executor->setWorkingDirectory(repoPath);
    // Full decoration preserves the ref namespace, which is the only safe
    // way to distinguish refs/heads/* from refs/remotes/*. Branch names
    // themselves are ambiguous because local branches may contain '/'.
    const GitCommandExecutor::Result gitResult = m_executor->execute(
        "log",
        {"--all", "--date-order", "--decorate=full",
         "--pretty=format:%H%x09%P%x09%an%x09%aI%x09%s%x09%d"});
    if (gitResult.error.hasError() && gitResult.exitCode != 0) {
        // Empty repositories make `git log` fail - treat as empty history.
        result.success = true;
        result.commits.clear();
        return result;
    }
    result.commits = parseLog(gitResult.output);
    result.success = true;
    return result;
}

QList<CommitNode> GitCommitGraphService::parseLog(const QByteArray &logOutput) {
    QList<CommitNode> commits;
    const QList<QByteArray> lines = logOutput.split('\n');
    for (const QByteArray &line : lines) {
        if (line.isEmpty()) continue;
        const QList<QByteArray> fields = line.split('\t');
        if (fields.size() < 5) continue;
        CommitNode node;
        node.hash = QString::fromUtf8(fields.at(0));
        node.shortHash = node.hash.left(7);
        // parents (field 1) may be empty.
        const QString parentField = QString::fromUtf8(fields.at(1));
        if (!parentField.isEmpty()) {
            node.parents = parentField.split(' ', Qt::SkipEmptyParts);
        }
        node.author = QString::fromUtf8(fields.at(2));
        node.time = QDateTime::fromString(QString::fromUtf8(fields.at(3)),
                                          Qt::ISODate);
        node.message = QString::fromUtf8(fields.at(4));
        if (fields.size() >= 6) {
            parseRefs(QString::fromUtf8(fields.at(5)),
                      &node.localBranches, &node.remoteBranches, &node.tags,
                      &node.isHead);
            node.refs = node.localBranches + node.remoteBranches + node.tags;
        }
        // v4-P3-3: merge commits have more than one parent.
        node.isMerge = node.parents.size() > 1;
        commits.append(node);
    }
    return commits;
}

void GitCommitGraphService::parseRefs(const QString &field,
                                      QStringList *localBranches,
                                      QStringList *remoteBranches,
                                      QStringList *tags,
                                      bool *isHead) {
    if (!localBranches || !remoteBranches || !tags || !isHead) return;
    localBranches->clear();
    remoteBranches->clear();
    tags->clear();
    *isHead = false;
    // %d output is wrapped in parentheses: " (HEAD -> refs/heads/main)".
    QString cleaned = field.trimmed();
    if (cleaned.startsWith(QLatin1Char('(')) && cleaned.endsWith(QLatin1Char(')'))) {
        cleaned = cleaned.mid(1, cleaned.size() - 2);
    }
    const QStringList parts = cleaned.split(", ", Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString trimmed = part.trimmed();
        if (trimmed.isEmpty()) continue;
        if (trimmed == QStringLiteral("HEAD")) {
            *isHead = true;
            continue;
        }
        QString name = trimmed;
        if (trimmed.startsWith(QStringLiteral("HEAD -> "))) {
            *isHead = true;
            name = trimmed.mid(8);
        }
        if (name.startsWith(QStringLiteral("refs/heads/"))) {
            localBranches->append(name.mid(11));
            continue;
        }
        if (name.startsWith(QStringLiteral("refs/remotes/"))) {
            remoteBranches->append(name.mid(13));
            continue;
        }
        if (name.startsWith(QStringLiteral("refs/tags/"))) {
            tags->append(name.mid(10));
            continue;
        }
        if (name.startsWith(QStringLiteral("tag: "))) {
            // v4-P6-21R6: with --decorate=full git prints
            // "tag: refs/tags/v1.0" - strip BOTH the marker and the
            // refs/tags/ prefix so the tag name is "v1.0".
            QString tagName = name.mid(5);
            if (tagName.startsWith(QStringLiteral("refs/tags/"))) {
                tagName = tagName.mid(10);
            }
            tags->append(tagName);
            continue;
        }
        // A bare decoration has no trustworthy local/remote identity.
        // Production requests --decorate=full, so ignore ambiguous legacy
        // values instead of guessing from '/' in a valid local branch name.
    }
}
