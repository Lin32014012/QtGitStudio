#include "ReleaseNotesService.h"
#include "GitCommandExecutor.h"

namespace {
const char *kFeatures = "Features";
const char *kBugFixes = "Bug Fixes";
const char *kChanges = "Changes";
}  // namespace

ReleaseNotesService::ReleaseNotesService(QObject *parent)
    : QObject(parent)
    , m_executor(new GitCommandExecutor(this)) {
}

void ReleaseNotesService::setWorkingDirectory(const QString &path) {
    m_executor->setWorkingDirectory(path);
}

QString ReleaseNotesService::workingDirectory() const {
    return m_executor->workingDirectory();
}

QList<ReleaseNoteCommit> ReleaseNotesService::parseLog(
    const QByteArray &output) {
    QList<ReleaseNoteCommit> commits;
    const QList<QByteArray> lines = output.split('\n');
    for (const QByteArray &line : lines) {
        if (line.isEmpty()) continue;
        const QList<QByteArray> fields = line.split('\t');
        if (fields.size() < 3) continue;
        ReleaseNoteCommit commit;
        commit.hash = QString::fromUtf8(fields.at(0));
        commit.author = QString::fromUtf8(fields.at(1));
        commit.message = QString::fromUtf8(fields.at(2));
        commits.append(commit);
    }
    return commits;
}

QString ReleaseNotesService::categorize(const QString &message) {
    const QString trimmed = message.trimmed();
    const QString lower = trimmed.toLower();
    if (lower.startsWith(QStringLiteral("feat:"))
        || lower.startsWith(QStringLiteral("feature:"))
        || lower.startsWith(QStringLiteral("add:"))) {
        return QString::fromLatin1(kFeatures);
    }
    if (lower.startsWith(QStringLiteral("fix:"))
        || lower.startsWith(QStringLiteral("bugfix:"))
        || lower.startsWith(QStringLiteral("repair:"))) {
        return QString::fromLatin1(kBugFixes);
    }
    return QString::fromLatin1(kChanges);
}

QString ReleaseNotesService::generateMarkdown(
    const QList<ReleaseNoteCommit> &commits) {
    if (commits.isEmpty()) {
        return QStringLiteral("_该范围内没有提交。_\n");
    }
    QStringList features;
    QStringList fixes;
    QStringList changes;
    for (const ReleaseNoteCommit &commit : commits) {
        const QString category = categorize(commit.message);
        const QString line = QStringLiteral("- %1 %2 (@%3)")
                                 .arg(commit.hash.left(7),
                                      commit.message, commit.author);
        if (category == QString::fromLatin1(kFeatures)) {
            features.append(line);
        } else if (category == QString::fromLatin1(kBugFixes)) {
            fixes.append(line);
        } else {
            changes.append(line);
        }
    }
    QStringList sections;
    if (!features.isEmpty()) {
        sections << QStringLiteral("## Features\n\n") + features.join('\n');
    }
    if (!fixes.isEmpty()) {
        sections << QStringLiteral("## Bug Fixes\n\n") + fixes.join('\n');
    }
    if (!changes.isEmpty()) {
        sections << QStringLiteral("## Changes\n\n") + changes.join('\n');
    }
    return sections.join(QStringLiteral("\n\n")) + QStringLiteral("\n");
}

void ReleaseNotesService::generate(const QString &oldTag,
                                   const QString &newTag,
                                   NotesCompletion completion) {
    m_executor->executeAsync(
        "log", {"--format=%H%x09%an%x09%s", oldTag + ".." + newTag},
        [this, oldTag, newTag, completion](GitCommandExecutor::Result result) {
            ReleaseNotesResult out;
            if (result.exitCode != 0) {
                // Invalid tag / no such range: clean failure with the
                // git error text.
                out.success = false;
                out.errorMessage =
                    QString::fromUtf8(result.errorOutput).trimmed();
                if (out.errorMessage.isEmpty()) {
                    out.errorMessage = QStringLiteral("无法解析提交范围 %1..%2")
                                           .arg(oldTag, newTag);
                }
                completion(out);
                return;
            }
            out.commits = parseLog(result.output);
            out.markdown = generateMarkdown(out.commits);
            out.success = true;
            completion(out);
        },
        30000);
}
