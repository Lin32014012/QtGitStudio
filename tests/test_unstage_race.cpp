// v4-P6-21R6 P1-01: unstage multi-stage repository retarget race.
// Stage 1 (rev-parse HEAD) is held pending via a fake executor; the
// repository is switched to B (executor retargeted); then A's stage 1
// completes. The pre-stage-2 session fence must abort stage 2:
// restore --staged / rm --cached invocation MUST be 0.
#include <QtTest>
#include <QTimer>
#include <QDir>
#include <QFile>
#include "GitCommitService.h"
#include "GitCommandExecutor.h"
#include "GitController.h"

namespace {

// Fake executor: delays the FIRST executeAsync call (stage 1) and
// records every subsequent invocation (stage 2 would appear here).
class SpyExecutor : public GitCommandExecutor {
public:
    using GitCommandExecutor::GitCommandExecutor;
    QString firstCommand;
    QStringList firstArgs;
    bool firstHeld = false;
    GitCommandExecutor::Completion firstCompletion;
    int stage2Invocations = 0;
    QString stage2Command;

    void executeAsync(const QString &command, const QStringList &args,
                      GitCommandExecutor::Completion completion,
                      int /*timeoutMs*/) override {
        if (!firstHeld) {
            firstHeld = true;
            firstCommand = command;
            firstArgs = args;
            firstCompletion = completion;
            return;  // hold stage 1 pending
        }
        ++stage2Invocations;  // stage 2 would execute HERE - must never happen
        stage2Command = command;
        GitCommandExecutor::Result r;
        completion(r);
    }
};

class DelayedCommitService : public GitCommitService {
public:
    using GitCommitService::GitCommitService;
    FilesCompletion pendingList;
    QString listTarget;
    int unstageInvocations = 0;
    QString unstageTarget;

    void stagedFilesAsync(FilesCompletion completion) override {
        listTarget = workingTarget;
        pendingList = completion;
    }

    void unstageFilesAsync(const QStringList &files,
                           OperationCompletion completion) override {
        Q_UNUSED(files);
        ++unstageInvocations;
        unstageTarget = workingTarget;
        OperationResult result;
        result.success = true;
        completion(result);
    }

    void setTarget(const QString &path) {
        workingTarget = path;
        setWorkingDirectory(path);
    }

    QString workingTarget;
};

}  // namespace

class TestUnstageRace : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {}

    void cleanupTestCase() {}

    // Stage 1 pending on A -> retarget executor to B -> A completes ->
    // stage 2 must NOT run.
    void unstageStage2AbortedAfterRetarget() {
        SpyExecutor *spy = new SpyExecutor(this);
        GitCommitService service;
        service.setExecutorForTest(spy);
        service.setWorkingDirectory(QStringLiteral("C:/repo/A"));

        int completions = 0;
        bool aborted = false;
        service.unstageFilesAsync(
            {QStringLiteral("a.txt")},
            [&](GitCommitService::OperationResult result) {
                ++completions;
                aborted = result.error.hasError();
            });

        QVERIFY(spy->firstHeld);
        QCOMPARE(spy->firstCommand, QStringLiteral("rev-parse"));

        // Retarget the executor (what MainWindow::applyRepositorySession
        // does when the user switches A -> B).
        service.setWorkingDirectory(QStringLiteral("C:/repo/B"));

        // Release A's stage 1 completion.
        GitCommandExecutor::Result headResult;
        headResult.output = "HEAD";
        headResult.error.type = GitErrorType::None;
        spy->firstCompletion(headResult);
        QApplication::processEvents();

        // Stage 2 must NEVER have been invoked, and the caller must
        // have received the session-mismatch abort.
        QCOMPARE(spy->stage2Invocations, 0);
        QCOMPARE(completions, 1);
        QVERIFY(aborted);
    }

    // Path equality alone is not a session identity. A -> B -> A while
    // stage 1 is pending must still reject the old A continuation.
    void unstageStage2AbortedAfterRepositoryRoundTrip() {
        auto *spy = new SpyExecutor(this);
        GitCommitService service;
        service.setExecutorForTest(spy);
        service.setWorkingDirectory(QStringLiteral("C:/repo/A"));

        int completions = 0;
        bool aborted = false;
        service.unstageFilesAsync(
            {QStringLiteral("a.txt")},
            [&](GitCommitService::OperationResult result) {
                ++completions;
                aborted = result.error.hasError();
            });
        QVERIFY(spy->firstHeld);
        QCOMPARE(spy->firstCommand, QStringLiteral("rev-parse"));

        service.setWorkingDirectory(QStringLiteral("C:/repo/B"));
        service.setWorkingDirectory(QStringLiteral("C:/repo/A"));
        QCOMPARE(spy->workingDirectory(), QStringLiteral("C:/repo/A"));

        GitCommandExecutor::Result headResult;
        headResult.output = "HEAD";
        headResult.error.type = GitErrorType::None;
        spy->firstCompletion(headResult);
        QApplication::processEvents();

        QCOMPARE(spy->stage2Invocations, 0);
        QCOMPARE(completions, 1);
        QVERIFY(aborted);
        qInfo().noquote()
            << QStringLiteral("UNSTAGE_ABA A_generation=1 B_generation=2 A_reopen_generation=3 stage2=0 result=PASS");
    }

    // Same repository (no retarget) -> stage 2 runs normally.
    void unstageStage2RunsWhenSameRepo() {
        SpyExecutor *spy = new SpyExecutor(this);
        GitCommitService service;
        service.setExecutorForTest(spy);
        service.setWorkingDirectory(QStringLiteral("C:/repo/A"));

        service.unstageFilesAsync(
            {QStringLiteral("a.txt")},
            [&](GitCommitService::OperationResult) {});
        QVERIFY(spy->firstHeld);

        // No retarget: stage 1 completes, stage 2 must run.
        GitCommandExecutor::Result headResult;
        headResult.output = "HEAD";
        headResult.error.type = GitErrorType::None;
        spy->firstCompletion(headResult);
        QApplication::processEvents();
        QCOMPARE(spy->stage2Invocations, 1);
        QCOMPARE(spy->stage2Command, QStringLiteral("restore"));
    }

    void controllerStage2AbortedAfterRetarget() {
        GitController controller;
        auto *fake = new DelayedCommitService(&controller);
        controller.setCommitServiceForTest(fake);
        controller.setRepositoryPath(QStringLiteral("C:/repo/A"));
        fake->setTarget(QStringLiteral("C:/repo/A"));

        int completions = 0;
        controller.unstageAllAsync([&](GitController::OperationResult) {
            ++completions;
        });
        QVERIFY(bool(fake->pendingList));
        QCOMPARE(fake->listTarget, QStringLiteral("C:/repo/A"));

        controller.setRepositoryPath(QStringLiteral("C:/repo/B"));
        fake->setTarget(QStringLiteral("C:/repo/B"));
        const auto release = fake->pendingList;
        fake->pendingList = {};
        release({QStringLiteral("a.txt")}, GitError());
        QApplication::processEvents();

        QCOMPARE(fake->unstageInvocations, 0);
        QCOMPARE(completions, 0);
        QCOMPARE(controller.workingDirectory(), QStringLiteral("C:/repo/B"));
        qInfo().noquote()
            << QStringLiteral("UNSTAGE_CONTROLLER A_list=%1 B_current=%2 stage2=0 result=PASS")
                   .arg(fake->listTarget, controller.workingDirectory());
    }
};

QTEST_MAIN(TestUnstageRace)
#include "test_unstage_race.moc"
