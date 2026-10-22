// v4-P6-21R6: branch switch/create fallback must not execute checkout
// after the shared executor has been retargeted to another repository.
#include <QtTest>

#include "GitBranchService.h"
#include "GitCommandExecutor.h"

namespace {

class FallbackSpyExecutor : public GitCommandExecutor {
public:
    using GitCommandExecutor::GitCommandExecutor;

    bool firstHeld = false;
    QString firstCommand;
    QString firstWorkingDirectory;
    GitCommandExecutor::Completion firstCompletion;
    int fallbackInvocations = 0;
    QString fallbackCommand;

    void executeAsync(const QString &command, const QStringList &args,
                      GitCommandExecutor::Completion completion,
                      int /*timeoutMs*/) override {
        Q_UNUSED(args);
        if (!firstHeld) {
            firstHeld = true;
            firstCommand = command;
            firstWorkingDirectory = workingDirectory();
            firstCompletion = completion;
            return;
        }
        ++fallbackInvocations;
        fallbackCommand = command;
        GitCommandExecutor::Result success;
        success.exitCode = 0;
        success.exitStatus = QProcess::NormalExit;
        completion(success);
    }

    void finishSwitchAsUnavailable() {
        GitCommandExecutor::Result unavailable;
        unavailable.exitCode = 1;
        unavailable.exitStatus = QProcess::NormalExit;
        unavailable.error.type = GitErrorType::ProcessFailed;
        unavailable.errorOutput = "git: 'switch' is not a git command";
        firstCompletion(unavailable);
    }
};

}  // namespace

class TestBranchFallbackRace : public QObject {
    Q_OBJECT

private slots:
    void checkoutFallbackAbortsAfterRetarget() {
        auto *spy = new FallbackSpyExecutor(this);
        GitBranchService service;
        service.setExecutorForTest(spy);
        service.setWorkingDirectory(QStringLiteral("C:/repo/A"));

        int completions = 0;
        bool failed = false;
        service.checkoutBranchAsync(QStringLiteral("feature/a"),
            [&](GitBranchService::OperationResult result) {
                ++completions;
                failed = !result.success && result.error.hasError();
            });
        QCOMPARE(spy->firstCommand, QStringLiteral("switch"));
        QCOMPARE(spy->firstWorkingDirectory, QStringLiteral("C:/repo/A"));
        service.setWorkingDirectory(QStringLiteral("C:/repo/B"));
        QCOMPARE(spy->workingDirectory(), QStringLiteral("C:/repo/B"));
        spy->finishSwitchAsUnavailable();

        QCOMPARE(spy->fallbackInvocations, 0);
        QCOMPARE(completions, 1);
        QVERIFY(failed);
        qInfo().noquote()
            << QStringLiteral("BRANCH_FALLBACK operation=checkout A_stage1_target=C:/repo/A B_current=C:/repo/B A_late=unavailable fallback_stage2=0 result=PASS");
    }

    void checkoutFallbackRunsInOriginalSession() {
        auto *spy = new FallbackSpyExecutor(this);
        GitBranchService service;
        service.setExecutorForTest(spy);
        service.setWorkingDirectory(QStringLiteral("C:/repo/A"));

        bool succeeded = false;
        service.checkoutBranchAsync(QStringLiteral("feature/a"),
            [&](GitBranchService::OperationResult result) { succeeded = result.success; });
        spy->finishSwitchAsUnavailable();

        QCOMPARE(spy->fallbackInvocations, 1);
        QCOMPARE(spy->fallbackCommand, QStringLiteral("checkout"));
        QVERIFY(succeeded);
    }

    void createFallbackAbortsAfterRetarget() {
        auto *spy = new FallbackSpyExecutor(this);
        GitBranchService service;
        service.setExecutorForTest(spy);
        service.setWorkingDirectory(QStringLiteral("C:/repo/A"));

        int completions = 0;
        bool failed = false;
        service.createBranchAsync(QStringLiteral("feature/a"), {}, true,
            [&](GitBranchService::OperationResult result) {
                ++completions;
                failed = !result.success && result.error.hasError();
            });
        QCOMPARE(spy->firstCommand, QStringLiteral("switch"));
        QCOMPARE(spy->firstWorkingDirectory, QStringLiteral("C:/repo/A"));
        service.setWorkingDirectory(QStringLiteral("C:/repo/B"));
        QCOMPARE(spy->workingDirectory(), QStringLiteral("C:/repo/B"));
        spy->finishSwitchAsUnavailable();

        QCOMPARE(spy->fallbackInvocations, 0);
        QCOMPARE(completions, 1);
        QVERIFY(failed);
        qInfo().noquote()
            << QStringLiteral("BRANCH_FALLBACK operation=create A_stage1_target=C:/repo/A B_current=C:/repo/B A_late=unavailable fallback_stage2=0 result=PASS");
    }

    void checkoutFallbackAbortsAfterRepositoryRoundTrip() {
        auto *spy = new FallbackSpyExecutor(this);
        GitBranchService service;
        service.setExecutorForTest(spy);
        service.setWorkingDirectory(QStringLiteral("C:/repo/A"));

        int completions = 0;
        service.checkoutBranchAsync(
            QStringLiteral("feature/a"),
            [&](GitBranchService::OperationResult) { ++completions; });
        QCOMPARE(spy->firstWorkingDirectory, QStringLiteral("C:/repo/A"));

        service.setWorkingDirectory(QStringLiteral("C:/repo/B"));
        service.setWorkingDirectory(QStringLiteral("C:/repo/A"));
        spy->finishSwitchAsUnavailable();

        QCOMPARE(spy->fallbackInvocations, 0);
        QCOMPARE(completions, 1);
        qInfo().noquote()
            << QStringLiteral("BRANCH_FALLBACK_ABA operation=checkout A_generation=1 B_generation=2 A_reopen_generation=3 fallback_stage2=0 result=PASS");
    }

    void createFallbackAbortsAfterRepositoryRoundTrip() {
        auto *spy = new FallbackSpyExecutor(this);
        GitBranchService service;
        service.setExecutorForTest(spy);
        service.setWorkingDirectory(QStringLiteral("C:/repo/A"));

        int completions = 0;
        service.createBranchAsync(
            QStringLiteral("feature/a"), {}, true,
            [&](GitBranchService::OperationResult) { ++completions; });
        QCOMPARE(spy->firstWorkingDirectory, QStringLiteral("C:/repo/A"));

        service.setWorkingDirectory(QStringLiteral("C:/repo/B"));
        service.setWorkingDirectory(QStringLiteral("C:/repo/A"));
        spy->finishSwitchAsUnavailable();

        QCOMPARE(spy->fallbackInvocations, 0);
        QCOMPARE(completions, 1);
        qInfo().noquote()
            << QStringLiteral("BRANCH_FALLBACK_ABA operation=create A_generation=1 B_generation=2 A_reopen_generation=3 fallback_stage2=0 result=PASS");
    }

    void createFallbackRunsInOriginalSession() {
        auto *spy = new FallbackSpyExecutor(this);
        GitBranchService service;
        service.setExecutorForTest(spy);
        service.setWorkingDirectory(QStringLiteral("C:/repo/A"));

        bool succeeded = false;
        service.createBranchAsync(QStringLiteral("feature/a"), {}, true,
            [&](GitBranchService::OperationResult result) { succeeded = result.success; });
        spy->finishSwitchAsUnavailable();

        QCOMPARE(spy->fallbackInvocations, 1);
        QCOMPARE(spy->fallbackCommand, QStringLiteral("checkout"));
        QVERIFY(succeeded);
    }
};

QTEST_MAIN(TestBranchFallbackRace)
#include "test_branch_fallback_race.moc"
