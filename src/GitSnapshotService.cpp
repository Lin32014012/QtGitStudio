#include "GitSnapshotService.h"

GitSnapshotService::GitSnapshotService(QObject *parent) : QObject(parent) {
    m_repository = new GitRepository(this);
}

GitSnapshotService::~GitSnapshotService() {}

void GitSnapshotService::setWorkingDirectory(const QString &dir) {
    // v4-P6-21R6 P1-01: keep BOTH the repository target AND the
    // bookkeeping path in sync - tests (and session fences) rely on
    // workingDirectory() reflecting the actual executor target.
    m_workingDirectory = dir;
    m_repository->setWorkingDirectory(dir);
}

GitSnapshot GitSnapshotService::createSnapshot() {
    return m_repository->snapshot();
}

void GitSnapshotService::createSnapshotAsync(GitRepository::SnapshotCompletion completion) {
    m_repository->snapshotAsync(std::move(completion));
}

GitError GitSnapshotService::lastError() const {
    return m_repository->lastError();
}
