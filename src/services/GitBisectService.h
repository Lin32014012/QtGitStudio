// v4-P4-3 Git Bisect Management MVP: start / mark good / mark bad /
// status / reset.
//   - start:  git bisect start [<bad> [<good>]]
//   - good:   git bisect good
//   - bad:    git bisect bad
//   - reset:  git bisect reset
//   - status: git bisect log (+ git rev-parse --git-path BISECT_LOG for
//             the active flag; path resolution happens in the service)
// git bisect run / auto-test scripts / CI integration are OUT of scope.
#ifndef GITBISECTSERVICE_H
#define GITBISECTSERVICE_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <functional>

class GitCommandExecutor;

struct BisectStatus {
    bool active = false;
    QString currentCommit;   // HEAD while bisecting (empty when idle)
    QString goodCommit;      // last marked good commit
    QString badCommit;       // last marked bad commit
    QString message;         // human-readable summary / log excerpt
};

enum class BisectOutcome {
    Started,      // bisect started / still narrowing down
    Marked,       // good/bad marked, still narrowing down
    Found,        // first bad commit identified
    Reset,        // bisect reset (now inactive)
    NotActive,    // operation requires an active bisect
    Error,
};

struct BisectResult {
    BisectOutcome outcome = BisectOutcome::Error;
    QString message;
    QString suggestedCommit;  // Found: the first bad commit hash
};

class GitBisectService : public QObject {
    Q_OBJECT
public:
    using BisectCompletion = std::function<void(const BisectResult &)>;
    using StatusCompletion = std::function<void(const BisectStatus &)>;

    explicit GitBisectService(QObject *parent = nullptr);

    void setWorkingDirectory(const QString &path);
    QString workingDirectory() const;

    // git bisect start [<bad> [<good>]]. Empty = omitted.
    void startAsync(const QString &badCommit, const QString &goodCommit,
                    BisectCompletion completion);

    // git bisect good / bad (current HEAD).
    void markGoodAsync(BisectCompletion completion);
    void markBadAsync(BisectCompletion completion);

    // git bisect reset.
    void resetAsync(BisectCompletion completion);

    // Current bisect status. The completion is invoked synchronously
    // (all probes are synchronous) - callers must not wrap it in an
    // event loop.
    void statusAsync(StatusCompletion completion);

    // Pure parser for `git bisect log` output: extracts the last marked
    // good hash, bad hash and a "first bad commit" hash if present.
    static void parseBisectLog(const QByteArray &logOutput,
                               QString *goodHash, QString *badHash,
                               QString *firstBadHash);

private:
    void classify(const QString &combined, int exitCode,
                  BisectCompletion completion);
    void collectStatus(BisectStatus *status);

    GitCommandExecutor *m_executor;
};

#endif // GITBISECTSERVICE_H
