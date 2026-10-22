#include "GitViewModel.h"

GitViewModel::GitViewModel(QObject *parent) : QObject(parent) {
    m_service = new GitSnapshotService(this);
}

GitViewModel::~GitViewModel() {}

void GitViewModel::setWorkingDirectory(const QString &dir) {
    m_service->setWorkingDirectory(dir);
}

bool GitViewModel::isLoading() const {
    return m_loading;
}

QString GitViewModel::error() const {
    return m_error;
}

GitSnapshot GitViewModel::snapshot() const {
    return m_snapshot;
}

QStringList GitViewModel::branches() const {
    return m_branches;
}

QString GitViewModel::currentBranch() const {
    return m_currentBranch;
}

QString GitViewModel::lastCommitHash() const {
    return m_lastCommitHash;
}

QString GitViewModel::lastCommitMessage() const {
    return m_lastCommitMessage;
}

void GitViewModel::setBranches(const QStringList &branches) {
    if (m_branches != branches) {
        m_branches = branches;
        emit branchesChanged();
    }
}

void GitViewModel::setCurrentBranch(const QString &branch) {
    if (m_currentBranch != branch) {
        m_currentBranch = branch;
        emit currentBranchChanged();
    }
}

void GitViewModel::resetSession() {
    // v4-P6-21R5 P1-01: new session identity + clear loading so the new
    // repository's refresh is never blocked by the old one.
    ++m_sessionGeneration;
    if (m_loading) {
        m_loading = false;
        emit loadingChanged();
    }
}

void GitViewModel::setError(const QString &error) {
    if (m_error != error) {
        m_error = error;
        emit errorChanged();
    }
}

void GitViewModel::refresh() {
    if (m_loading) return;
    m_loading = true;
    emit loadingChanged();
    
    m_snapshot = m_service->createSnapshot();
    
    GitError err = m_service->lastError();
    if (err.hasError()) {
        m_error = err.message;
        emit errorChanged();
    } else {
        m_error.clear();
        emit errorChanged();
    }
    
    m_loading = false;
    emit loadingChanged();
    emit snapshotChanged();
}

void GitViewModel::refreshAsync(RefreshCompletion completion) {
    // v4-P6-21R5 P1-01: loading is bound to the repository session - a
    // refresh started for repo A must not block repo B's refresh, and
    // a late A snapshot must never overwrite B.
    const quint64 generation = m_sessionGeneration;
    if (m_loading) {
        GitError error;
        error.type = GitErrorType::ProcessFailed;
        error.message = "A repository refresh is already running";
        completion(error);
        return;
    }

    m_loading = true;
    emit loadingChanged();
    m_service->createSnapshotAsync([this, completion, generation](GitSnapshot snapshot, GitError error) {
        if (generation != m_sessionGeneration) {
            return;  // stale session - drop the result entirely
        }
        if (error.hasError()) {
            m_error = error.message;
            emit errorChanged();
        } else {
            m_snapshot = snapshot;
            m_error.clear();
            emit errorChanged();
            emit snapshotChanged();
        }
        m_loading = false;
        emit loadingChanged();
        completion(error);
    });
}
