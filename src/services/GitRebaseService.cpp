#include "GitRebaseService.h"
#include "GitCommandExecutor.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>

namespace {
const char *outcomeName(RebaseOutcome outcome) {
    switch (outcome) {
    case RebaseOutcome::Success: return "Success";
    case RebaseOutcome::UpToDate: return "UpToDate";
    case RebaseOutcome::Conflict: return "Conflict";
    case RebaseOutcome::InProgress: return "InProgress";
    case RebaseOutcome::Rejected: return "Rejected";
    case RebaseOutcome::Failed: return "Failed";
    }
    return "Unknown";
}

QStringList parseUnmergedPaths(const QByteArray &output) {
    QStringList files;
    const QList<QByteArray> lines = output.split('\n');
    for (const QByteArray &line : lines) {
        if (line.size() < 4) continue;
        const char x = line.at(0);
        const char y = line.at(1);
        const bool unmerged = (x == 'U' || y == 'U'
                               || (x == 'A' && y == 'A')
                               || (x == 'D' && y == 'D'));
        if (!unmerged) continue;
        QByteArray path = line.mid(3).trimmed();
        if (path.size() >= 2 && path.startsWith('"') && path.endsWith('"')) {
            path = path.mid(1, path.size() - 2);
        }
        files.append(QString::fromUtf8(path));
    }
    return files;
}
}  // namespace

GitRebaseService::GitRebaseService(QObject *parent)
    : QObject(parent)
    , m_executor(new GitCommandExecutor(this))
    , m_continueExecutor(new GitCommandExecutor(this)) {
    // Non-interactive rebase --continue: never open a terminal editor.
    // Scoped to this executor's child processes only.
    m_continueExecutor->setExtraEnvironment(
        {QStringLiteral("GIT_EDITOR=true")});
}

void GitRebaseService::setWorkingDirectory(const QString &path) {
    m_executor->setWorkingDirectory(path);
    m_continueExecutor->setWorkingDirectory(path);
}

QString GitRebaseService::workingDirectory() const {
    return m_executor->workingDirectory();
}

// `git rev-parse --git-path` prints paths relative to the working
// directory; resolve them against it before checking existence.
static bool gitPathExists(const QString &path, const QString &workingDir) {
    if (path.isEmpty()) return false;
    const QFileInfo info(path);
    if (info.isAbsolute()) return info.exists();
    return QFileInfo(QDir(workingDir), path).exists();
}

void GitRebaseService::isRebaseInProgressAsync(BoolCompletion completion) {
    const GitCommandExecutor::Result mergeDir =
        m_executor->execute("rev-parse", {"--git-path", "rebase-merge"},
                            15000);
    const GitCommandExecutor::Result applyDir =
        m_executor->execute("rev-parse", {"--git-path", "rebase-apply"},
                            15000);
    const QString mergePath = QString::fromUtf8(mergeDir.output).trimmed();
    const QString applyPath = QString::fromUtf8(applyDir.output).trimmed();
    const bool inProgress =
        gitPathExists(mergePath, workingDirectory())
        || gitPathExists(applyPath, workingDirectory());
    completion(inProgress, inProgress
                              ? QStringLiteral("已有 Rebase 正在进行")
                              : QString());
}

bool GitRebaseService::preconditionsOk(const QString &targetBranch,
                                       QString *reason) {
    // 1. Repository valid / git reachable.
    const GitCommandExecutor::Result head = m_executor->execute(
        "rev-parse", {"--abbrev-ref", "HEAD"}, 15000);
    if (head.exitCode != 0) {
        *reason = QStringLiteral("当前仓库无效或 Git 不可用");
        return false;
    }
    const QString currentBranch = QString::fromUtf8(head.output).trimmed();

    // 2. Not a detached HEAD.
    if (currentBranch == QStringLiteral("HEAD")) {
        *reason = QStringLiteral("当前处于 Detached HEAD 状态，无法执行 Rebase");
        return false;
    }
    // 3. Current branch exists (implied by the symbolic ref).
    if (currentBranch.isEmpty()) {
        *reason = QStringLiteral("无法确定当前分支");
        return false;
    }
    // 4. Target branch exists.
    const GitCommandExecutor::Result verify = m_executor->execute(
        "rev-parse", {"--verify", "--quiet", targetBranch + "^{commit}"},
        15000);
    if (verify.exitCode != 0) {
        *reason = QStringLiteral("目标分支 %1 不存在").arg(targetBranch);
        return false;
    }
    // 5. Target branch is not the current branch.
    if (targetBranch == currentBranch) {
        *reason = QStringLiteral("目标分支与当前分支相同（%1），无需 Rebase")
                      .arg(currentBranch);
        return false;
    }
    // 6. Working tree and index are clean.
    const GitCommandExecutor::Result status =
        m_executor->execute("status", {"--porcelain"}, 15000);
    if (status.exitCode == 0 && !status.output.trimmed().isEmpty()) {
        *reason = QStringLiteral(
            "当前存在未提交修改。\n请先 Commit 或 Stash，再执行 Rebase。");
        return false;
    }
    // 7. No rebase already in progress. The git dir paths come from
    // `git rev-parse --git-path`; only existence is checked here.
    const GitCommandExecutor::Result mergeDir = m_executor->execute(
        "rev-parse", {"--git-path", "rebase-merge"}, 15000);
    const GitCommandExecutor::Result applyDir = m_executor->execute(
        "rev-parse", {"--git-path", "rebase-apply"}, 15000);
    const QString mergePath = QString::fromUtf8(mergeDir.output).trimmed();
    const QString applyPath = QString::fromUtf8(applyDir.output).trimmed();
    if (gitPathExists(mergePath, workingDirectory())
        || gitPathExists(applyPath, workingDirectory())) {
        *reason = QStringLiteral("已有 Rebase 正在进行，请先继续或放弃");
        return false;
    }
    return true;
}

void GitRebaseService::collectConflictedFiles(RebaseResult *out) {
    const GitCommandExecutor::Result status =
        m_executor->execute("status", {"--porcelain"}, 15000);
    if (status.exitCode == 0) {
        out->conflictedFiles = parseUnmergedPaths(status.output);
    }
}

void GitRebaseService::classify(const GitCommandExecutor::Result &result,
                                RebaseCompletion completion) {
    RebaseResult out;
    const QString combined = QString::fromUtf8(result.output)
        + QString::fromUtf8(result.errorOutput);
    out.message = combined.trimmed();

    if (combined.contains(QStringLiteral("up to date"), Qt::CaseInsensitive)) {
        out.outcome = RebaseOutcome::UpToDate;
    } else if (combined.contains(QStringLiteral("CONFLICT"))
               || combined.contains(QStringLiteral("could not apply"))
               || combined.contains(QStringLiteral("error: could not"))) {
        out.outcome = RebaseOutcome::Conflict;
        collectConflictedFiles(&out);
    } else if (result.exitCode == 0) {
        out.outcome = RebaseOutcome::Success;
    } else {
        out.outcome = RebaseOutcome::Failed;
    }
    completion(out);
}

void GitRebaseService::rebaseAsync(const QString &targetBranch,
                                   RebaseCompletion completion) {
    QString reason;
    if (!preconditionsOk(targetBranch, &reason)) {
        RebaseResult out;
        out.outcome = RebaseOutcome::Rejected;
        out.message = reason;
        completion(out);
        return;
    }
    m_executor->executeAsync(
        "rebase", {targetBranch},
        [this, completion](GitCommandExecutor::Result result) {
            classify(result, completion);
        },
        120000);
}

void GitRebaseService::continueAsync(RebaseCompletion completion) {
    m_continueExecutor->executeAsync(
        "rebase", {"--continue"},
        [this, completion](GitCommandExecutor::Result result) {
            // Result uses the same classification (may conflict again).
            classify(result, completion);
        },
        120000);
}

void GitRebaseService::abortAsync(BoolCompletion completion) {
    m_executor->executeAsync(
        "rebase", {"--abort"},
        [completion](GitCommandExecutor::Result result) {
            const bool ok = (result.exitCode == 0);
            completion(ok, ok ? QString()
                              : QString::fromUtf8(result.errorOutput));
        },
        30000);
}
