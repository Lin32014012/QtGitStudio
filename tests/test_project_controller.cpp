#include <QtTest>
#include "test_settings_isolation.h"
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QPlainTextEdit>
#include <QClipboard>
#include "ProjectController.h"
#include "CloneDialog.h"

class TestProjectController : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { isolateTestSettings(); }
    void testDetectGitProject();
    void testDetectNonGitProject();
    void testInitializeGit();
    void testInitializeGitAsync();
    void testCloneLocalRepository();
    void testCloneLocalRepositoryAsync();
    void testCloneFailureDoesNotPersist();
    void testCloneFailureCategories();
    void testCloneResultDiagnostics();
    void testClonePreservesPreexistingEmptyTarget();
    void testCloneDialogValidation();
    void testCloneDialogAsyncFeedback();
    void testCloneDialogFailureDiagnostics();
    void testRecentProjects();
    void testRecentProjectsPersistAcrossInstances();
};

void TestProjectController::testDetectGitProject() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    
    // Init git repo
    QProcess proc;
    proc.setWorkingDirectory(dir.path());
    proc.start("git", {"init"});
    proc.waitForFinished(5000);
    
    ProjectController controller;
    auto info = controller.detectProject(dir.path());
    QVERIFY(info.isGitRepo);
}

void TestProjectController::testDetectNonGitProject() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    
    ProjectController controller;
    auto info = controller.detectProject(dir.path());
    QVERIFY(!info.isGitRepo);
}

void TestProjectController::testInitializeGit() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QFile readme(dir.filePath("README.md"));
    QVERIFY(readme.open(QIODevice::WriteOnly));
    QCOMPARE(readme.write("unchanged"), qint64(9));
    readme.close();

    ProjectController controller;
    QVERIFY(controller.initializeGit(dir.path()));
    QVERIFY(QFile::exists(dir.path() + "/.git"));
    QVERIFY(readme.open(QIODevice::ReadOnly));
    QCOMPARE(readme.readAll(), QByteArray("unchanged"));
    readme.close();

    QProcess git;
    git.setWorkingDirectory(dir.path());
    git.start("git", {"rev-parse", "--verify", "HEAD"});
    QVERIFY(git.waitForFinished(5000));
    QVERIFY(git.exitCode() != 0);
}

void TestProjectController::testInitializeGitAsync() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile readme(dir.filePath("README.md"));
    QVERIFY(readme.open(QIODevice::WriteOnly));
    QCOMPARE(readme.write("unchanged async"), qint64(15));
    readme.close();

    ProjectController controller;
    bool completed = false;
    bool success = false;
    controller.initializeGitAsync(dir.path(), [&](bool result, const QString &) {
        completed = true;
        success = result;
    });
    QVERIFY(!completed);
    QTRY_VERIFY(completed);
    QVERIFY(success);
    QVERIFY(QFile::exists(dir.path() + "/.git"));
    QVERIFY(readme.open(QIODevice::ReadOnly));
    QCOMPARE(readme.readAll(), QByteArray("unchanged async"));
}

void TestProjectController::testCloneLocalRepository() {
    QTemporaryDir source;
    QTemporaryDir targetParent;
    QVERIFY(source.isValid());
    QVERIFY(targetParent.isValid());

    QProcess git;
    git.setWorkingDirectory(source.path());
    git.start("git", {"init"});
    QVERIFY(git.waitForFinished(5000));
    QCOMPARE(git.exitCode(), 0);
    git.start("git", {"config", "user.name", "QtGitStudio Test"});
    QVERIFY(git.waitForFinished(5000));
    QCOMPARE(git.exitCode(), 0);
    git.start("git", {"config", "user.email", "qtgitstudio@example.invalid"});
    QVERIFY(git.waitForFinished(5000));
    QCOMPARE(git.exitCode(), 0);

    QFile readme(source.filePath("README.md"));
    QVERIFY(readme.open(QIODevice::WriteOnly));
    QCOMPARE(readme.write("# clone test"), qint64(12));
    readme.close();
    git.start("git", {"add", "README.md"});
    QVERIFY(git.waitForFinished(5000));
    QCOMPARE(git.exitCode(), 0);
    git.start("git", {"commit", "-m", "initial"});
    QVERIFY(git.waitForFinished(5000));
    QCOMPARE(git.exitCode(), 0);

    ProjectController controller;
    const QString targetPath = targetParent.filePath("clone");
    QVERIFY(controller.cloneRepository(source.path(), targetPath));
    QVERIFY(QFile::exists(targetPath + "/.git"));
    QVERIFY(QFile::exists(targetPath + "/README.md"));

    git.setWorkingDirectory(source.path());
    git.start("git", {"rev-parse", "HEAD"});
    QVERIFY(git.waitForFinished(5000));
    QCOMPARE(git.exitCode(), 0);
    const QByteArray sourceHead = git.readAllStandardOutput().trimmed();
    git.setWorkingDirectory(targetPath);
    git.start("git", {"rev-parse", "HEAD"});
    QVERIFY(git.waitForFinished(5000));
    QCOMPARE(git.exitCode(), 0);
    QCOMPARE(git.readAllStandardOutput().trimmed(), sourceHead);
}

void TestProjectController::testCloneLocalRepositoryAsync() {
    QTemporaryDir source;
    QTemporaryDir targetParent;
    QVERIFY(source.isValid());
    QVERIFY(targetParent.isValid());

    QProcess git;
    git.setWorkingDirectory(source.path());
    for (const QStringList &args : QList<QStringList>{
             {"init"},
             {"config", "user.name", "QtGitStudio Test"},
             {"config", "user.email", "qtgitstudio@example.invalid"},
             {"commit", "--allow-empty", "-m", "initial"}}) {
        git.start("git", args);
        QVERIFY(git.waitForFinished(5000));
        QCOMPARE(git.exitCode(), 0);
    }

    ProjectController controller;
    const QString targetPath = targetParent.filePath("async-clone");
    bool completed = false;
    bool success = false;
    controller.cloneRepositoryAsync(source.path(), targetPath, [&](bool result, const QString &) {
        completed = true;
        success = result;
    });
    QVERIFY(!completed);
    QTRY_VERIFY(completed);
    QVERIFY(success);
    QVERIFY(QFile::exists(targetPath + "/.git"));
}

void TestProjectController::testCloneFailureDoesNotPersist() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
    ProjectController controller(&settings);
    const QString targetPath = dir.filePath("failed-clone");

    QVERIFY(!controller.cloneRepository(dir.filePath("missing-source"), targetPath));
    QVERIFY(!controller.lastError().isEmpty());
    QVERIFY(controller.recentProjects().isEmpty());
    QVERIFY(!QFileInfo::exists(targetPath));
}

void TestProjectController::testCloneFailureCategories() {
    using Category = ProjectController::CloneFailureCategory;
    QCOMPARE(
        ProjectController::classifyCloneFailure("fatal: unable to access: Could not resolve host"),
        Category::Network);
    QCOMPARE(
        ProjectController::classifyCloneFailure("fatal: destination path already exists and is not an empty directory"),
        Category::TargetNotEmpty);
    QCOMPARE(
        ProjectController::classifyCloneFailure("remote: Repository not found. Authentication failed"),
        Category::RepositoryOrAuthentication);
    QCOMPARE(
        ProjectController::classifyCloneFailure("fatal: SSL certificate problem: unable to get local issuer certificate"),
        Category::Tls);
    QCOMPARE(
        ProjectController::classifyCloneFailure("fatal: repository 'missing' does not exist"),
        Category::InvalidSource);
    QCOMPARE(
        ProjectController::classifyCloneFailure("fatal: unexpected clone failure"),
        Category::Unknown);
}

void TestProjectController::testCloneResultDiagnostics() {
    QTemporaryDir targetParent;
    QVERIFY(targetParent.isValid());
    ProjectController controller;
    const QString source = targetParent.filePath("missing-source");
    const QString target = targetParent.filePath("failed-detailed-clone");

    const auto result = controller.cloneRepositoryDetailed(source, target);
    QVERIFY(!result.success);
    QCOMPARE(result.category, ProjectController::CloneFailureCategory::InvalidSource);
    QCOMPARE(result.source, source);
    QCOMPARE(result.targetPath, QDir::cleanPath(target));
    QVERIFY(QFileInfo(result.gitProgram).isAbsolute());
    QCOMPARE(result.arguments, QStringList({"clone", "--", source, QDir::cleanPath(target)}));
    QCOMPARE(result.exitStatus, QProcess::NormalExit);
    QVERIFY(result.exitCode != 0);
    QVERIFY(!result.stderrText.isEmpty());
    QVERIFY(result.details().contains(result.gitProgram));
    QVERIFY(result.details().contains(source));
    QVERIFY(result.details().contains(QDir::toNativeSeparators(target)));
    QVERIFY(result.details().contains("退出代码"));
    QVERIFY(result.details().contains("does not exist"));
}

void TestProjectController::testClonePreservesPreexistingEmptyTarget() {
    QTemporaryDir targetParent;
    QVERIFY(targetParent.isValid());
    const QString target = targetParent.filePath("preexisting-empty");
    QVERIFY(QDir().mkdir(target));

    ProjectController controller;
    const auto result = controller.cloneRepositoryDetailed(
        targetParent.filePath("missing-source"),
        target);
    QVERIFY(!result.success);
    QVERIFY(QFileInfo(target).isDir());
    QVERIFY(QDir(target).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
}

void TestProjectController::testCloneDialogValidation() {
    QTemporaryDir targetParent;
    QVERIFY(targetParent.isValid());
    ProjectController controller;
    CloneDialog dialog(&controller);

    QLineEdit *sourceEdit = dialog.findChild<QLineEdit *>("cloneSourceEdit");
    QLineEdit *parentEdit = dialog.findChild<QLineEdit *>("cloneTargetParentEdit");
    QLineEdit *nameEdit = dialog.findChild<QLineEdit *>("cloneNameEdit");
    QPushButton *startButton = dialog.findChild<QPushButton *>("cloneStartButton");
    QLabel *preview = dialog.findChild<QLabel *>("cloneTargetPreview");
    QLabel *error = dialog.findChild<QLabel *>("cloneErrorLabel");
    QVERIFY(sourceEdit != nullptr);
    QVERIFY(parentEdit != nullptr);
    QVERIFY(nameEdit != nullptr);
    QVERIFY(startButton != nullptr);
    QVERIFY(preview != nullptr);
    QVERIFY(error != nullptr);
    QVERIFY(!startButton->isEnabled());

    sourceEdit->setText("https://example.invalid/team/sample.git");
    parentEdit->setText(targetParent.path());
    QCOMPARE(nameEdit->text(), QString("sample"));
    QVERIFY(preview->text().contains("sample"));
    QVERIFY(startButton->isEnabled());

    QVERIFY(QDir().mkdir(targetParent.filePath("sample")));
    QVERIFY(QMetaObject::invokeMethod(&dialog, "updateState"));
    QVERIFY(!startButton->isEnabled());
    QVERIFY(error->text().contains("目标目录已存在"));
}

void TestProjectController::testCloneDialogAsyncFeedback() {
    QTemporaryDir source;
    QTemporaryDir targetParent;
    QVERIFY(source.isValid());
    QVERIFY(targetParent.isValid());

    QProcess git;
    git.setWorkingDirectory(source.path());
    for (const QStringList &args : QList<QStringList>{
             {"init"},
             {"config", "user.name", "QtGitStudio Test"},
             {"config", "user.email", "qtgitstudio@example.invalid"},
             {"commit", "--allow-empty", "-m", "initial"}}) {
        git.start("git", args);
        QVERIFY(git.waitForFinished(5000));
        QCOMPARE(git.exitCode(), 0);
    }

    ProjectController controller;
    CloneDialog dialog(&controller);
    auto *sourceEdit = dialog.findChild<QLineEdit *>("cloneSourceEdit");
    auto *parentEdit = dialog.findChild<QLineEdit *>("cloneTargetParentEdit");
    auto *nameEdit = dialog.findChild<QLineEdit *>("cloneNameEdit");
    auto *startButton = dialog.findChild<QPushButton *>("cloneStartButton");
    QVERIFY(sourceEdit != nullptr);
    QVERIFY(parentEdit != nullptr);
    QVERIFY(nameEdit != nullptr);
    QVERIFY(startButton != nullptr);

    sourceEdit->setText(source.path());
    parentEdit->setText(targetParent.path());
    nameEdit->setText("dialog-clone");
    QVERIFY(startButton->isEnabled());
    startButton->click();
    QCOMPARE(startButton->text(), QString("正在克隆…"));
    QVERIFY(startButton->property("busy").toBool());
    QVERIFY(!startButton->isEnabled());

    QTRY_COMPARE(startButton->text(), QString("克隆成功"));
    QCOMPARE(startButton->property("result").toString(), QString("success"));
    QTRY_COMPARE(dialog.result(), int(QDialog::Accepted));
    QVERIFY(QFile::exists(targetParent.filePath("dialog-clone/.git")));
}

void TestProjectController::testCloneDialogFailureDiagnostics() {
    QTemporaryDir settingsDir;
    QTemporaryDir targetParent;
    QVERIFY(settingsDir.isValid());
    QVERIFY(targetParent.isValid());
    QSettings settings(settingsDir.filePath("settings.ini"), QSettings::IniFormat);
    ProjectController controller(&settings);
    CloneDialog dialog(&controller);
    dialog.show();
    QApplication::processEvents();

    auto *sourceEdit = dialog.findChild<QLineEdit *>("cloneSourceEdit");
    auto *parentEdit = dialog.findChild<QLineEdit *>("cloneTargetParentEdit");
    auto *nameEdit = dialog.findChild<QLineEdit *>("cloneNameEdit");
    auto *startButton = dialog.findChild<QPushButton *>("cloneStartButton");
    auto *status = dialog.findChild<QLabel *>("cloneStatusLabel");
    auto *error = dialog.findChild<QLabel *>("cloneErrorLabel");
    QVERIFY(sourceEdit != nullptr);
    QVERIFY(parentEdit != nullptr);
    QVERIFY(nameEdit != nullptr);
    QVERIFY(startButton != nullptr);
    QVERIFY(status != nullptr);
    QVERIFY(error != nullptr);

    const QString missingSource = targetParent.filePath("missing-source");
    const QString targetPath = targetParent.filePath("failed-dialog-clone");
    sourceEdit->setText(missingSource);
    parentEdit->setText(targetParent.path());
    nameEdit->setText("failed-dialog-clone");
    QVERIFY(startButton->isEnabled());
    startButton->click();
    QCOMPARE(startButton->text(), QString("正在克隆…"));
    QVERIFY(!startButton->isEnabled());
    QTRY_VERIFY(error->isVisible());

    QVERIFY(
        error->text().contains("仓库地址或本地源路径无效") &&
        dialog.findChild<QWidget *>("cloneErrorDetails") != nullptr &&
        dialog.findChild<QWidget *>("cloneErrorDetails")->isVisible() &&
        dialog.findChild<QWidget *>("cloneErrorDetails")->property("plainText").toString().contains("does not exist") &&
        dialog.findChild<QPushButton *>("cloneCopyErrorButton") != nullptr &&
        dialog.findChild<QPushButton *>("cloneCopyErrorButton")->isVisible());

    QTRY_VERIFY(startButton->isEnabled());
    QCOMPARE(startButton->text(), QString("开始克隆"));
    QCOMPARE(sourceEdit->text(), missingSource);
    QCOMPARE(parentEdit->text(), targetParent.path());
    QCOMPARE(nameEdit->text(), QString("failed-dialog-clone"));
    QVERIFY(controller.recentProjects().isEmpty());
    QVERIFY(!QFileInfo::exists(targetPath));
    QCOMPARE(dialog.result(), int(QDialog::Rejected));

    auto *details = dialog.findChild<QPlainTextEdit *>("cloneErrorDetails");
    auto *copyButton = dialog.findChild<QPushButton *>("cloneCopyErrorButton");
    QVERIFY(details != nullptr);
    QVERIFY(copyButton != nullptr);
    copyButton->click();
    QCOMPARE(QApplication::clipboard()->text(), details->toPlainText());
}

void TestProjectController::testRecentProjects() {
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    QSettings settings(settingsDir.filePath("settings.ini"), QSettings::IniFormat);
    ProjectController controller(&settings);
    QStringList projects = controller.recentProjects();
    QVERIFY(projects.isEmpty());
    
    controller.addRecentProject("/test/path");
    projects = controller.recentProjects();
    QVERIFY(projects.contains("/test/path"));
}

void TestProjectController::testRecentProjectsPersistAcrossInstances() {
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    const QString settingsPath = settingsDir.filePath("settings.ini");
    {
        QSettings settings(settingsPath, QSettings::IniFormat);
        ProjectController controller(&settings);
        controller.addRecentProject("C:/projects/persistent");
        settings.sync();
    }
    {
        QSettings settings(settingsPath, QSettings::IniFormat);
        ProjectController controller(&settings);
        QCOMPARE(controller.recentProjects(), QStringList{"C:/projects/persistent"});
    }
}

QTEST_MAIN(TestProjectController)
#include "test_project_controller.moc"
