// v4-P0-R1.3 / v4-P1-4.1: DropOpenSession - unified lifecycle for a
// drag-and-drop open operation. State machine: Idle -> Resolving ->
// Opening -> Done. A second drop while a session is active is rejected
// (no overlay leak, no concurrent loadProject). `finish()` is called by
// the host once loadProject completes; `cancel()` aborts a resolving
// session. v4-P1-4.1 hardening: a watchdog timer aborts a stuck probe so
// the overlay can never remain visible and future drops are never
// blocked forever; every exit path (resolve completion, timeout, cancel,
// finish) converges on Idle/Done with the timer stopped.
#ifndef DROPOPENSESSION_H
#define DROPOPENSESSION_H

#include <QObject>
#include <QString>
#include <QTimer>

class RepositoryResolver;

class DropOpenSession : public QObject {
    Q_OBJECT
public:
    enum class State {
        Idle,
        Resolving,   // repository probe in flight
        Opening,     // probe succeeded, host is loading the project
        Done,        // loadProject completed or failed
    };

    explicit DropOpenSession(RepositoryResolver *resolver, QObject *parent = nullptr);

    // Start a session for `path`. Returns false (and emits `rejected`)
    // when a session is already active. On success the resolver probes
    // the path and `resolved(path, isRepo, branch)` is emitted exactly
    // once.
    bool begin(const QString &path);
    // Host calls this after loadProject finished (success or failure).
    void finish();
    // Abort while resolving; resolves to Idle without emitting resolved.
    void cancel();

    bool isActive() const { return m_state != State::Idle && m_state != State::Done; }
    State state() const { return m_state; }
    QString path() const { return m_path; }

signals:
    void resolved(const QString &path, bool isRepo, const QString &branch);
    void finished();
    void rejected(const QString &reason);

private:
    void stopWatchdog();
    void onWatchdogTimeout();

    RepositoryResolver *m_resolver;
    QTimer m_watchdog;
    State m_state = State::Idle;
    QString m_path;
};

#endif // DROPOPENSESSION_H
