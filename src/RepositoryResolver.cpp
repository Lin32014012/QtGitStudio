#include "RepositoryResolver.h"

#include "GitCommandExecutor.h"

RepositoryResolver::RepositoryResolver(QObject *parent)
    : QObject(parent)
    , m_executor(new GitCommandExecutor(this)) {
}

void RepositoryResolver::resolve(const QString &path, Completion completion) {
    if (m_busy) {
        return;  // single-flight: never run two probes at once
    }
    m_busy = true;
    m_pendingPath = path;
    m_executor->setWorkingDirectory(path);

    // Probe 1: is the path inside a work tree?
    m_executor->executeAsync(
        QStringLiteral("rev-parse"),
        {QStringLiteral("--is-inside-work-tree")},
        [this, path, completion](GitCommandExecutor::Result result) {
            if (!m_busy || m_pendingPath != path) {
                return;  // stale completion (session cancelled/restarted)
            }
            // NOTE: use Result::exitCode, NOT error.exitCode (which stays
            // -1 on success - it is only filled when hasError()).
            const bool isRepo = (result.exitCode == 0);
            if (!isRepo) {
                Resolution resolution;
                resolution.error = result.error.stderrOutput;
                m_busy = false;
                m_pendingPath.clear();
                completion(resolution);
                return;
            }
            // Probe 2 (only inside a work tree): current branch name.
            m_executor->executeAsync(
                QStringLiteral("branch"),
                {QStringLiteral("--show-current")},
                [this, path, completion](GitCommandExecutor::Result branchResult) {
                    if (!m_busy || m_pendingPath != path) {
                        return;  // stale
                    }
                    Resolution resolution;
                    resolution.isRepo = true;
                    if (branchResult.exitCode == 0) {
                        resolution.branch =
                            QString::fromUtf8(branchResult.output).trimmed();
                    }
                    m_busy = false;
                    m_pendingPath.clear();
                    completion(resolution);
                });
        });
}
