#include "GitCherryPickService.h"
#include "GitCommandExecutor.h"

GitCherryPickService::GitCherryPickService(QObject *parent)
    : QObject(parent)
    , m_executor(new GitCommandExecutor(this)) {
}

void GitCherryPickService::setWorkingDirectory(const QString &path) {
    m_executor->setWorkingDirectory(path);
}

QString GitCherryPickService::workingDirectory() const {
    return m_executor->workingDirectory();
}

QStringList GitCherryPickService::parseConflictedFiles(
    const QByteArray &output) {
    QStringList files;
    const QList<QByteArray> lines = output.split('\n');
    for (const QByteArray &line : lines) {
        if (line.size() < 4) continue;
        const char x = line.at(0);
        const char y = line.at(1);
        // Unmerged entries: X or Y is 'U', or both equal 'A'/'D'.
        const bool unmerged = (x == 'U' || y == 'U'
                               || (x == 'A' && y == 'A')
                               || (x == 'D' && y == 'D'));
        if (!unmerged) continue;
        QByteArray path = line.mid(3).trimmed();
        // v1 porcelain quotes paths with special characters; strip the
        // surrounding quotes for display.
        if (path.size() >= 2 && path.startsWith('"') && path.endsWith('"')) {
            path = path.mid(1, path.size() - 2);
        }
        files.append(QString::fromUtf8(path));
    }
    return files;
}

void GitCherryPickService::cherryPickAsync(const QString &hash,
                                           CherryPickCompletion completion) {
    m_executor->executeAsync(
        "cherry-pick", {hash},
        [this, completion](GitCommandExecutor::Result result) {
            CherryPickResult out;
            const QString combined =
                QString::fromUtf8(result.output)
                + QString::fromUtf8(result.errorOutput);
            out.message = combined.trimmed();
            const bool saysConflict =
                combined.contains(QStringLiteral("CONFLICT"))
                || combined.contains(QStringLiteral("could not apply"));
            if (result.exitCode == 0) {
                out.success = true;
            } else if (saysConflict) {
                out.conflict = true;
                // Collect unmerged files via git status --porcelain.
                const GitCommandExecutor::Result status =
                    m_executor->execute("status", {"--porcelain"}, 15000);
                if (status.exitCode == 0) {
                    out.conflictedFiles = parseConflictedFiles(status.output);
                }
            }
            completion(out);
        },
        60000);
}

void GitCherryPickService::abortAsync(
    std::function<void(bool, const QString &)> completion) {
    m_executor->executeAsync(
        "cherry-pick", {"--abort"},
        [completion](GitCommandExecutor::Result result) {
            const bool ok = (result.exitCode == 0);
            completion(ok, ok ? QString()
                              : QString::fromUtf8(result.errorOutput));
        },
        30000);
}
