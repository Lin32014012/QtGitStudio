// v4-P2-1 Git Environment Detection: detects whether git is installed
// (git --version), parses the version, and supports a custom git path.
// Uses its own GitCommandExecutor; never bundles git, never touches the
// installer or release_package.
#ifndef GITENVIRONMENTSERVICE_H
#define GITENVIRONMENTSERVICE_H

#include <QObject>
#include <QString>
#include <functional>

class GitCommandExecutor;

class GitEnvironmentService : public QObject {
    Q_OBJECT
public:
    struct GitEnvironment {
        bool available = false;
        QString gitPath;    // resolved executable path
        QString version;    // parsed, e.g. "2.39.1"
        QString rawVersion; // full git --version output
        QString error;
    };
    using Completion = std::function<void(const GitEnvironment &)>;

    explicit GitEnvironmentService(QObject *parent = nullptr);

    // Synchronous detection. `customPath` empty = auto-detect via PATH.
    // v4-P6-20 P1-3: virtual so tests can inject a counting fake.
    virtual GitEnvironment detectSync(const QString &customPath = {});
    // Asynchronous detection.
    void detectAsync(const QString &customPath, Completion completion);
    QString gitPath() const;
    void setGitPath(const QString &path);

    // "git version 2.39.1.windows.1" -> "2.39.1"; falls back to the raw
    // token when unparseable. Pure function (unit-testable).
    static QString parseVersion(const QString &rawVersion);

private:
    GitCommandExecutor *m_executor;
};

#endif // GITENVIRONMENTSERVICE_H
