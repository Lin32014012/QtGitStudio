#include "GitCommandExecutor.h"

#include <QStandardPaths>
#include <QTimer>

#include <memory>
#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

GitCommandExecutor::GitCommandExecutor(QObject *parent) : QObject(parent) {
    // v4-P6-21 ISSUE-04: a globally configured executable (set from
    // the Git environment dialog) wins over auto-detection - every
    // current and future executor uses the SAME configured git.
    if (!s_defaultGitPath.isEmpty()) {
        m_gitPath = s_defaultGitPath;
    } else {
        m_gitPath = QStandardPaths::findExecutable("git");
        if (m_gitPath.isEmpty()) {
            m_gitPath = "git";
        }
    }
    s_instances.insert(this);
}

GitCommandExecutor::~GitCommandExecutor() {
    s_instances.remove(this);
    const auto processes = findChildren<QProcess *>(QString(), Qt::FindDirectChildrenOnly);
    for (QProcess *process : processes) {
        if (process->state() == QProcess::NotRunning) {
            continue;
        }
        process->disconnect();
        process->blockSignals(true);
        process->terminate();
        if (!process->waitForFinished(500)) {
            process->kill();
            process->waitForFinished(1000);
        }
    }
}

GitCommandExecutor::Result GitCommandExecutor::execute(const QString &command, const QStringList &args, int timeoutMs) {
    Result result;
    QProcess process;
    
    if (!m_workingDirectory.isEmpty()) {
        process.setWorkingDirectory(m_workingDirectory);
    }
    
    QStringList fullArgs{command};
    fullArgs.append(args);

    // v4-P6-21R4 P1-03: an explicit path wins over the shared default -
    // candidate detection must execute the CANDIDATE, never the global
    // executable.
    const QString git = resolveGitExecutable();
    process.setProgram(git);
    process.setArguments(fullArgs);
    if (!m_extraEnvironment.isEmpty()) {
        process.setEnvironment(QProcess::systemEnvironment()
                               + m_extraEnvironment);
    }
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
        args->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
        args->startupInfo->wShowWindow = SW_HIDE;
    });
#endif
    process.start();
    
    if (!process.waitForStarted(5000)) {
        result.error.type = GitErrorType::ProcessFailed;
        result.error.message = "Failed to start git process";
        return result;
    }
    
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished();
        result.output = process.readAllStandardOutput();
        result.errorOutput = process.readAllStandardError();
        result.exitCode = process.exitCode();
        result.exitStatus = process.exitStatus();
        result.error.type = GitErrorType::Timeout;
        result.error.message = "Git command timed out";
        result.error.exitCode = result.exitCode;
        result.error.stderrOutput = QString::fromUtf8(result.errorOutput);
        return result;
    }
    
    result.output = process.readAllStandardOutput();
    result.errorOutput = process.readAllStandardError();
    result.exitCode = process.exitCode();
    result.exitStatus = process.exitStatus();
    
    if (process.exitStatus() != QProcess::NormalExit) {
        result.error.type = GitErrorType::ProcessFailed;
        result.error.message = "Process crashed or was killed";
    } else if (result.exitCode != 0) {
        result.error.type = GitErrorType::ProcessFailed;
        result.error.message = QString("Command failed with exit code %1").arg(result.exitCode);
        result.error.exitCode = result.exitCode;
        result.error.stderrOutput = QString::fromUtf8(result.errorOutput);
    }
    
    return result;
}

void GitCommandExecutor::executeAsync(
    const QString &command,
    const QStringList &args,
    Completion completion,
    int timeoutMs) {
    auto *process = new QProcess(this);
    if (!m_workingDirectory.isEmpty()) {
        process->setWorkingDirectory(m_workingDirectory);
    }

    QStringList fullArgs{command};
    fullArgs.append(args);
    process->setProgram(resolveGitExecutable());
    process->setArguments(fullArgs);
    if (!m_extraEnvironment.isEmpty()) {
        process->setEnvironment(QProcess::systemEnvironment()
                                + m_extraEnvironment);
    }
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
        args->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
        args->startupInfo->wShowWindow = SW_HIDE;
    });
#endif

    auto *timeout = new QTimer(process);
    timeout->setSingleShot(true);
    const auto completed = std::make_shared<bool>(false);
    const auto timedOut = std::make_shared<bool>(false);
    auto finish = [process, timeout, completed, timedOut, completion](
                            int exitCode,
                            QProcess::ExitStatus exitStatus) mutable {
        if (*completed) {
            return;
        }
        *completed = true;
        timeout->stop();

        Result result;
        result.output = process->readAllStandardOutput();
        result.errorOutput = process->readAllStandardError();
        result.exitCode = exitCode;
        result.exitStatus = exitStatus;
        if (*timedOut) {
            result.error.type = GitErrorType::Timeout;
            result.error.message = "Git command timed out";
        } else if (exitStatus != QProcess::NormalExit) {
            result.error.type = GitErrorType::ProcessFailed;
            result.error.message = "Process crashed or was killed";
        } else if (exitCode != 0) {
            result.error.type = GitErrorType::ProcessFailed;
            result.error.message = QString("Command failed with exit code %1").arg(exitCode);
        }
        if (result.error.hasError()) {
            result.error.exitCode = exitCode;
            result.error.stderrOutput = QString::fromUtf8(result.errorOutput);
        }

        process->deleteLater();
        completion(result);
    };

    connect(process, &QProcess::finished, this, finish);
    connect(
        process,
        &QProcess::errorOccurred,
        this,
        [process, finish](QProcess::ProcessError error) mutable {
            if (error != QProcess::FailedToStart) {
                return;
            }
            finish(-1, QProcess::CrashExit);
        });
    connect(timeout, &QTimer::timeout, this, [process, timedOut]() {
        *timedOut = true;
        process->kill();
    });

    process->start();
    timeout->start(timeoutMs);
}

void GitCommandExecutor::setExtraEnvironment(const QStringList &entries) {
    m_extraEnvironment = entries;
}

QStringList GitCommandExecutor::extraEnvironment() const {
    return m_extraEnvironment;
}

void GitCommandExecutor::setWorkingDirectory(const QString &dir) {
    m_workingDirectory = dir;
}

QString GitCommandExecutor::workingDirectory() const {
    return m_workingDirectory;
}

QString GitCommandExecutor::gitPath() const {
    return resolveGitExecutable();
}

void GitCommandExecutor::setGitPath(const QString &path) {
    m_followsDefault = false;
    m_gitPath = path.isEmpty() ? QStringLiteral("git") : path;
}

// v4-P6-21R5 P1-03: SINGLE authoritative resolution for every executor
// and every overload (sync + async). Priority:
//   1. explicit per-operation override (candidate detection)
//   2. validated global provider (shared default)
//   3. instance path (auto-detected / explicitly set)
QString GitCommandExecutor::resolveGitExecutable() const {
    if (!m_explicitGitPath.isEmpty()) {
        return m_explicitGitPath;
    }
    if (!s_defaultGitPath.isEmpty()) {
        return s_defaultGitPath;
    }
    return m_gitPath;
}

// v4-P6-21 ISSUE-04: shared executable configuration.
QString GitCommandExecutor::s_defaultGitPath;
QSet<GitCommandExecutor *> GitCommandExecutor::s_instances;
void GitCommandExecutor::setDefaultGitExecutable(const QString &path) {
    const QString selected = path.trimmed();
    if (selected.isEmpty()) {
        s_defaultGitPath.clear();
        QString autoPath = QStandardPaths::findExecutable("git");
        if (autoPath.isEmpty()) {
            autoPath = QStringLiteral("git");
        }
        for (GitCommandExecutor *executor : std::as_const(s_instances)) {
            if (executor && executor->m_followsDefault) {
                executor->m_gitPath = autoPath;
            }
        }
        return;
    }
    s_defaultGitPath = selected;
    for (GitCommandExecutor *executor : std::as_const(s_instances)) {
        if (executor && executor->m_followsDefault) {
            executor->m_gitPath = selected;
        }
    }
}
QString GitCommandExecutor::defaultGitExecutable() {
    return s_defaultGitPath;
}
