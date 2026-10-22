// v4-P0-R1.3 stability: RepositoryResolver - resolves whether a dropped
// path is a git repository, with strict QProcess lifecycle management.
// Each resolve uses its OWN GitCommandExecutor (independent QProcess
// instances; never shares the controller's executor, so the controller's
// working directory can never be polluted by a probe). Single-flight:
// a second resolve while one is in flight is rejected.
#ifndef REPOSITORYRESOLVER_H
#define REPOSITORYRESOLVER_H

#include <QObject>
#include <QString>
#include <functional>

class GitCommandExecutor;

class RepositoryResolver : public QObject {
    Q_OBJECT
public:
    struct Resolution {
        bool isRepo = false;
        QString branch;
        QString error;
    };
    using Completion = std::function<void(const Resolution &)>;

    explicit RepositoryResolver(QObject *parent = nullptr);

    // Resolve `path` asynchronously. Rejected (completion NOT called) when
    // another resolve is already in flight.
    void resolve(const QString &path, Completion completion);
    bool isBusy() const { return m_busy; }
    QString pendingPath() const { return m_pendingPath; }

private:
    GitCommandExecutor *m_executor;
    bool m_busy = false;
    QString m_pendingPath;
};

#endif // REPOSITORYRESOLVER_H
