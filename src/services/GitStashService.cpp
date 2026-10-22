#include "GitStashService.h"
#include "GitCommandExecutor.h"

GitStashService::GitStashService(QObject *parent)
    : QObject(parent)
    , m_executor(new GitCommandExecutor(this)) {
}

void GitStashService::setWorkingDirectory(const QString &path) {
    m_executor->setWorkingDirectory(path);
}

QString GitStashService::workingDirectory() const {
    return m_executor->workingDirectory();
}

// Parse one %gs field: "WIP on main: c3b84cb first", "On main: wip work"
// or an unexpected variant. Branch is everything before the first ':';
// the trailing "<hash> " prefix (if any) is stripped from the subject.
static void parseRefsField(const QString &field, QString *branch,
                           QString *message) {
    const int colon = field.indexOf(QLatin1Char(':'));
    if (colon < 0) {
        *branch = QString();
        *message = field;
        return;
    }
    QString head = field.left(colon).trimmed();
    QString subject = field.mid(colon + 1).trimmed();
    if (head.startsWith(QStringLiteral("WIP on "))) {
        head = head.mid(7);
    } else if (head.startsWith(QStringLiteral("On "))) {
        head = head.mid(3);
    } else if (head == QStringLiteral("WIP")) {
        head.clear();
    }
    *branch = head;
    // Strip the leading commit hash ("c3b84cb ") when present.
    const int firstSpace = subject.indexOf(QLatin1Char(' '));
    if (firstSpace > 0) {
        const QString token = subject.left(firstSpace);
        bool allHex = true;
        for (const QChar &c : token) {
            if (!c.isDigit() && !(c >= QLatin1Char('a') && c <= QLatin1Char('f'))
                && !(c >= QLatin1Char('A') && c <= QLatin1Char('F'))) {
                allHex = false;
                break;
            }
        }
        if (allHex && token.size() >= 7) {
            subject = subject.mid(firstSpace + 1).trimmed();
        }
    }
    *message = subject;
}

QList<GitStash> GitStashService::parseList(const QByteArray &output) {
    QList<GitStash> stashes;
    const QList<QByteArray> lines = output.split('\n');
    for (const QByteArray &line : lines) {
        if (line.isEmpty()) continue;
        GitStash stash;
        QString refsField;
        if (line.contains('\t')) {
            const QList<QByteArray> fields = line.split('\t');
            stash.id = QString::fromUtf8(fields.at(0).trimmed());
            refsField = QString::fromUtf8(fields.at(1).trimmed());
            if (fields.size() >= 3) {
                stash.date = QString::fromUtf8(fields.at(2).trimmed());
            }
        } else {
            // Plain `git stash list` line: "stash@{0}: WIP on main: ...".
            const int colon = line.indexOf(':');
            if (colon < 0) continue;
            stash.id = QString::fromUtf8(line.left(colon).trimmed());
            refsField = QString::fromUtf8(line.mid(colon + 1).trimmed());
        }
        if (!stash.id.startsWith(QStringLiteral("stash@{"))) continue;
        parseRefsField(refsField, &stash.branch, &stash.message);
        stashes.append(stash);
    }
    return stashes;
}

GitStashService::ListResult GitStashService::listStashes() {
    ListResult result;
    const GitCommandExecutor::Result gitResult = m_executor->execute(
        "log", {"-g", "refs/stash", "--format=%gd%x09%gs%x09%cI"}, 30000);
    if (gitResult.error.hasError() && gitResult.exitCode != 0) {
        // `git log -g refs/stash` fails when no stash exists yet -
        // that is a valid empty list, not an error.
        result.success = true;
        result.stashes.clear();
        return result;
    }
    result.stashes = parseList(gitResult.output);
    result.success = true;
    return result;
}

void GitStashService::createStashAsync(const QString &message,
                                       OperationCompletion completion) {
    QStringList args;
    args << "push";
    if (!message.trimmed().isEmpty()) {
        args << "-m" << message.trimmed();
    }
    m_executor->executeAsync(
        "stash", args,
        [completion](GitCommandExecutor::Result result) {
            OperationResult out;
            out.success = (result.exitCode == 0);
            if (!out.success) {
                out.error = QString::fromUtf8(result.errorOutput)
                    + QString::fromUtf8(result.output);
            }
            completion(out);
        },
        30000);
}

void GitStashService::applyStashAsync(const QString &id,
                                      OperationCompletion completion) {
    m_executor->executeAsync(
        "stash", {"apply", id},
        [completion](GitCommandExecutor::Result result) {
            OperationResult out;
            out.success = (result.exitCode == 0);
            if (!out.success) {
                out.error = QString::fromUtf8(result.errorOutput)
                    + QString::fromUtf8(result.output);
            }
            completion(out);
        },
        30000);
}

void GitStashService::popStashAsync(const QString &id,
                                    OperationCompletion completion) {
    m_executor->executeAsync(
        "stash", {"pop", id},
        [completion](GitCommandExecutor::Result result) {
            OperationResult out;
            out.success = (result.exitCode == 0);
            if (!out.success) {
                out.error = QString::fromUtf8(result.errorOutput)
                    + QString::fromUtf8(result.output);
            }
            completion(out);
        },
        30000);
}

void GitStashService::dropStashAsync(const QString &id,
                                     OperationCompletion completion) {
    m_executor->executeAsync(
        "stash", {"drop", id},
        [completion](GitCommandExecutor::Result result) {
            OperationResult out;
            out.success = (result.exitCode == 0);
            if (!out.success) {
                out.error = QString::fromUtf8(result.errorOutput)
                    + QString::fromUtf8(result.output);
            }
            completion(out);
        },
        30000);
}
