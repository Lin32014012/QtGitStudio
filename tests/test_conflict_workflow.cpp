#include "ConflictResolutionPlan.h"
#include "GitConflictResolutionService.h"
#include "GitConflictService.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include <functional>
#include <memory>

namespace {

bool waitUntil(const std::function<bool()> &predicate, int timeoutMs = 20000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QTest::qWait(5);
    }
    return predicate();
}

bool git(const QString &repository, const QStringList &args, bool expectSuccess = true) {
    QStringList full{QStringLiteral("-C"), repository};
    full.append(args);
    QProcess process;
    process.start(QStringLiteral("git"), full);
    if (!process.waitForStarted(5000) || !process.waitForFinished(15000)) return false;
    return (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) == expectSuccess;
}

bool writeFile(const QString &repository, const QString &path, const QByteArray &contents) {
    const QString absolute = QDir(repository).filePath(path);
    if (!QDir().mkpath(QFileInfo(absolute).dir().absolutePath())) return false;
    QFile file(absolute);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

struct Fixture {
    QTemporaryDir dir;
    QString repository;
};

std::unique_ptr<Fixture> makeOperationConflict(ConflictOperationType operation) {
    auto fixture = std::make_unique<Fixture>();
    if (!fixture->dir.isValid()) return {};
    fixture->repository = fixture->dir.path();
    const QString repo = fixture->repository;
    if (!git(repo, {QStringLiteral("init")})
        || !git(repo, {QStringLiteral("config"), QStringLiteral("user.name"), QStringLiteral("Test")})
        || !git(repo, {QStringLiteral("config"), QStringLiteral("user.email"), QStringLiteral("test@example.invalid")})
        || !git(repo, {QStringLiteral("config"), QStringLiteral("core.autocrlf"), QStringLiteral("false")})
        || !writeFile(repo, QStringLiteral("file.txt"), "base\n")
        || !git(repo, {QStringLiteral("add"), QStringLiteral("--"), QStringLiteral("file.txt")})
        || !git(repo, {QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("base")})
        || !git(repo, {QStringLiteral("branch"), QStringLiteral("-M"), QStringLiteral("main")})) return {};

    if (operation == ConflictOperationType::Merge || operation == ConflictOperationType::Rebase) {
        if (!git(repo, {QStringLiteral("switch"), QStringLiteral("-c"), QStringLiteral("topic")})
            || !writeFile(repo, QStringLiteral("file.txt"), "topic\n")
            || !git(repo, {QStringLiteral("add"), QStringLiteral("--"), QStringLiteral("file.txt")})
            || !git(repo, {QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("topic")})
            || !git(repo, {QStringLiteral("switch"), QStringLiteral("main")})
            || !writeFile(repo, QStringLiteral("file.txt"), "main\n")
            || !git(repo, {QStringLiteral("add"), QStringLiteral("--"), QStringLiteral("file.txt")})
            || !git(repo, {QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("main")})) return {};
        if (operation == ConflictOperationType::Merge) {
            if (!git(repo, {QStringLiteral("merge"), QStringLiteral("topic")}, false)) return {};
        } else {
            if (!git(repo, {QStringLiteral("switch"), QStringLiteral("topic")})
                || !git(repo, {QStringLiteral("rebase"), QStringLiteral("main")}, false)) return {};
        }
        return fixture;
    }

    if (operation == ConflictOperationType::Revert) {
        if (!writeFile(repo, QStringLiteral("file.txt"), "source\n")
            || !git(repo, {QStringLiteral("add"), QStringLiteral("--"), QStringLiteral("file.txt")})
            || !git(repo, {QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("source")})
            || !writeFile(repo, QStringLiteral("file.txt"), "main\n")
            || !git(repo, {QStringLiteral("add"), QStringLiteral("--"), QStringLiteral("file.txt")})
            || !git(repo, {QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("main")})
            || !git(repo, {QStringLiteral("revert"), QStringLiteral("--no-edit"), QStringLiteral("HEAD~1")}, false)) {
            return {};
        }
        return fixture;
    }

    if (!git(repo, {QStringLiteral("switch"), QStringLiteral("-c"), QStringLiteral("source")})
        || !writeFile(repo, QStringLiteral("file.txt"), "source\n")
        || !git(repo, {QStringLiteral("add"), QStringLiteral("--"), QStringLiteral("file.txt")})
        || !git(repo, {QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("source")})
        || !git(repo, {QStringLiteral("switch"), QStringLiteral("main")})
        || !writeFile(repo, QStringLiteral("file.txt"), "main\n")
        || !git(repo, {QStringLiteral("add"), QStringLiteral("--"), QStringLiteral("file.txt")})
        || !git(repo, {QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("main")})
        || !git(repo, {QStringLiteral("cherry-pick"), QStringLiteral("source")}, false)) return {};
    return fixture;
}

std::unique_ptr<Fixture> makeRebaseWithTwoConflictBatches() {
    auto fixture = std::make_unique<Fixture>();
    if (!fixture->dir.isValid()) return {};
    fixture->repository = fixture->dir.path();
    const QString repo = fixture->repository;
    if (!git(repo, {QStringLiteral("init")})
        || !git(repo, {QStringLiteral("config"), QStringLiteral("user.name"), QStringLiteral("Test")})
        || !git(repo, {QStringLiteral("config"), QStringLiteral("user.email"), QStringLiteral("test@example.invalid")})
        || !git(repo, {QStringLiteral("config"), QStringLiteral("core.autocrlf"), QStringLiteral("false")})
        || !writeFile(repo, QStringLiteral("first.txt"), "base first\n")
        || !writeFile(repo, QStringLiteral("second.txt"), "base second\n")
        || !git(repo, {QStringLiteral("add"), QStringLiteral("--"), QStringLiteral("first.txt"), QStringLiteral("second.txt")})
        || !git(repo, {QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("base")})
        || !git(repo, {QStringLiteral("branch"), QStringLiteral("-M"), QStringLiteral("main")})
        || !git(repo, {QStringLiteral("switch"), QStringLiteral("-c"), QStringLiteral("topic")})
        || !writeFile(repo, QStringLiteral("first.txt"), "topic first\n")
        || !git(repo, {QStringLiteral("add"), QStringLiteral("--"), QStringLiteral("first.txt")})
        || !git(repo, {QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("topic first")})
        || !writeFile(repo, QStringLiteral("second.txt"), "topic second\n")
        || !git(repo, {QStringLiteral("add"), QStringLiteral("--"), QStringLiteral("second.txt")})
        || !git(repo, {QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("topic second")})
        || !git(repo, {QStringLiteral("switch"), QStringLiteral("main")})
        || !writeFile(repo, QStringLiteral("first.txt"), "main first\n")
        || !writeFile(repo, QStringLiteral("second.txt"), "main second\n")
        || !git(repo, {QStringLiteral("add"), QStringLiteral("--"), QStringLiteral("first.txt"), QStringLiteral("second.txt")})
        || !git(repo, {QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("main changes")})
        || !git(repo, {QStringLiteral("switch"), QStringLiteral("topic")})
        || !git(repo, {QStringLiteral("rebase"), QStringLiteral("main")}, false)) {
        return {};
    }
    return fixture;
}

ConflictSnapshot scan(const QString &repository, GitConflictService *reader) {
    reader->setWorkingDirectory(repository);
    ConflictScanResult result;
    bool done = false;
    reader->scanConflictsAsync([&](ConflictScanResult value) { result = std::move(value); done = true; });
    return waitUntil([&] { return done; }) ? result.snapshot : ConflictSnapshot{};
}

ConflictResolutionPlan readyPlan(const QString &repository, const ConflictFile &file, quint64 generation) {
    ConflictResolutionPlan plan;
    plan.repositoryPath = repository;
    plan.filePath = file.path;
    plan.conflictType = file.type;
    plan.action = ResolutionAction::Manual;
    plan.resultContent = QStringLiteral("resolved\n");
    plan.ready = true;
    plan.unresolvedHunks = 0;
    plan.sessionGeneration = generation;
    plan.expectedBaseObjectId = file.baseObjectId;
    plan.expectedOursObjectId = file.oursObjectId;
    plan.expectedTheirsObjectId = file.theirsObjectId;
    QFile worktree(QDir(repository).filePath(file.path));
    plan.expectedWorkingTreeExists = worktree.exists() && worktree.open(QIODevice::ReadOnly);
    if (plan.expectedWorkingTreeExists) {
        plan.expectedWorkingTreeSha256 = QString::fromLatin1(
            QCryptographicHash::hash(worktree.readAll(), QCryptographicHash::Sha256).toHex());
    }
    return plan;
}

ConflictApplyResult resolve(GitConflictResolutionService *service,
                            const ConflictResolutionPlan &plan) {
    ConflictApplyResult result;
    bool done = false;
    service->applyPlanAsync(plan, [&](ConflictApplyResult value) { result = std::move(value); done = true; });
    waitUntil([&] { return done; });
    return result;
}

ConflictApplyResult perform(GitConflictResolutionService *service,
                            ConflictOperationType operation, bool abort) {
    ConflictApplyResult result;
    bool done = false;
    const auto completion = [&](ConflictApplyResult value) { result = std::move(value); done = true; };
    if (abort) service->abortOperationAsync(operation, completion);
    else service->continueOperationAsync(operation, completion);
    waitUntil([&] { return done; });
    return result;
}

} // namespace

class TestConflictWorkflow final : public QObject {
    Q_OBJECT
private slots:
    void mergeApplyDoesNotAutoContinue();
    void mergeContinueAfterVerifiedResolution();
    void mergeAbortHasExplicitServicePath();
    void continueRejectsRemainingConflicts();
    void rebaseContinueUsesNonInteractiveEditor();
    void rebaseAbortRestoresOperationState();
    void cherryPickContinue();
    void cherryPickAbort();
    void revertContinue();
    void revertAbort();
    void rebaseContinueCanReturnTheNextConflictBatch();
    void unknownOperationCannotContinueOrAbort();
    void continueFailurePreservesResolvedIndex();
    void repositorySwitchBeforeContinueStopsAtFence();
};

void TestConflictWorkflow::mergeApplyDoesNotAutoContinue() {
    auto fixture = makeOperationConflict(ConflictOperationType::Merge);
    QVERIFY(fixture);
    GitConflictService reader;
    const auto snapshot = scan(fixture->repository, &reader);
    QCOMPARE(snapshot.operationType, ConflictOperationType::Merge);
    GitConflictResolutionService service;
    service.setWorkingDirectory(fixture->repository);
    const auto applied = resolve(&service, readyPlan(fixture->repository, snapshot.files.first(), service.repositoryGeneration()));
    QVERIFY2(applied.success(), qPrintable(applied.message));
    QProcess mergeHead;
    mergeHead.start(QStringLiteral("git"), {QStringLiteral("-C"), fixture->repository, QStringLiteral("rev-parse"), QStringLiteral("-q"), QStringLiteral("--verify"), QStringLiteral("MERGE_HEAD")});
    QVERIFY(mergeHead.waitForFinished(5000));
    QVERIFY(mergeHead.exitCode() == 0); // Applying only stages; it never commits automatically.
}

void TestConflictWorkflow::mergeContinueAfterVerifiedResolution() {
    auto fixture = makeOperationConflict(ConflictOperationType::Merge);
    QVERIFY(fixture);
    GitConflictService reader;
    const auto snapshot = scan(fixture->repository, &reader);
    GitConflictResolutionService service;
    service.setWorkingDirectory(fixture->repository);
    QVERIFY(resolve(&service, readyPlan(fixture->repository, snapshot.files.first(), service.repositoryGeneration())).success());
    QVERIFY2(perform(&service, ConflictOperationType::Merge, false).success(), "merge --continue failed");
}

void TestConflictWorkflow::mergeAbortHasExplicitServicePath() {
    auto fixture = makeOperationConflict(ConflictOperationType::Merge);
    QVERIFY(fixture);
    GitConflictResolutionService service;
    service.setWorkingDirectory(fixture->repository);
    QVERIFY2(perform(&service, ConflictOperationType::Merge, true).success(), "merge --abort failed");
}

void TestConflictWorkflow::continueRejectsRemainingConflicts() {
    auto fixture = makeOperationConflict(ConflictOperationType::Merge);
    QVERIFY(fixture);
    GitConflictResolutionService service;
    service.setWorkingDirectory(fixture->repository);
    QCOMPARE(perform(&service, ConflictOperationType::Merge, false).error, ConflictApplyError::InvalidPlan);
}

void TestConflictWorkflow::rebaseContinueUsesNonInteractiveEditor() {
    auto fixture = makeOperationConflict(ConflictOperationType::Rebase);
    QVERIFY(fixture);
    GitConflictService reader;
    const auto snapshot = scan(fixture->repository, &reader);
    QCOMPARE(snapshot.operationType, ConflictOperationType::Rebase);
    GitConflictResolutionService service;
    service.setWorkingDirectory(fixture->repository);
    QVERIFY(resolve(&service, readyPlan(fixture->repository, snapshot.files.first(), service.repositoryGeneration())).success());
    QVERIFY2(perform(&service, ConflictOperationType::Rebase, false).success(), "rebase --continue failed");
}

void TestConflictWorkflow::rebaseAbortRestoresOperationState() {
    auto fixture = makeOperationConflict(ConflictOperationType::Rebase);
    QVERIFY(fixture);
    GitConflictResolutionService service;
    service.setWorkingDirectory(fixture->repository);
    QVERIFY2(perform(&service, ConflictOperationType::Rebase, true).success(), "rebase --abort failed");
}

void TestConflictWorkflow::cherryPickContinue() {
    auto fixture = makeOperationConflict(ConflictOperationType::CherryPick);
    QVERIFY(fixture);
    GitConflictService reader;
    const auto snapshot = scan(fixture->repository, &reader);
    QCOMPARE(snapshot.operationType, ConflictOperationType::CherryPick);
    GitConflictResolutionService service;
    service.setWorkingDirectory(fixture->repository);
    QVERIFY(resolve(&service, readyPlan(fixture->repository, snapshot.files.first(), service.repositoryGeneration())).success());
    QVERIFY2(perform(&service, ConflictOperationType::CherryPick, false).success(), "cherry-pick --continue failed");
}

void TestConflictWorkflow::cherryPickAbort() {
    auto fixture = makeOperationConflict(ConflictOperationType::CherryPick);
    QVERIFY(fixture);
    GitConflictResolutionService service;
    service.setWorkingDirectory(fixture->repository);
    QVERIFY2(perform(&service, ConflictOperationType::CherryPick, true).success(), "cherry-pick --abort failed");
}

void TestConflictWorkflow::revertContinue() {
    auto fixture = makeOperationConflict(ConflictOperationType::Revert);
    QVERIFY(fixture);
    GitConflictService reader;
    const auto snapshot = scan(fixture->repository, &reader);
    QCOMPARE(snapshot.operationType, ConflictOperationType::Revert);
    GitConflictResolutionService service;
    service.setWorkingDirectory(fixture->repository);
    QVERIFY(resolve(&service, readyPlan(fixture->repository, snapshot.files.first(), service.repositoryGeneration())).success());
    QVERIFY2(perform(&service, ConflictOperationType::Revert, false).success(), "revert --continue failed");
}

void TestConflictWorkflow::revertAbort() {
    auto fixture = makeOperationConflict(ConflictOperationType::Revert);
    QVERIFY(fixture);
    GitConflictResolutionService service;
    service.setWorkingDirectory(fixture->repository);
    QVERIFY2(perform(&service, ConflictOperationType::Revert, true).success(), "revert --abort failed");
}

void TestConflictWorkflow::rebaseContinueCanReturnTheNextConflictBatch() {
    auto fixture = makeRebaseWithTwoConflictBatches();
    QVERIFY(fixture);
    GitConflictService reader;
    const auto first = scan(fixture->repository, &reader);
    QCOMPARE(first.operationType, ConflictOperationType::Rebase);
    QCOMPARE(first.conflictCount, 1);
    GitConflictResolutionService service;
    service.setWorkingDirectory(fixture->repository);
    QVERIFY(resolve(&service, readyPlan(fixture->repository, first.files.first(), service.repositoryGeneration())).success());
    const auto continued = perform(&service, ConflictOperationType::Rebase, false);
    QVERIFY2(continued.success(), qPrintable(continued.message));
    QVERIFY(continued.continuedButMoreConflicts);
    QCOMPARE(continued.snapshot.operationType, ConflictOperationType::Rebase);
    QCOMPARE(continued.snapshot.conflictCount, 1);
    QVERIFY(continued.snapshot.files.first().path == QStringLiteral("second.txt"));
}

void TestConflictWorkflow::unknownOperationCannotContinueOrAbort() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    GitConflictResolutionService service;
    service.setWorkingDirectory(temporary.path());
    QCOMPARE(perform(&service, ConflictOperationType::Unknown, false).error, ConflictApplyError::UnknownOperation);
    QCOMPARE(perform(&service, ConflictOperationType::Unknown, true).error, ConflictApplyError::UnknownOperation);
}

void TestConflictWorkflow::continueFailurePreservesResolvedIndex() {
    auto fixture = makeOperationConflict(ConflictOperationType::Merge);
    QVERIFY(fixture);
    GitConflictService reader;
    const auto snapshot = scan(fixture->repository, &reader);
    GitConflictResolutionService service;
    service.setWorkingDirectory(fixture->repository);
    QVERIFY(resolve(&service, readyPlan(fixture->repository, snapshot.files.first(), service.repositoryGeneration())).success());
    service.setFailureInjectionForTesting({false, false, false, true});
    QCOMPARE(perform(&service, ConflictOperationType::Merge, false).error, ConflictApplyError::ContinueFailed);
    QVERIFY(git(fixture->repository, {QStringLiteral("diff"), QStringLiteral("--cached"), QStringLiteral("--quiet")}, false));
}

void TestConflictWorkflow::repositorySwitchBeforeContinueStopsAtFence() {
    auto fixture = makeOperationConflict(ConflictOperationType::Merge);
    QVERIFY(fixture);
    GitConflictService reader;
    const auto snapshot = scan(fixture->repository, &reader);
    GitConflictResolutionService service;
    service.setWorkingDirectory(fixture->repository);
    QVERIFY(resolve(&service, readyPlan(fixture->repository, snapshot.files.first(),
                                         service.repositoryGeneration())).success());
    QTemporaryDir repositoryB;
    QVERIFY(repositoryB.isValid());
    service.setBeforeContinueHookForTesting([&service, &repositoryB] {
        service.setWorkingDirectory(repositoryB.path());
    });
    QCOMPARE(perform(&service, ConflictOperationType::Merge, false).error,
             ConflictApplyError::StaleSession);
    QVERIFY(git(fixture->repository, {QStringLiteral("rev-parse"), QStringLiteral("-q"),
                                      QStringLiteral("--verify"), QStringLiteral("MERGE_HEAD")}));
    qInfo().noquote() << QStringLiteral("CROSS_REPO_FENCE before_continue=1 repo_a=%1 repo_b=%2")
                            .arg(fixture->repository, repositoryB.path());
}

QTEST_MAIN(TestConflictWorkflow)
#include "test_conflict_workflow.moc"
