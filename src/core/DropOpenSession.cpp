#include "DropOpenSession.h"

#include "RepositoryResolver.h"

DropOpenSession::DropOpenSession(RepositoryResolver *resolver, QObject *parent)
    : QObject(parent)
    , m_resolver(resolver) {
    m_watchdog.setSingleShot(true);
    m_watchdog.setInterval(30000);  // probe must finish within 30s
    connect(&m_watchdog, &QTimer::timeout,
            this, &DropOpenSession::onWatchdogTimeout);
}

bool DropOpenSession::begin(const QString &path) {
    if (isActive()) {
        emit rejected(QStringLiteral("另一个仓库正在打开中"));
        return false;
    }
    m_state = State::Resolving;
    m_path = path;
    m_watchdog.start();
    m_resolver->resolve(path, [this, path](const RepositoryResolver::Resolution &resolution) {
        if (m_state != State::Resolving || m_path != path) {
            return;  // cancelled, superseded, or timed out
        }
        stopWatchdog();
        m_state = State::Opening;
        emit resolved(path, resolution.isRepo, resolution.branch);
    });
    return true;
}

void DropOpenSession::finish() {
    if (m_state == State::Idle) {
        return;
    }
    stopWatchdog();
    m_state = State::Done;
    m_path.clear();
    emit finished();
}

void DropOpenSession::cancel() {
    if (m_state != State::Resolving) {
        return;
    }
    // Path staleness check inside the resolver keeps the pending probe
    // from delivering; we simply drop back to Idle.
    stopWatchdog();
    m_path.clear();
    m_state = State::Idle;
}

void DropOpenSession::stopWatchdog() {
    if (m_watchdog.isActive()) {
        m_watchdog.stop();
    }
}

void DropOpenSession::onWatchdogTimeout() {
    // Probe never completed: abort the session so the overlay is hidden
    // and future drops are accepted again. The pending resolver callback
    // (if it ever arrives) is discarded by the staleness check above.
    m_path.clear();
    m_state = State::Idle;
    emit rejected(QStringLiteral("打开仓库超时，请重试"));
}
