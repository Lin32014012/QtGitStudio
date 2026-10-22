#include "GitEnvironmentService.h"
#include "GitCommandExecutor.h"

#include <QRegularExpression>
#include <QFileInfo>

GitEnvironmentService::GitEnvironmentService(QObject *parent)
    : QObject(parent)
    , m_executor(new GitCommandExecutor(this)) {
}

QString GitEnvironmentService::parseVersion(const QString &rawVersion) {
    // Typical: "git version 2.39.1.windows.1" or "git version 2.39.1"
    QRegularExpression re(
        QStringLiteral("git\\s+version\\s+([0-9]+\\.[0-9]+(?:\\.[0-9]+)?)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(rawVersion.trimmed());
    if (m.hasMatch()) {
        return m.captured(1);
    }
    // Fallback: first dotted number token anywhere.
    QRegularExpression fallback(QStringLiteral("([0-9]+\\.[0-9]+(?:\\.[0-9]+)?)"));
    const QRegularExpressionMatch fm = fallback.match(rawVersion);
    if (fm.hasMatch()) {
        return fm.captured(1);
    }
    return QString();
}

GitEnvironmentService::GitEnvironment GitEnvironmentService::detectSync(
    const QString &customPath) {
    GitEnvironment env;
    QString path = customPath;
    if (path.isEmpty()) {
        path = m_executor->gitPath();
        if (path == QStringLiteral("git")) {
            path.clear();  // auto-detect marker, resolve below
        }
    }
    if (path.isEmpty()) {
        // Let the executor resolve via PATH; if that fails the process
        // simply cannot start.
        path = QStringLiteral("git");
    }
    // Detection is exploratory: execute the candidate itself without
    // changing the service or application default. The dialog commits a
    // successful candidate only after the user accepts it.
    m_executor->setExplicitGitPath(path);
    env.gitPath = path;

    const GitCommandExecutor::Result result =
        m_executor->execute(QStringLiteral("--version"), {}, 10000);
    m_executor->setExplicitGitPath({});
    if (result.exitCode != 0 || result.exitStatus != QProcess::NormalExit) {
        env.available = false;
        env.error = QString::fromUtf8(result.errorOutput).trimmed();
        if (env.error.isEmpty()) {
            env.error = result.error.message;
        }
        return env;
    }
    env.available = true;
    env.rawVersion = QString::fromUtf8(result.output).trimmed();
    env.version = parseVersion(env.rawVersion);
    if (!customPath.isEmpty()) {
        env.gitPath = QFileInfo(customPath).absoluteFilePath();
    }
    return env;
}

void GitEnvironmentService::detectAsync(const QString &customPath,
                                        Completion completion) {
    const GitEnvironment env = detectSync(customPath);
    completion(env);
}

QString GitEnvironmentService::gitPath() const {
    return m_executor ? m_executor->gitPath() : QString();
}

void GitEnvironmentService::setGitPath(const QString &path) {
    if (m_executor) {
        m_executor->setGitPath(path);
    }
}
