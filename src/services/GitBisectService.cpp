#include "GitBisectService.h"
#include "GitCommandExecutor.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QDir>

namespace {
// git <= 2.54 prints "first bad commit"; git >= 2.55 quotes the (possibly
// custom) bisect term: "first 'bad' commit". Match the shape, not the
// spelling, so old/new or --term-new=broken sessions are recognised too.
const QRegularExpression &firstBadLogLineRe() {
    static const QRegularExpression re(
        QStringLiteral("^# first '?[^\\s']+'? commit:"),
        QRegularExpression::MultilineOption);
    return re;
}
const QRegularExpression &firstBadOutputRe() {
    static const QRegularExpression re(
        QStringLiteral("is the first '?[^\\s']+'? commit"));
    return re;
}

// `git rev-parse --git-path` prints paths relative to the working
// directory; resolve them against it before checking existence.
bool gitPathExists(const QString &path, const QString &workingDir) {
    if (path.isEmpty()) return false;
    const QFileInfo info(path);
    if (info.isAbsolute()) return info.exists();
    return QFileInfo(QDir(workingDir), path).exists();
}

QString extractHashFromLine(const QString &line) {
    const int open = line.indexOf(QLatin1Char('['));
    const int close = line.indexOf(QLatin1Char(']'), open + 1);
    if (open >= 0 && close > open) {
        return line.mid(open + 1, close - open - 1).trimmed();
    }
    // "<hash> is the first bad commit" style: leading 40-hex token.
    const QString trimmed = line.trimmed();
    if (trimmed.size() >= 40) {
        QString token;
        for (const QChar &c : trimmed) {
            if (c.isSpace() || c == QLatin1Char(':')) break;
            token += c;
        }
        if (token.size() == 40) return token;
    }
    return QString();
}
}  // namespace

GitBisectService::GitBisectService(QObject *parent)
    : QObject(parent)
    , m_executor(new GitCommandExecutor(this)) {
}

void GitBisectService::setWorkingDirectory(const QString &path) {
    m_executor->setWorkingDirectory(path);
}

QString GitBisectService::workingDirectory() const {
    return m_executor->workingDirectory();
}

void GitBisectService::parseBisectLog(const QByteArray &logOutput,
                                      QString *goodHash, QString *badHash,
                                      QString *firstBadHash) {
    if (goodHash) goodHash->clear();
    if (badHash) badHash->clear();
    if (firstBadHash) firstBadHash->clear();
    const QList<QByteArray> lines = logOutput.split('\n');
    for (const QByteArray &lineBytes : lines) {
        const QString line = QString::fromUtf8(lineBytes);
        if (line.startsWith(QStringLiteral("# good: ["))) {
            if (goodHash) {
                *goodHash = extractHashFromLine(line);
            }
        } else if (line.startsWith(QStringLiteral("# bad: ["))) {
            if (badHash) {
                *badHash = extractHashFromLine(line);
            }
        } else if (firstBadLogLineRe().match(line).hasMatch()) {
            if (firstBadHash) {
                *firstBadHash = extractHashFromLine(line);
            }
        }
    }
}

void GitBisectService::classify(const QString &combined, int exitCode,
                                BisectCompletion completion) {
    BisectResult out;
    out.message = combined.trimmed();
    if (firstBadOutputRe().match(combined).hasMatch()
        || firstBadLogLineRe().match(combined).hasMatch()) {
        out.outcome = BisectOutcome::Found;
        QString good, bad, firstBad;
        // The first bad hash may come from the log excerpt in the output.
        parseBisectLog(combined.toUtf8(), &good, &bad, &firstBad);
        if (firstBad.isEmpty()) {
            firstBad = extractHashFromLine(combined);
        }
        out.suggestedCommit = firstBad;
        completion(out);
        return;
    }
    if (exitCode != 0) {
        if (combined.contains(QStringLiteral("not in a bisect"))
            || combined.contains(QStringLiteral("no terms defined"))
            || combined.contains(QStringLiteral("Bad bisect"))) {
            out.outcome = BisectOutcome::NotActive;
        } else {
            out.outcome = BisectOutcome::Error;
        }
        completion(out);
        return;
    }
    out.outcome = combined.contains(QStringLiteral("Bisecting:"))
                      ? BisectOutcome::Started
                      : BisectOutcome::Marked;
    completion(out);
}

void GitBisectService::collectStatus(BisectStatus *status) {
    // Active flag: BISECT_LOG present in the git dir (path from git).
    const GitCommandExecutor::Result logPath =
        m_executor->execute("rev-parse", {"--git-path", "BISECT_LOG"},
                            15000);
    const QString bisectLogPath =
        QString::fromUtf8(logPath.output).trimmed();
    status->active = gitPathExists(bisectLogPath, workingDirectory());
    if (!status->active) {
        status->message = QStringLiteral("当前没有进行中的 Bisect");
        return;
    }
    // Current test commit.
    const GitCommandExecutor::Result head =
        m_executor->execute("rev-parse", {"HEAD"}, 15000);
    if (head.exitCode == 0) {
        status->currentCommit = QString::fromUtf8(head.output).trimmed();
    }
    // Marked commits from `git bisect log`.
    const GitCommandExecutor::Result log =
        m_executor->execute("bisect", {"log"}, 15000);
    if (log.exitCode == 0) {
        QString good, bad, firstBad;
        parseBisectLog(log.output, &good, &bad, &firstBad);
        status->goodCommit = good;
        status->badCommit = bad;
        status->message =
            QStringLiteral("Bisect 进行中（good=%1，bad=%2）")
                .arg(good.left(7), bad.left(7));
    }
}

void GitBisectService::startAsync(const QString &badCommit,
                                  const QString &goodCommit,
                                  BisectCompletion completion) {
    QStringList args;
    args << "start";
    if (!badCommit.trimmed().isEmpty()) {
        args << badCommit.trimmed();
        if (!goodCommit.trimmed().isEmpty()) {
            args << goodCommit.trimmed();
        }
    }
    m_executor->executeAsync(
        "bisect", args,
        [this, completion](GitCommandExecutor::Result result) {
            const QString combined = QString::fromUtf8(result.output)
                + QString::fromUtf8(result.errorOutput);
            classify(combined, result.exitCode, completion);
        },
        30000);
}

void GitBisectService::markGoodAsync(BisectCompletion completion) {
    m_executor->executeAsync(
        "bisect", {"good"},
        [this, completion](GitCommandExecutor::Result result) {
            const QString combined = QString::fromUtf8(result.output)
                + QString::fromUtf8(result.errorOutput);
            classify(combined, result.exitCode, completion);
        },
        30000);
}

void GitBisectService::markBadAsync(BisectCompletion completion) {
    m_executor->executeAsync(
        "bisect", {"bad"},
        [this, completion](GitCommandExecutor::Result result) {
            const QString combined = QString::fromUtf8(result.output)
                + QString::fromUtf8(result.errorOutput);
            classify(combined, result.exitCode, completion);
        },
        30000);
}

void GitBisectService::resetAsync(BisectCompletion completion) {
    m_executor->executeAsync(
        "bisect", {"reset"},
        [this, completion](GitCommandExecutor::Result result) {
            const QString combined = QString::fromUtf8(result.output)
                + QString::fromUtf8(result.errorOutput);
            BisectResult out;
            out.message = combined.trimmed();
            if (result.exitCode == 0) {
                out.outcome = BisectOutcome::Reset;
            } else {
                out.outcome = BisectOutcome::Error;
            }
            completion(out);
        },
        30000);
}

void GitBisectService::statusAsync(StatusCompletion completion) {
    BisectStatus status;
    collectStatus(&status);
    completion(status);
}
