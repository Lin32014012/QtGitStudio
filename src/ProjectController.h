#pragma once
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <functional>

class GitCommandExecutor;
class QSettings;

class ProjectController : public QObject {
    Q_OBJECT
public:
    explicit ProjectController(QObject *parent = nullptr);
    explicit ProjectController(QSettings *settings, QObject *parent = nullptr);
    ~ProjectController();
    
    struct ProjectInfo {
        bool isGitRepo = false;
        QString branch;
        QString status;
        QString error;
    };

    using ProjectInfoCompletion = std::function<void(ProjectInfo)>;
    using OperationCompletion = std::function<void(bool, QString)>;

    enum class CloneFailureCategory {
        None,
        Network,
        TargetNotEmpty,
        RepositoryOrAuthentication,
        Tls,
        InvalidSource,
        Unknown
    };

    struct CloneResult {
        bool success = false;
        QProcess::ExitStatus exitStatus = QProcess::CrashExit;
        int exitCode = -1;
        QString stdoutText;
        QString stderrText;
        QString gitProgram;
        QStringList arguments;
        QString source;
        QString targetPath;
        CloneFailureCategory category = CloneFailureCategory::Unknown;
        QString summary;

        QString details() const;
    };

    using CloneCompletion = std::function<void(CloneResult)>;
    
    ProjectInfo detectProject(const QString &path);
    bool initializeGit(const QString &path);
    bool cloneRepository(const QString &source, const QString &targetPath);
    CloneResult cloneRepositoryDetailed(const QString &source, const QString &targetPath);
    void detectProjectAsync(const QString &path, ProjectInfoCompletion completion);
    void initializeGitAsync(const QString &path, OperationCompletion completion);
    void cloneRepositoryAsync(
        const QString &source,
        const QString &targetPath,
        OperationCompletion completion);
    void cloneRepositoryDetailedAsync(
        const QString &source,
        const QString &targetPath,
        CloneCompletion completion);
    static CloneFailureCategory classifyCloneFailure(const QString &stderrText);
    QStringList recentProjects() const;
    void addRecentProject(const QString &path);

    // v4-P6-7: one-time legacy (registry) -> IniFormat migration.
    // Pass a fake legacy store in tests; nullptr = production registry
    // (NativeFormat, UserScope, QtGitStudio/QtGitStudio).
    void migrateLegacyRecentProjects(QSettings *legacyStore = nullptr);
    QString lastError() const;
    
private:
    CloneResult prepareClone(const QString &source, const QString &targetPath) const;
    void finishCloneResult(CloneResult &cloneResult, const QByteArray &stdoutBytes, const QByteArray &stderrBytes,
                           int exitCode, QProcess::ExitStatus exitStatus, const QString &processError) const;
    static QString cloneFailureSummary(CloneFailureCategory category);
    static bool isDirectoryEmpty(const QString &path);
    static void removeEmptyCloneDirectory(const QString &path, bool existedBefore);

    bool runGit(
        const QString &workingDir,
        const QStringList &args,
        int *exitCode = nullptr,
        QString *output = nullptr,
        QString *stderrOutput = nullptr,
        int timeoutMs = 10000);

    GitCommandExecutor *m_executor;
    QSettings *m_settings;
    QString m_lastError;
};
