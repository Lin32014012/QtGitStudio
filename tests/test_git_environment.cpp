// v4-P2-1 Git Environment Detection tests:
//   1. git present detection (auto-detect via PATH)
//   2. git absent (nonexistent custom path)
//   3. custom git path (explicit executable path is honoured)
//   4. version parsing (pure function: plain / suffixed / unparseable)
#include <QtTest>
#include <QProcess>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QTimer>
#include <QCoreApplication>
#include <QPushButton>
#include "GitEnvironmentService.h"
#include "GitEnvironmentDialog.h"
#include "GitCommandExecutor.h"
#include "GitController.h"

namespace {

GitCommandExecutor::Result executeAsyncAndWait(GitCommandExecutor &executor) {
    GitCommandExecutor::Result result;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    executor.executeAsync(QStringLiteral("--version"), {},
                          [&](GitCommandExecutor::Result value) {
                              result = value;
                              loop.quit();
                          });
    timeout.start(10000);
    loop.exec();
    return result;
}

class DefaultGitGuard {
public:
    DefaultGitGuard()
        : previous(GitCommandExecutor::defaultGitExecutable()) {}
    ~DefaultGitGuard() {
        GitCommandExecutor::setDefaultGitExecutable(previous);
    }
    QString previous;
};

class ScriptedEnvironmentService : public GitEnvironmentService {
public:
    using GitEnvironmentService::GitEnvironmentService;
    GitEnvironment response;
    int calls = 0;

    GitEnvironment detectSync(const QString &customPath = {}) override {
        Q_UNUSED(customPath);
        ++calls;
        return response;
    }
};

}  // namespace

class TestGitEnvironment : public QObject {
    Q_OBJECT
private slots:
    void gitPresentDetection() {
        GitEnvironmentService service;
        const GitEnvironmentService::GitEnvironment env = service.detectSync();
        QVERIFY(env.available);
        QVERIFY(!env.version.isEmpty());
        QVERIFY(!env.rawVersion.isEmpty());
    }

    void gitAbsent() {
        GitEnvironmentService service;
        const GitEnvironmentService::GitEnvironment env =
            service.detectSync(QStringLiteral("C:/definitely/not/git.exe"));
        QVERIFY(!env.available);
        QVERIFY(!env.error.isEmpty());
    }

    void customPathHonoured() {
        // Resolve the real git path used by the test environment.
        const QString gitPath = QStandardPaths::findExecutable(
            QStringLiteral("git"));
        if (gitPath.isEmpty()) {
            QSKIP("git not found in PATH - cannot run custom-path scenario");
        }
        GitEnvironmentService service;
        const GitEnvironmentService::GitEnvironment env =
            service.detectSync(gitPath);
        QVERIFY(env.available);
        QVERIFY(env.gitPath.endsWith(QStringLiteral("git.exe"))
                || env.gitPath.endsWith(QStringLiteral("git")));
        QVERIFY(!env.version.isEmpty());
    }

    void invalidCandidateLeavesConfiguredExecutableUsable() {
        const QString realGit = QStandardPaths::findExecutable(
            QStringLiteral("git"));
        QVERIFY(!realGit.isEmpty());
        GitCommandExecutor::setDefaultGitExecutable(realGit);

        GitEnvironmentService service;
        const auto invalid = service.detectSync(
            QStringLiteral("C:/definitely/not/git.exe"));
        QVERIFY(!invalid.available);
        QCOMPARE(GitCommandExecutor::defaultGitExecutable(), realGit);

        // This is a real process invocation, not just a resolved-path check.
        GitCommandExecutor executor;
        QCOMPARE(executor.resolveGitExecutable(), realGit);
        QVERIFY(!executor.execute(QStringLiteral("--version"), {}).error.hasError());
        GitCommandExecutor::setDefaultGitExecutable(QString());
    }

    void validCandidateExecutesAndCanBecomeGlobal() {
        const QString realGit = QStandardPaths::findExecutable(
            QStringLiteral("git"));
        QVERIFY(!realGit.isEmpty());
        GitCommandExecutor::setDefaultGitExecutable(QStringLiteral("C:/definitely/not/git.exe"));

        GitEnvironmentService service;
        const auto valid = service.detectSync(realGit);
        QVERIFY(valid.available);
        QCOMPARE(valid.gitPath, QFileInfo(realGit).absoluteFilePath());

        GitCommandExecutor::setDefaultGitExecutable(valid.gitPath);
        GitCommandExecutor executor;
        QVERIFY(!executor.execute(QStringLiteral("--version"), {}).error.hasError());
        GitCommandExecutor::setDefaultGitExecutable(QString());
    }

    void versionParsing() {
        // Plain.
        QCOMPARE(GitEnvironmentService::parseVersion(
                     QStringLiteral("git version 2.39.1")),
                 QStringLiteral("2.39.1"));
        // With Windows suffix.
        QCOMPARE(GitEnvironmentService::parseVersion(
                     QStringLiteral("git version 2.39.1.windows.1")),
                 QStringLiteral("2.39.1"));
        // Two-part version.
        QCOMPARE(GitEnvironmentService::parseVersion(
                     QStringLiteral("git version 2.39")),
                 QStringLiteral("2.39"));
        // Leading whitespace tolerated.
        QCOMPARE(GitEnvironmentService::parseVersion(
                     QStringLiteral("  git version 2.40.0\n")),
                 QStringLiteral("2.40.0"));
        // Unparseable -> empty.
        QVERIFY(GitEnvironmentService::parseVersion(
                    QStringLiteral("git: command not found")).isEmpty());
    }

    void selectedExecutablePropagatesToAllExecutors() {
        const QString realGit = QStandardPaths::findExecutable(
            QStringLiteral("git"));
        QVERIFY(!realGit.isEmpty());
        GitCommandExecutor::setDefaultGitExecutable(realGit);
        GitCommandExecutor existing;
        GitController controller;
        controller.setRepositoryPath(QDir::tempPath());
        QCOMPARE(existing.gitPath(), realGit);
        QCOMPARE(controller.gitExecutablePath(), realGit);
        QVERIFY(!existing.execute("--version", {}).error.hasError());

        const QString missing = QStringLiteral("C:/definitely/not/git.exe");
        GitCommandExecutor::setDefaultGitExecutable(missing);
        GitCommandExecutor createdAfterSelection;
        QCOMPARE(existing.gitPath(), missing);
        QCOMPARE(createdAfterSelection.gitPath(), missing);
        QCOMPARE(controller.gitExecutablePath(), missing);
        // The actual QProcess must receive the selected executable; it must
        // not silently fall back to PATH's git.
        QVERIFY(existing.execute("--version", {}).error.hasError());

        GitCommandExecutor::setDefaultGitExecutable(realGit);
        QVERIFY(!controller.gitExecutablePath().isEmpty());
        QVERIFY(!controller.gitExecutablePath().contains(
            QStringLiteral("definitely/not")));
        GitCommandExecutor::setDefaultGitExecutable(QString());
    }

    void actualExecutableMarkersCoverConfirmCancelAndInvalid() {
        DefaultGitGuard restoreDefault;
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString helper = QDir(QCoreApplication::applicationDirPath())
                                   .filePath(QStringLiteral("test_git_marker_helper")
#ifdef Q_OS_WIN
                                             + QStringLiteral(".exe")
#endif
                                   );
        QVERIFY2(QFileInfo::exists(helper), qPrintable(helper));
        const QString gitA = QDir(temp.path()).filePath(QStringLiteral("git-A.exe"));
        const QString gitB = QDir(temp.path()).filePath(QStringLiteral("git-B.exe"));
        QVERIFY(QFile::copy(helper, gitA));
        QVERIFY(QFile::copy(helper, gitB));

        GitCommandExecutor::setDefaultGitExecutable(gitA);
        GitCommandExecutor existing;
        QVERIFY(QString::fromUtf8(existing.execute(QStringLiteral("--version"), {}).output)
                    .contains(QStringLiteral("marker=git-A.exe")));

        GitEnvironmentService detector;
        const auto validB = detector.detectSync(gitB);
        QVERIFY(validB.available);
        QVERIFY(validB.rawVersion.contains(QStringLiteral("marker=git-B.exe")));
        QCOMPARE(GitCommandExecutor::defaultGitExecutable(), gitA);

        // Cancel after successfully detecting B: neither an existing nor a
        // newly-created executor may leave the still-active global A.
        QVERIFY(QString::fromUtf8(executeAsyncAndWait(existing).output)
                    .contains(QStringLiteral("marker=git-A.exe")));
        GitCommandExecutor createdAfterCancel;
        QVERIFY(QString::fromUtf8(executeAsyncAndWait(createdAfterCancel).output)
                    .contains(QStringLiteral("marker=git-A.exe")));

        const auto invalid = detector.detectSync(
            QDir(temp.path()).filePath(QStringLiteral("missing-git.exe")));
        QVERIFY(!invalid.available);
        QCOMPARE(GitCommandExecutor::defaultGitExecutable(), gitA);
        QVERIFY(QString::fromUtf8(executeAsyncAndWait(existing).output)
                    .contains(QStringLiteral("marker=git-A.exe")));

        // Confirm the validated B path: the existing executor and a future
        // executor must both launch B on the real asynchronous QProcess path.
        GitCommandExecutor::setDefaultGitExecutable(validB.gitPath);
        QVERIFY(QString::fromUtf8(executeAsyncAndWait(existing).output)
                    .contains(QStringLiteral("marker=git-B.exe")));
        GitCommandExecutor createdAfterConfirm;
        QVERIFY(QString::fromUtf8(executeAsyncAndWait(createdAfterConfirm).output)
                    .contains(QStringLiteral("marker=git-B.exe")));
        qInfo().noquote()
            << QStringLiteral("CUSTOM_GIT A=%1 B=%2 cancel=A invalid=A confirm_existing=B confirm_new=B result=PASS")
                   .arg(gitA, gitB);
    }

    void dialogResultAndValidatedStateGateGlobalSelection() {
        DefaultGitGuard restoreDefault;
        const QString gitA = QStringLiteral("C:/configured/git-A.exe");
        const QString gitB = QStringLiteral("C:/candidate/git-B.exe");
        const QString invalidC = QStringLiteral("C:/candidate/missing-C.exe");
        GitCommandExecutor::setDefaultGitExecutable(gitA);

        // Valid B followed by Cancel must keep A.
        ScriptedEnvironmentService cancelService;
        cancelService.response.available = true;
        cancelService.response.gitPath = gitB;
        GitEnvironmentDialog cancelDialog(&cancelService);
        cancelDialog.setCustomPath(gitB);
        QVERIFY(QMetaObject::invokeMethod(&cancelDialog, "reDetect"));
        QTRY_COMPARE(cancelDialog.validatedPath(), gitB);
        cancelDialog.reject();
        cancelDialog.commitSelection(gitA);
        QCOMPARE(GitCommandExecutor::defaultGitExecutable(), gitA);

        // Valid B followed by Accept commits B through the same dialog gate
        // that MainWindow calls.
        ScriptedEnvironmentService confirmService;
        confirmService.response.available = true;
        confirmService.response.gitPath = gitB;
        GitEnvironmentDialog confirmDialog(&confirmService);
        confirmDialog.setCustomPath(gitB);
        QVERIFY(QMetaObject::invokeMethod(&confirmDialog, "reDetect"));
        QTRY_COMPARE(confirmDialog.validatedPath(), gitB);
        confirmDialog.accept();
        QCOMPARE(confirmDialog.result(), int(QDialog::Accepted));
        confirmDialog.commitSelection(gitA);
        QCOMPARE(GitCommandExecutor::defaultGitExecutable(), gitB);

        // Start invalid C after valid B. The old B validation is cleared
        // synchronously and an early Accept is rejected while detection runs.
        GitCommandExecutor::setDefaultGitExecutable(gitA);
        ScriptedEnvironmentService invalidService;
        invalidService.response.available = true;
        invalidService.response.gitPath = gitB;
        GitEnvironmentDialog invalidDialog(&invalidService);
        QTRY_COMPARE(invalidDialog.validatedPath(), gitB);
        invalidService.response = {};
        invalidService.response.gitPath = invalidC;
        invalidDialog.setCustomPath(invalidC);
        QVERIFY(QMetaObject::invokeMethod(&invalidDialog, "reDetect"));
        QVERIFY(invalidDialog.validatedPath().isEmpty());
        auto *closeButton =
            invalidDialog.findChild<QPushButton *>(QStringLiteral("gitEnvClose"));
        QVERIFY(closeButton != nullptr);
        QVERIFY(!closeButton->isEnabled());
        invalidDialog.accept();
        QVERIFY(invalidDialog.result() != int(QDialog::Accepted));
        QTRY_VERIFY(closeButton->isEnabled());
        QVERIFY(invalidDialog.validatedPath().isEmpty());
        invalidDialog.accept();
        QCOMPARE(invalidDialog.result(), int(QDialog::Accepted));
        invalidDialog.commitSelection(gitA);
        QCOMPARE(GitCommandExecutor::defaultGitExecutable(), gitA);
        qInfo().noquote()
            << QStringLiteral("CUSTOM_GIT_DIALOG cancel=A confirm=B invalid_early_accept=blocked invalid_final=A result=PASS");
    }
};

QTEST_MAIN(TestGitEnvironment)
#include "test_git_environment.moc"
