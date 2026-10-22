#include "GitCommitDiffService.h"
#include "TextEncoding.h"
#include "GitCommandExecutor.h"

#include <QRegularExpression>

GitCommitDiffService::GitCommitDiffService(QObject *parent)
    : QObject(parent)
    , m_executor(new GitCommandExecutor(this)) {
}

GitCommitDiffService::LoadResult GitCommitDiffService::load(
    const QString &repoPath, const QString &hash) {
    LoadResult result;
    if (hash.trimmed().isEmpty()) {
        result.error = QStringLiteral("缺少提交 hash");
        return result;
    }
    m_executor->setWorkingDirectory(repoPath);
    // -m: for merge commits, plain `git show` emits an empty combined
    // diff when the merge resolved cleanly; -m expands each parent's
    // diff so changed files are always visible. Regular and root
    // commits are unaffected.
    const GitCommandExecutor::Result gitResult = m_executor->execute(
        "show", {hash.trimmed(), "-m", "--no-color", "--format=%H"});
    if (gitResult.error.hasError() && gitResult.exitCode != 0) {
        result.error = QStringLiteral("无法读取提交：%1")
                           .arg(QString::fromUtf8(gitResult.errorOutput).trimmed());
        return result;
    }
    result.diff = parseShow(hash.trimmed(), gitResult.output);
    result.success = true;
    return result;
}

CommitDiff GitCommitDiffService::parseShow(const QString &hash,
                                           const QByteArray &showOutput) {
    CommitDiff diff;
    diff.hash = hash;
    const QString text = TextEncoding::decodeMixedLines(showOutput);
    diff.diffText = text;

    static const QRegularExpression fileRe(
        QStringLiteral("^diff --git a/(.*) b/.*$"));
    QStringList files;
    const QStringList lines = text.split('\n');
    for (const QString &line : lines) {
        const QRegularExpressionMatch match = fileRe.match(line);
        if (match.hasMatch()) {
            const QString path = match.captured(1);
            if (!files.contains(path)) {
                files.append(path);
            }
            continue;
        }
        // Count added/removed lines (skip +++/--- headers).
        if (line.startsWith(QLatin1Char('+'))
            && !line.startsWith(QStringLiteral("+++"))) {
            ++diff.additions;
        } else if (line.startsWith(QLatin1Char('-'))
                   && !line.startsWith(QStringLiteral("---"))) {
            ++diff.deletions;
        }
    }
    diff.files = files;
    return diff;
}
