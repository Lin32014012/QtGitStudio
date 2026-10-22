#include "GitMergeService.h"
#include "GitCommandExecutor.h"

GitMergeService::GitMergeService(QObject *parent)
    : QObject(parent)
    , m_executor(new GitCommandExecutor(this)) {
}

void GitMergeService::setWorkingDirectory(const QString &path) {
    m_executor->setWorkingDirectory(path);
}

QString GitMergeService::workingDirectory() const {
    return m_executor->workingDirectory();
}

GitMergeService::MergeKind GitMergeService::classify(
    const QString &combined, int exitCode) const {
    if (combined.contains("CONFLICT") || combined.contains("Automatic merge failed")
        || combined.contains("conflict")) {
        return MergeKind::Conflict;
    }
    if (combined.contains("Fast-forward")) {
        return MergeKind::FastForward;
    }
    if (exitCode == 0) {
        // Successful non-FF merge produces "Merge made by the 'ort' strategy."
        // An empty successful merge (already up to date) counts as FF-ish.
        return MergeKind::MergeCommit;
    }
    return MergeKind::Error;
}

void GitMergeService::mergeAsync(const QString &branch, MergeCompletion completion) {
    m_executor->executeAsync(
        "merge", {branch},
        [this, completion](GitCommandExecutor::Result result) {
            MergeResult out;
            const QString combined =
                QString::fromUtf8(result.output)
                + QString::fromUtf8(result.errorOutput);
            const MergeKind kind = classify(combined, result.exitCode);
            out.kind = kind;
            out.conflict = (kind == MergeKind::Conflict);
            out.fastForward = (kind == MergeKind::FastForward);
            out.success = (kind == MergeKind::FastForward
                           || kind == MergeKind::MergeCommit);
            if (!out.success) {
                out.error = combined;
            }
            completion(out);
        },
        60000);
}

void GitMergeService::abortAsync(
    std::function<void(bool, const QString &)> completion) {
    m_executor->executeAsync(
        "merge", {"--abort"},
        [completion](GitCommandExecutor::Result result) {
            const bool ok = (result.exitCode == 0);
            completion(ok, ok ? QString()
                              : QString::fromUtf8(result.errorOutput));
        },
        30000);
}
