#include "GitTestRepository.h"
#include <QProcess>
#include <QFile>
#include <QDebug>

GitTestRepository::GitTestRepository() {
    m_dir = std::make_unique<QTemporaryDir>();
    if (!m_dir->isValid()) {
        qWarning() << "Failed to create temp dir";
        return;
    }
    
    QString repoPath = m_dir->path();
    
    if (!runGit({"init", repoPath})) return;
    if (!runGit({"-C", repoPath, "config", "user.name", "QtGitStudio Test"})) return;
    if (!runGit({"-C", repoPath, "config", "user.email", "qtgitstudio@example.invalid"})) return;
    
    QFile file(repoPath + "/README.md");
    file.open(QIODevice::WriteOnly);
    file.write("# Test Repository");
    file.close();
    
    if (!runGit({"-C", repoPath, "add", "README.md"})) return;
    if (!runGit({"-C", repoPath, "commit", "-m", "initial commit"})) return;
    if (!runGit({"-C", repoPath, "branch", "-M", "main"})) return;
}

GitTestRepository::~GitTestRepository() = default;

bool GitTestRepository::runGit(const QStringList &args) {
    QProcess proc;
    proc.start("git", args);
    
    if (!proc.waitForStarted(5000)) {
        qWarning() << "Git failed to start:" << "git" << args;
        return false;
    }
    
    proc.waitForFinished(10000);
    
    if (proc.exitStatus() != QProcess::NormalExit) {
        qWarning() << "Git process crashed:" << "git" << args;
        return false;
    }
    
    if (proc.exitCode() != 0) {
        qWarning() << "Git command failed:" << "git" << args;
        qWarning() << "stdout:" << proc.readAllStandardOutput();
        qWarning() << "stderr:" << proc.readAllStandardError();
        return false;
    }
    
    return true;
}
