#include "GitRepository.h"
#include "GitStatusParser.h"
#include "GitDiffParser.h"
#include "GitBranchParser.h"

namespace {
bool isUnbornHeadError(const GitCommandExecutor::Result &result) {
    const QString stderrOutput = QString::fromUtf8(result.errorOutput);
    return stderrOutput.contains("unknown revision", Qt::CaseInsensitive)
        || stderrOutput.contains("ambiguous argument 'HEAD'", Qt::CaseInsensitive)
        || stderrOutput.contains("bad revision 'HEAD'", Qt::CaseInsensitive);
}
}

GitRepository::GitRepository(QObject *parent) : QObject(parent) {
    m_executor = new GitCommandExecutor(this);
}

GitRepository::~GitRepository() {}

void GitRepository::setWorkingDirectory(const QString &dir) {
    m_executor->setWorkingDirectory(dir);
}

QString GitRepository::workingDirectory() const {
    return m_executor->workingDirectory();
}

GitSnapshot GitRepository::status() {
    m_lastError = GitError();
    auto result = m_executor->execute("status", QStringList() << "--porcelain=v2");
    if (result.error.hasError()) {
        m_lastError = result.error;
        return GitSnapshot();
    }
    return GitStatusParser::parse(result.output);
}

GitSnapshot GitRepository::diff(const QString &commit) {
    m_lastError = GitError();
    auto result = m_executor->execute("diff-tree", QStringList() << "-r" << "--numstat" << commit);
    if (result.error.hasError()) {
        m_lastError = result.error;
        return GitSnapshot();
    }
    return GitDiffParser::parse(result.output);
}

GitSnapshot GitRepository::branch() {
    m_lastError = GitError();
    QStringList format;
    format << "--format=" + QString("%(refname:short)%09%(objectname:short)%09%(refname)%09%(upstream:short)%09%(upstream:track,nobracket)%09%(HEAD)");
    auto result = m_executor->execute("for-each-ref", QStringList() << "refs/heads/" << format);
    if (result.error.hasError()) {
        m_lastError = result.error;
        return GitSnapshot();
    }
    return GitBranchParser::parse(result.output);
}

GitSnapshot GitRepository::snapshot() {
    GitSnapshot snap;
    snap.files = status().files;
    snap.diffs = diff().diffs;
    snap.branches = branch().branches;
    return snap;
}

void GitRepository::snapshotAsync(SnapshotCompletion completion) {
    // A refresh spans three commands.  Snapshot the complete execution
    // context so a repository switch cannot retarget the second or third
    // command to the newly selected worktree.
    auto *requestExecutor = new GitCommandExecutor(this);
    requestExecutor->setWorkingDirectory(m_executor->workingDirectory());
    requestExecutor->setGitPath(m_executor->gitPath());
    requestExecutor->setExtraEnvironment(m_executor->extraEnvironment());
    requestExecutor->executeAsync(
        "status",
        {"--porcelain=v2"},
        [requestExecutor, completion](GitCommandExecutor::Result statusResult) {
            if (statusResult.error.hasError()) {
                requestExecutor->deleteLater();
                completion({}, statusResult.error);
                return;
            }
            GitSnapshot snapshot = GitStatusParser::parse(statusResult.output);
            requestExecutor->executeAsync(
                "diff-tree",
                {"-r", "--numstat", "HEAD"},
                [requestExecutor, completion, snapshot](GitCommandExecutor::Result diffResult) mutable {
                    if (diffResult.error.hasError()) {
                        // An initialized repository has no commit yet, so
                        // HEAD is intentionally unresolved.  Its diff is
                        // the empty set; only unrelated git failures should
                        // fail the refresh pipeline.
                        if (isUnbornHeadError(diffResult)) {
                            snapshot.diffs.clear();
                        } else {
                            requestExecutor->deleteLater();
                            completion({}, diffResult.error);
                            return;
                        }
                    } else {
                        snapshot.diffs = GitDiffParser::parse(diffResult.output).diffs;
                    }
                    QStringList format;
                    format << "--format=" + QString("%(refname:short)%09%(objectname:short)%09%(refname)%09%(upstream:short)%09%(upstream:track,nobracket)%09%(HEAD)");
                    requestExecutor->executeAsync(
                        "for-each-ref",
                        {"refs/heads/", format.first()},
                        [requestExecutor, completion, snapshot](GitCommandExecutor::Result branchResult) mutable {
                            if (branchResult.error.hasError()) {
                                requestExecutor->deleteLater();
                                completion({}, branchResult.error);
                                return;
                            }
                            snapshot.branches = GitBranchParser::parse(branchResult.output).branches;
                            requestExecutor->deleteLater();
                            completion(snapshot, GitError());
                        });
                });
        });
}

bool GitRepository::initialize() {
    m_lastError = GitError();
    auto result = m_executor->execute("init", QStringList());
    if (result.error.hasError()) {
        m_lastError = result.error;
        return false;
    }
    return result.exitCode == 0;
}
