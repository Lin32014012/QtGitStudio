#pragma once
#include "GitError.h"
#include <QObject>
#include <QProcess>
#include <QByteArray>
#include <QSet>
#include <functional>

class GitCommandExecutor : public QObject {
    Q_OBJECT
public:
    explicit GitCommandExecutor(QObject *parent = nullptr);
    ~GitCommandExecutor();
    
    struct Result {
        QByteArray output;
        QByteArray errorOutput;
        int exitCode = -1;
        QProcess::ExitStatus exitStatus = QProcess::CrashExit;
        GitError error;
    };

    using Completion = std::function<void(Result)>;
    
    Result execute(const QString &command, const QStringList &args, int timeoutMs = 30000);
    // v4-P6-21R6 test seam: virtual so tests can spy/delay stages.
    virtual void executeAsync(
        const QString &command,
        const QStringList &args,
        Completion completion,
        int timeoutMs = 30000);
    // v4-P4-2: extra "KEY=VALUE" entries appended to the child process
    // environment (e.g. GIT_EDITOR=true for non-interactive rebase
    // --continue). Scoped to this executor's processes only - never
    // touches the system or git global configuration.
    void setExtraEnvironment(const QStringList &entries);
    QStringList extraEnvironment() const;
    void setWorkingDirectory(const QString &dir);
    QString workingDirectory() const;
    // v4-P2-1: custom git executable path (empty = auto-detect).
    void setGitPath(const QString &path);
    // v4-P6-21R4 P1-03: explicit per-call path (candidate detection) -
    // wins over both the instance path and the shared default.
    void setExplicitGitPath(const QString &path) {
        m_explicitGitPath = path;
    }
    QString gitPath() const;
    // v4-P6-21R5 P1-03: single authoritative executable resolution.
    QString resolveGitExecutable() const;

    // v4-P6-21 ISSUE-04: shared git executable configuration - once
    // set, every current and future executor uses this executable.
    static void setDefaultGitExecutable(const QString &path);
    static QString defaultGitExecutable();
    
private:
    QString m_workingDirectory;
    QString m_gitPath;
    bool m_followsDefault = true;
    // v4-P6-21R4 P1-03: per-call explicit path (candidate detection).
    QString m_explicitGitPath;
    QStringList m_extraEnvironment;
    // v4-P6-21 ISSUE-04: shared app-wide git executable (empty = auto).
    static QString s_defaultGitPath;
    static QSet<GitCommandExecutor *> s_instances;
};
