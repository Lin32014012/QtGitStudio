#include "ProjectController.h"
#include "GitCommandExecutor.h"
#include <QSettings>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDebug>

namespace {
QString quotedArgument(const QString &argument) {
    if (!argument.contains(' ') && !argument.contains('\t') && !argument.contains('"')) {
        return argument;
    }
    QString escaped = argument;
    escaped.replace('"', "\\\"");
    return '"' + escaped + '"';
}
}

QString ProjectController::CloneResult::details() const {
    QStringList renderedArguments;
    for (const QString &argument : arguments) {
        renderedArguments.append(quotedArgument(argument));
    }
    const QString renderedExitStatus =
        exitStatus == QProcess::NormalExit ? QStringLiteral("正常退出") : QStringLiteral("异常退出或未启动");
    const QFileInfo sourceInfo(source);
    const QString sourceCheck = sourceInfo.isAbsolute() && !sourceInfo.exists()
        ? QStringLiteral("源检查：does not exist（本地源路径不存在）\n")
        : QString();
    return QStringLiteral(
               "Git 可执行文件：%1\n"
               "实际参数：%2\n"
               "源地址：%3\n"
               "目标路径：%4\n"
               "%5"
               "退出状态：%6\n"
               "退出代码：%7\n\n"
               "标准输出：\n%8\n\n"
               "标准错误：\n%9")
        .arg(
            gitProgram,
            renderedArguments.join(' '),
            source,
            QDir::toNativeSeparators(targetPath),
            sourceCheck,
            renderedExitStatus,
            QString::number(exitCode),
            stdoutText.isEmpty() ? QStringLiteral("（空）") : stdoutText,
            stderrText.isEmpty() ? QStringLiteral("（空）") : stderrText);
}

ProjectController::ProjectController(QObject *parent)
    : ProjectController(new QSettings(QSettings::IniFormat, QSettings::UserScope,
                                "QtGitStudio", "QtGitStudio"), parent) {
    m_settings->setParent(this);
}

ProjectController::ProjectController(QSettings *settings, QObject *parent)
    : QObject(parent), m_settings(settings) {
    m_executor = new GitCommandExecutor(this);
}

// v4-P6-7: one-time migration of the legacy NativeFormat (registry)
// recent-projects list into the current IniFormat store. No-op when:
//   - a migration marker already exists (second start), or
//   - the new store already has recentProjects data (never overwrite).
// Only existing directories are migrated, duplicates are removed while
// preserving the original order, the legacy registry data is kept, and
// a migration/version marker prevents repeated migration on every start.
// Tests inject a fake legacy store; production uses the real registry
// (NativeFormat, UserScope, QtGitStudio/QtGitStudio).
void ProjectController::migrateLegacyRecentProjects(QSettings *legacyStore) {
    if (m_settings->contains(QStringLiteral("migration/version"))) {
        return;  // already migrated in a previous run
    }

    const QStringList existing =
        m_settings->value(QStringLiteral("recentProjects")).toStringList();
    if (!existing.isEmpty()) {
        // New store already has data: never overwrite it.
        m_settings->setValue(QStringLiteral("migration/version"), 1);
        return;
    }

    QSettings ownedLegacy(QSettings::NativeFormat, QSettings::UserScope,
                          QStringLiteral("QtGitStudio"),
                          QStringLiteral("QtGitStudio"));
    QSettings *legacy = legacyStore ? legacyStore : &ownedLegacy;
    const QStringList oldProjects =
        legacy->value(QStringLiteral("recentProjects")).toStringList();

    // Filter to existing directories only; keep first-seen order.
    QStringList migrated;
    for (const QString &path : oldProjects) {
        if (QFileInfo(path).isDir() && !migrated.contains(path)) {
            migrated.append(path);
        }
    }

    if (!migrated.isEmpty()) {
        m_settings->setValue(QStringLiteral("recentProjects"), migrated);
    }
    m_settings->setValue(QStringLiteral("migration/version"), 1);
}

ProjectController::~ProjectController() {}

ProjectController::ProjectInfo ProjectController::detectProject(const QString &path) {
    ProjectInfo info;
    
    // Check if .git exists
    QString gitDir = path + "/.git";
    if (!QFile::exists(gitDir)) {
        info.isGitRepo = false;
        info.status = "Not a Git repository";
        return info;
    }
    
    // Check if inside work tree
    int exitCode = 0;
    QString output;
    QString stderrOutput;
    bool ok = runGit(path, {"rev-parse", "--is-inside-work-tree"}, &exitCode, &output, &stderrOutput);
    
    if (!ok || exitCode != 0) {
        info.isGitRepo = false;
        info.status = "Not a Git repository";
        return info;
    }
    
    info.isGitRepo = true;
    
    // Get current branch
    ok = runGit(path, {"branch", "--show-current"}, &exitCode, &output, &stderrOutput);
    if (ok && exitCode == 0) {
        info.branch = output.trimmed();
        if (info.branch.isEmpty()) {
            info.branch = "(detached)";
        }
    } else {
        info.branch = "(unknown)";
    }
    
    // Get status
    ok = runGit(path, {"status", "--porcelain"}, &exitCode, &output, &stderrOutput);
    if (ok && exitCode == 0) {
        info.status = output.isEmpty() ? "Clean" : "Modified";
    } else {
        info.status = "Unknown";
    }
    
    return info;
}

bool ProjectController::initializeGit(const QString &path) {
    m_lastError.clear();
    int exitCode = 0;
    QString stderrOutput;
    bool ok = runGit(path, {"init"}, &exitCode, nullptr, &stderrOutput);
    
    if (!ok || exitCode != 0) {
        m_lastError = stderrOutput.trimmed();
        if (m_lastError.isEmpty()) {
            m_lastError = "Git 仓库初始化失败";
        }
        qWarning() << "Git init failed:" << stderrOutput;
        return false;
    }
    
    return true;
}

bool ProjectController::cloneRepository(const QString &source, const QString &targetPath) {
    return cloneRepositoryDetailed(source, targetPath).success;
}

ProjectController::CloneResult ProjectController::cloneRepositoryDetailed(
    const QString &source,
    const QString &targetPath) {
    m_lastError.clear();
    CloneResult cloneResult = prepareClone(source, targetPath);
    if (!cloneResult.summary.isEmpty()) {
        m_lastError = cloneResult.summary;
        return cloneResult;
    }

    const bool targetExistedBefore = QFileInfo::exists(cloneResult.targetPath);
    m_executor->setWorkingDirectory(QFileInfo(cloneResult.targetPath).absolutePath());
    const auto processResult = m_executor->execute(
        "clone",
        {"--", cloneResult.source, cloneResult.targetPath},
        60000);
    finishCloneResult(
        cloneResult,
        processResult.output,
        processResult.errorOutput,
        processResult.exitCode,
        processResult.exitStatus,
        processResult.error.message);
    if (!cloneResult.success) {
        removeEmptyCloneDirectory(cloneResult.targetPath, targetExistedBefore);
        m_lastError = cloneResult.summary;
    }
    return cloneResult;
}

void ProjectController::detectProjectAsync(const QString &path, ProjectInfoCompletion completion) {
    ProjectInfo info;
    if (!QFile::exists(path + "/.git")) {
        info.status = "Not a Git repository";
        completion(info);
        return;
    }

    m_executor->setWorkingDirectory(path);
    m_executor->executeAsync(
        "rev-parse",
        {"--is-inside-work-tree"},
        [this, completion](GitCommandExecutor::Result workTreeResult) {
            ProjectInfo info;
            if (workTreeResult.error.hasError()) {
                info.status = "Not a Git repository";
                info.error = QString::fromUtf8(workTreeResult.errorOutput).trimmed();
                completion(info);
                return;
            }
            info.isGitRepo = true;
            m_executor->executeAsync(
                "branch",
                {"--show-current"},
                [this, completion, info](GitCommandExecutor::Result branchResult) mutable {
                    if (!branchResult.error.hasError()) {
                        info.branch = QString::fromUtf8(branchResult.output).trimmed();
                        if (info.branch.isEmpty()) {
                            info.branch = "(detached)";
                        }
                    } else {
                        info.branch = "(unknown)";
                    }
                    m_executor->executeAsync(
                        "status",
                        {"--porcelain"},
                        [completion, info](GitCommandExecutor::Result statusResult) mutable {
                            if (statusResult.error.hasError()) {
                                info.status = "Unknown";
                                info.error = QString::fromUtf8(statusResult.errorOutput).trimmed();
                            } else {
                                info.status = statusResult.output.isEmpty() ? "Clean" : "Modified";
                            }
                            completion(info);
                        },
                        10000);
                },
                10000);
        },
        10000);
}

void ProjectController::initializeGitAsync(const QString &path, OperationCompletion completion) {
    m_lastError.clear();
    m_executor->setWorkingDirectory(path);
    m_executor->executeAsync(
        "init",
        {},
        [this, completion](GitCommandExecutor::Result result) {
            const bool success = !result.error.hasError();
            if (!success) {
                m_lastError = QString::fromUtf8(result.errorOutput).trimmed();
                if (m_lastError.isEmpty()) {
                    m_lastError = result.error.message;
                }
            }
            completion(success, m_lastError);
        },
        10000);
}

void ProjectController::cloneRepositoryAsync(
    const QString &source,
    const QString &targetPath,
    OperationCompletion completion) {
    cloneRepositoryDetailedAsync(
        source,
        targetPath,
        [completion](CloneResult result) {
            completion(result.success, result.success ? QString() : result.summary);
        });
}

void ProjectController::cloneRepositoryDetailedAsync(
    const QString &source,
    const QString &targetPath,
    CloneCompletion completion) {
    m_lastError.clear();
    CloneResult cloneResult = prepareClone(source, targetPath);
    if (!cloneResult.summary.isEmpty()) {
        m_lastError = cloneResult.summary;
        completion(cloneResult);
        return;
    }

    const bool targetExistedBefore = QFileInfo::exists(cloneResult.targetPath);
    m_executor->setWorkingDirectory(QFileInfo(cloneResult.targetPath).absolutePath());
    m_executor->executeAsync(
        "clone",
        {"--", cloneResult.source, cloneResult.targetPath},
        [this, cloneResult, targetExistedBefore, completion](GitCommandExecutor::Result processResult) mutable {
            finishCloneResult(
                cloneResult,
                processResult.output,
                processResult.errorOutput,
                processResult.exitCode,
                processResult.exitStatus,
                processResult.error.message);
            if (!cloneResult.success) {
                removeEmptyCloneDirectory(cloneResult.targetPath, targetExistedBefore);
                m_lastError = cloneResult.summary;
            }
            completion(cloneResult);
        },
        60000);
}

ProjectController::CloneResult ProjectController::prepareClone(
    const QString &source,
    const QString &targetPath) const {
    CloneResult result;
    result.gitProgram = m_executor->gitPath();
    result.source = source.trimmed();
    result.targetPath = targetPath.trimmed().isEmpty()
        ? QString()
        : QDir::cleanPath(targetPath.trimmed());
    result.arguments = {"clone", "--", result.source, result.targetPath};

    if (result.source.isEmpty()) {
        result.category = CloneFailureCategory::InvalidSource;
        result.summary = cloneFailureSummary(result.category);
        result.stderrText = "仓库地址或本地源路径不能为空";
        return result;
    }
    if (targetPath.trimmed().isEmpty()) {
        result.category = CloneFailureCategory::InvalidSource;
        result.summary = "目标路径无效，请重新选择目标目录。";
        result.stderrText = "目标路径不能为空";
        return result;
    }

    const QFileInfo targetInfo(result.targetPath);
    if (targetInfo.exists() && (!targetInfo.isDir() || !isDirectoryEmpty(result.targetPath))) {
        result.category = CloneFailureCategory::TargetNotEmpty;
        result.summary = cloneFailureSummary(result.category);
        result.stderrText = "目标目录已存在且非空：" + QDir::toNativeSeparators(result.targetPath);
        return result;
    }

    const QString targetParent = targetInfo.absolutePath();
    if (!QDir(targetParent).exists()) {
        result.category = CloneFailureCategory::InvalidSource;
        result.summary = "目标路径无效，请重新选择目标目录。";
        result.stderrText = "目标父目录不存在：" + QDir::toNativeSeparators(targetParent);
    }
    return result;
}

void ProjectController::finishCloneResult(
    CloneResult &cloneResult,
    const QByteArray &stdoutBytes,
    const QByteArray &stderrBytes,
    int exitCode,
    QProcess::ExitStatus exitStatus,
    const QString &processError) const {
    cloneResult.stdoutText = QString::fromUtf8(stdoutBytes);
    cloneResult.stderrText = QString::fromUtf8(stderrBytes);
    cloneResult.exitCode = exitCode;
    cloneResult.exitStatus = exitStatus;

    if (exitStatus == QProcess::NormalExit && exitCode == 0 &&
        QFileInfo::exists(cloneResult.targetPath + "/.git")) {
        cloneResult.success = true;
        cloneResult.category = CloneFailureCategory::None;
        cloneResult.summary.clear();
        return;
    }

    if (cloneResult.stderrText.isEmpty()) {
        cloneResult.stderrText = processError.trimmed();
    }
    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        cloneResult.stderrText = "克隆完成，但目标目录不是有效 Git 仓库";
    }
    const QFileInfo sourceInfo(cloneResult.source);
    cloneResult.category = sourceInfo.isAbsolute() && !sourceInfo.exists()
        ? CloneFailureCategory::InvalidSource
        : classifyCloneFailure(cloneResult.stderrText);
    cloneResult.summary = cloneFailureSummary(cloneResult.category);
}

ProjectController::CloneFailureCategory ProjectController::classifyCloneFailure(const QString &stderrText) {
    const QString error = stderrText.toLower();
    if (error.contains("already exists and is not an empty directory") ||
        error.contains("目标目录已存在且非空")) {
        return CloneFailureCategory::TargetNotEmpty;
    }
    if (error.contains("ssl certificate problem") || error.contains("tls") ||
        error.contains("schannel") || error.contains("certificate verify failed")) {
        return CloneFailureCategory::Tls;
    }
    if (error.contains("authentication failed") || error.contains("repository not found") ||
        error.contains("permission denied") || error.contains("could not read from remote repository") ||
        error.contains("authentication is required")) {
        return CloneFailureCategory::RepositoryOrAuthentication;
    }
    if (error.contains("could not resolve host") || error.contains("failed to connect") ||
        error.contains("connection timed out") || error.contains("network is unreachable") ||
        error.contains("connection was reset")) {
        return CloneFailureCategory::Network;
    }
    if (error.contains("does not exist") || error.contains("not a git repository") ||
        error.contains("invalid url") || error.contains("unsupported protocol")) {
        return CloneFailureCategory::InvalidSource;
    }
    return CloneFailureCategory::Unknown;
}

QString ProjectController::cloneFailureSummary(CloneFailureCategory category) {
    switch (category) {
    case CloneFailureCategory::Network:
        return "无法连接到远程仓库，请检查网络、代理或 GitHub 访问情况。";
    case CloneFailureCategory::TargetNotEmpty:
        return "目标目录已经存在且不是空目录，请更换目录名称或处理现有文件。";
    case CloneFailureCategory::RepositoryOrAuthentication:
        return "仓库不存在、地址不正确，或当前凭据没有访问权限。";
    case CloneFailureCategory::Tls:
        return "Git 无法验证远程服务器证书，请检查系统时间、Git 证书配置或网络代理。";
    case CloneFailureCategory::InvalidSource:
        return "仓库地址或本地源路径无效。";
    case CloneFailureCategory::None:
        return {};
    case CloneFailureCategory::Unknown:
        return "克隆失败，以下是 Git 返回的完整错误：";
    }
    return "克隆失败，以下是 Git 返回的完整错误：";
}

bool ProjectController::isDirectoryEmpty(const QString &path) {
    const QDir directory(path);
    return directory.exists() &&
        directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty();
}

void ProjectController::removeEmptyCloneDirectory(const QString &path, bool existedBefore) {
    if (!existedBefore && isDirectoryEmpty(path)) {
        QDir().rmdir(path);
    }
}

QStringList ProjectController::recentProjects() const {
    return m_settings->value("recentProjects").toStringList();
}

void ProjectController::addRecentProject(const QString &path) {
    QStringList projects = m_settings->value("recentProjects").toStringList();
    
    // Remove if already exists
    projects.removeAll(path);
    
    // Add to front
    projects.prepend(path);
    
    // Keep only last 10
    while (projects.size() > 10) {
        projects.removeLast();
    }
    
    m_settings->setValue("recentProjects", projects);
}

QString ProjectController::lastError() const {
    return m_lastError;
}

bool ProjectController::runGit(
    const QString &workingDir,
    const QStringList &args,
    int *exitCode,
    QString *output,
    QString *stderrOutput,
    int timeoutMs) {
    if (args.isEmpty()) {
        qWarning() << "Git failed to start:" << args;
        return false;
    }

    m_executor->setWorkingDirectory(workingDir);
    const auto result = m_executor->execute(args.first(), args.mid(1), timeoutMs);
    if (exitCode) *exitCode = result.exitCode;
    if (output) *output = QString::fromUtf8(result.output);
    if (stderrOutput) *stderrOutput = QString::fromUtf8(result.errorOutput);
    return !result.error.hasError();
}
