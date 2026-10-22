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

bool waitUntil(const std::function<bool()> &predicate, int timeoutMs = 15000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QTest::qWait(5);
    }
    return predicate();
}

bool git(const QString &repository, const QStringList &args) {
    QProcess process;
    QStringList fullArgs{QStringLiteral("-C"), repository};
    fullArgs.append(args);
    process.start(QStringLiteral("git"), fullArgs);
    return process.waitForStarted(5000) && process.waitForFinished(10000)
        && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool writeFile(const QString &repository, const QString &path, const QByteArray &contents) {
    const QString absolute = QDir(repository).filePath(path);
    if (!QDir().mkpath(QFileInfo(absolute).dir().absolutePath())) return false;
    QFile file(absolute);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

QByteArray readFile(const QString &repository, const QString &path) {
    QFile file(QDir(repository).filePath(path));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

int unmergedIndexEntryCount(const QString &repository, const QString &path) {
    QProcess process;
    process.start(QStringLiteral("git"), {QStringLiteral("-C"), repository,
                                           QStringLiteral("ls-files"), QStringLiteral("-u"),
                                           QStringLiteral("-z"), QStringLiteral("--"), path});
    if (!process.waitForStarted(5000) || !process.waitForFinished(10000)
        || process.exitCode() != 0) {
        return -1;
    }
    int entries = 0;
    for (const QByteArray &entry : process.readAllStandardOutput().split('\0')) {
        if (!entry.isEmpty()) ++entries;
    }
    return entries;
}

struct Fixture {
    QTemporaryDir dir;
    QString repository;
    QString path;
};

std::unique_ptr<Fixture> makeBothModified(const QString &path = QStringLiteral("file.txt"),
                                          const QByteArray &base = "base\n",
                                          const QByteArray &ours = "ours\n",
                                          const QByteArray &theirs = "theirs\n") {
    auto fixture = std::make_unique<Fixture>();
    if (!fixture->dir.isValid()) return {};
    fixture->repository = fixture->dir.path();
    fixture->path = path;
    const QString repo = fixture->repository;
    if (!git(repo, {QStringLiteral("init")})
        || !git(repo, {QStringLiteral("config"), QStringLiteral("user.name"), QStringLiteral("Test")})
        || !git(repo, {QStringLiteral("config"), QStringLiteral("user.email"), QStringLiteral("test@example.invalid")})
        || !git(repo, {QStringLiteral("config"), QStringLiteral("core.autocrlf"), QStringLiteral("false")})
        || !writeFile(repo, path, base)
        || !git(repo, {QStringLiteral("add"), QStringLiteral("--"), path})
        || !git(repo, {QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("base")})
        || !git(repo, {QStringLiteral("branch"), QStringLiteral("-M"), QStringLiteral("main")})
        || !git(repo, {QStringLiteral("switch"), QStringLiteral("-c"), QStringLiteral("ours")})
        || !writeFile(repo, path, ours)
        || !git(repo, {QStringLiteral("add"), QStringLiteral("--"), path})
        || !git(repo, {QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("ours")})
        || !git(repo, {QStringLiteral("switch"), QStringLiteral("main")})
        || !writeFile(repo, path, theirs)
        || !git(repo, {QStringLiteral("add"), QStringLiteral("--"), path})
        || !git(repo, {QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("theirs")})) {
        return {};
    }
    QProcess merge;
    merge.start(QStringLiteral("git"), {QStringLiteral("-C"), repo, QStringLiteral("merge"), QStringLiteral("ours")});
    if (!merge.waitForStarted(5000) || !merge.waitForFinished(10000) || merge.exitCode() == 0) return {};
    return fixture;
}

std::unique_ptr<Fixture> makeBothAdded(const QString &path = QStringLiteral("added.txt")) {
    auto fixture = std::make_unique<Fixture>();
    if (!fixture->dir.isValid()) return {};
    fixture->repository = fixture->dir.path();
    fixture->path = path;
    const QString repo = fixture->repository;
    if (!git(repo, {QStringLiteral("init")})
        || !git(repo, {QStringLiteral("config"), QStringLiteral("user.name"), QStringLiteral("Test")})
        || !git(repo, {QStringLiteral("config"), QStringLiteral("user.email"), QStringLiteral("test@example.invalid")})
        || !git(repo, {QStringLiteral("config"), QStringLiteral("core.autocrlf"), QStringLiteral("false")})
        || !writeFile(repo, QStringLiteral("README.md"), "base\n")
        || !git(repo, {QStringLiteral("add"), QStringLiteral("--"), QStringLiteral("README.md")})
        || !git(repo, {QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("base")})
        || !git(repo, {QStringLiteral("branch"), QStringLiteral("-M"), QStringLiteral("main")})
        || !git(repo, {QStringLiteral("switch"), QStringLiteral("-c"), QStringLiteral("other")})
        || !writeFile(repo, path, "other add\n")
        || !git(repo, {QStringLiteral("add"), QStringLiteral("--"), path})
        || !git(repo, {QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("other add")})
        || !git(repo, {QStringLiteral("switch"), QStringLiteral("main")})
        || !writeFile(repo, path, "main add\n")
        || !git(repo, {QStringLiteral("add"), QStringLiteral("--"), path})
        || !git(repo, {QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("main add")})) {
        return {};
    }
    QProcess merge;
    merge.start(QStringLiteral("git"), {QStringLiteral("-C"), repo, QStringLiteral("merge"), QStringLiteral("other")});
    if (!merge.waitForStarted(5000) || !merge.waitForFinished(10000) || merge.exitCode() == 0) return {};
    return fixture;
}

std::unique_ptr<Fixture> makeDeleteModifyConflict(bool currentBranchDeletes,
                                                   const QString &path = QStringLiteral("deleted.txt")) {
    auto fixture = std::make_unique<Fixture>();
    if (!fixture->dir.isValid()) return {};
    fixture->repository = fixture->dir.path();
    fixture->path = path;
    const QString repo = fixture->repository;
    if (!git(repo, {QStringLiteral("init")})
        || !git(repo, {QStringLiteral("config"), QStringLiteral("user.name"), QStringLiteral("Test")})
        || !git(repo, {QStringLiteral("config"), QStringLiteral("user.email"), QStringLiteral("test@example.invalid")})
        || !git(repo, {QStringLiteral("config"), QStringLiteral("core.autocrlf"), QStringLiteral("false")})
        || !writeFile(repo, path, "base\n")
        || !git(repo, {QStringLiteral("add"), QStringLiteral("--"), path})
        || !git(repo, {QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("base")})
        || !git(repo, {QStringLiteral("branch"), QStringLiteral("-M"), QStringLiteral("main")})
        || !git(repo, {QStringLiteral("switch"), QStringLiteral("-c"), QStringLiteral("other")})) {
        return {};
    }

    const auto commitDelete = [&] {
        return git(repo, {QStringLiteral("rm"), QStringLiteral("--"), path})
            && git(repo, {QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("delete")});
    };
    const auto commitModify = [&] {
        return writeFile(repo, path, "modified\n")
            && git(repo, {QStringLiteral("add"), QStringLiteral("--"), path})
            && git(repo, {QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("modify")});
    };
    if (currentBranchDeletes) {
        if (!commitModify() || !git(repo, {QStringLiteral("switch"), QStringLiteral("main")})
            || !commitDelete()) return {};
    } else {
        if (!commitDelete() || !git(repo, {QStringLiteral("switch"), QStringLiteral("main")})
            || !commitModify()) return {};
    }
    QProcess merge;
    merge.start(QStringLiteral("git"), {QStringLiteral("-C"), repo, QStringLiteral("merge"), QStringLiteral("other")});
    if (!merge.waitForStarted(5000) || !merge.waitForFinished(10000) || merge.exitCode() == 0) return {};
    return fixture;
}

ConflictSnapshot scan(const QString &repository, GitConflictService *service) {
    service->setWorkingDirectory(repository);
    ConflictScanResult result;
    bool done = false;
    service->scanConflictsAsync([&](ConflictScanResult value) { result = std::move(value); done = true; });
    if (!waitUntil([&] { return done; })) return {};
    return result.snapshot;
}

ConflictResolutionPlan planFor(const QString &repository, const ConflictFile &file,
                               quint64 generation, const QString &text,
                               ResolutionAction action = ResolutionAction::Manual) {
    ConflictResolutionPlan plan;
    plan.repositoryPath = repository;
    plan.filePath = file.path;
    plan.conflictType = file.type;
    plan.action = action;
    plan.resultContent = text;
    plan.ready = true;
    plan.unresolvedHunks = 0;
    plan.sessionGeneration = generation;
    plan.expectedBaseObjectId = file.baseObjectId;
    plan.expectedOursObjectId = file.oursObjectId;
    plan.expectedTheirsObjectId = file.theirsObjectId;
    const QByteArray current = readFile(repository, file.path);
    plan.expectedWorkingTreeExists = QFileInfo(QDir(repository).filePath(file.path)).exists();
    plan.expectedWorkingTreeSha256 = QString::fromLatin1(
        QCryptographicHash::hash(current, QCryptographicHash::Sha256).toHex());
    return plan;
}

ConflictApplyResult apply(GitConflictResolutionService *service,
                          const ConflictResolutionPlan &plan) {
    ConflictApplyResult result;
    bool done = false;
    service->applyPlanAsync(plan, [&](ConflictApplyResult value) { result = std::move(value); done = true; });
    if (!waitUntil([&] { return done; })) {
        result.error = ConflictApplyError::GitCommandFailed;
        result.message = QStringLiteral("Timed out");
    }
    return result;
}

} // namespace

class TestConflictResolutionService final : public QObject {
    Q_OBJECT
private slots:
    void manualResultAppliesStagesAndVerifies();
    void oursAndTheirsPlansApply();
    void bothAddedOursTheirsAndManualPlansApply();
    void deletedByUsAndThemKeepPlansApply();
    void pathsWithDashSpacesAndUnicodeStageExactly();
    void preservesCrLfAndUtf8Bom();
    void externalConflictChangeIsRefused();
    void externalWorktreeChangeIsRefused();
    void staleRepositorySessionIsRefused();
    void invalidTraversalIsRefused();
    void deletePlanUsesGitRm();
    void binaryOursAndTheirsBlobPlansApply();
    void writeFailureNeverStages();
    void stageFailureReportsWorktreeMutation();
    void verifyFailureIsHonest();
    void applyAllReportsPartialSuccess();
    void repositorySwitchBetweenWriteAndStageStopsAtFence();
};

void TestConflictResolutionService::manualResultAppliesStagesAndVerifies() {
    auto fixture = makeBothModified();
    QVERIFY(fixture);
    GitConflictService reader;
    const ConflictSnapshot snapshot = scan(fixture->repository, &reader);
    QCOMPARE(snapshot.conflictCount, 1);
    GitConflictResolutionService service;
    service.setWorkingDirectory(fixture->repository);
    const QString beforeSha = QString::fromLatin1(QCryptographicHash::hash(
        readFile(fixture->repository, fixture->path), QCryptographicHash::Sha256).toHex());
    QCOMPARE(unmergedIndexEntryCount(fixture->repository, fixture->path), 3);
    const auto result = apply(&service, planFor(fixture->repository, snapshot.files.first(),
                                                 service.repositoryGeneration(), QStringLiteral("manual\n")));
    QVERIFY2(result.success(), qPrintable(result.message));
    QVERIFY(result.workingTreeUpdated);
    QVERIFY(result.staged);
    QCOMPARE(readFile(fixture->repository, fixture->path), QByteArray("manual\n"));
    QVERIFY(!result.snapshot.hasConflicts);
    const QString afterSha = QString::fromLatin1(QCryptographicHash::hash(
        readFile(fixture->repository, fixture->path), QCryptographicHash::Sha256).toHex());
    QCOMPARE(unmergedIndexEntryCount(fixture->repository, fixture->path), 0);
    qInfo().noquote() << QStringLiteral("INTEGRITY manual_apply path=%1 before_sha256=%2 after_sha256=%3 expected_sha256=%4 unmerged_before=3 unmerged_after=0")
                            .arg(fixture->path, beforeSha, afterSha,
                                 QString::fromLatin1(QCryptographicHash::hash(
                                     QByteArray("manual\n"), QCryptographicHash::Sha256).toHex()));
}

void TestConflictResolutionService::oursAndTheirsPlansApply() {
    for (const auto &choice : {qMakePair(ResolutionAction::Ours, QByteArray("ours\n")),
                               qMakePair(ResolutionAction::Theirs, QByteArray("theirs\n"))}) {
        auto fixture = makeBothModified();
        QVERIFY(fixture);
        GitConflictService reader;
        const auto snapshot = scan(fixture->repository, &reader);
        GitConflictResolutionService service;
        service.setWorkingDirectory(fixture->repository);
        auto plan = planFor(fixture->repository, snapshot.files.first(), service.repositoryGeneration(),
                            QString::fromUtf8(choice.second), choice.first);
        QVERIFY(apply(&service, plan).success());
        QCOMPARE(readFile(fixture->repository, fixture->path), choice.second);
    }
}

void TestConflictResolutionService::bothAddedOursTheirsAndManualPlansApply() {
    for (const auto &choice : {qMakePair(ResolutionAction::Ours, QByteArray("main add\n")),
                               qMakePair(ResolutionAction::Theirs, QByteArray("other add\n")),
                               qMakePair(ResolutionAction::Manual, QByteArray("manual add\n"))}) {
        auto fixture = makeBothAdded();
        QVERIFY(fixture);
        GitConflictService reader;
        const auto snapshot = scan(fixture->repository, &reader);
        QCOMPARE(snapshot.conflictCount, 1);
        QCOMPARE(snapshot.files.first().type, ConflictType::BothAdded);
        GitConflictResolutionService service;
        service.setWorkingDirectory(fixture->repository);
        auto plan = planFor(fixture->repository, snapshot.files.first(), service.repositoryGeneration(),
                            QString::fromUtf8(choice.second), choice.first);
        QVERIFY(apply(&service, plan).success());
        QCOMPARE(readFile(fixture->repository, fixture->path), choice.second);
    }
}

void TestConflictResolutionService::deletedByUsAndThemKeepPlansApply() {
    for (const auto &choice : {qMakePair(true, ConflictType::DeletedByUs),
                               qMakePair(false, ConflictType::DeletedByThem)}) {
        auto fixture = makeDeleteModifyConflict(choice.first);
        QVERIFY(fixture);
        GitConflictService reader;
        const auto snapshot = scan(fixture->repository, &reader);
        QCOMPARE(snapshot.conflictCount, 1);
        QCOMPARE(snapshot.files.first().type, choice.second);
        GitConflictResolutionService service;
        service.setWorkingDirectory(fixture->repository);
        const QByteArray kept = choice.first ? QByteArray("keep-other\n") : QByteArray("keep-current\n");
        auto plan = planFor(fixture->repository, snapshot.files.first(), service.repositoryGeneration(),
                            QString::fromUtf8(kept), ResolutionAction::Manual);
        QVERIFY(apply(&service, plan).success());
        QCOMPARE(readFile(fixture->repository, fixture->path), kept);
    }
}

void TestConflictResolutionService::pathsWithDashSpacesAndUnicodeStageExactly() {
    for (const QString &path : {QStringLiteral("-leading.txt"), QStringLiteral("with space.txt"),
                                QStringLiteral("目录/冲突 文件.txt")}) {
        auto fixture = makeBothModified(path);
        QVERIFY2(fixture, qPrintable(path));
        GitConflictService reader;
        const auto snapshot = scan(fixture->repository, &reader);
        GitConflictResolutionService service;
        service.setWorkingDirectory(fixture->repository);
        QVERIFY(apply(&service, planFor(fixture->repository, snapshot.files.first(),
                                         service.repositoryGeneration(), QStringLiteral("safe\n"))).success());
        QCOMPARE(readFile(fixture->repository, path), QByteArray("safe\n"));
    }
}

void TestConflictResolutionService::preservesCrLfAndUtf8Bom() {
    const QByteArray bom("\xEF\xBB\xBF", 3);
    auto fixture = makeBothModified(QStringLiteral("crlf.txt"), bom + "base\r\n",
                                    bom + "ours\r\n", bom + "theirs\r\n");
    QVERIFY(fixture);
    GitConflictService reader;
    const auto snapshot = scan(fixture->repository, &reader);
    GitConflictResolutionService service;
    service.setWorkingDirectory(fixture->repository);
    // Git's conflict-marker writer may strip a BOM. Restore one in the
    // current worktree before the plan is captured to prove our writer keeps
    // an explicitly present UTF-8 BOM rather than normalizing it away.
    QVERIFY(writeFile(fixture->repository, fixture->path,
                      bom + readFile(fixture->repository, fixture->path)));
    QVERIFY(apply(&service, planFor(fixture->repository, snapshot.files.first(),
                                     service.repositoryGeneration(), QStringLiteral("chosen\nline\n"))).success());
    QCOMPARE(readFile(fixture->repository, fixture->path), bom + "chosen\r\nline\r\n");
}

void TestConflictResolutionService::externalConflictChangeIsRefused() {
    auto fixture = makeBothModified();
    QVERIFY(fixture);
    GitConflictService reader;
    const auto snapshot = scan(fixture->repository, &reader);
    GitConflictResolutionService service;
    service.setWorkingDirectory(fixture->repository);
    const auto plan = planFor(fixture->repository, snapshot.files.first(), service.repositoryGeneration(), QStringLiteral("new\n"));
    QVERIFY(git(fixture->repository, {QStringLiteral("add"), QStringLiteral("--"), fixture->path}));
    QCOMPARE(apply(&service, plan).error, ConflictApplyError::ConflictChangedExternally);
}

void TestConflictResolutionService::externalWorktreeChangeIsRefused() {
    auto fixture = makeBothModified();
    QVERIFY(fixture);
    GitConflictService reader;
    const auto snapshot = scan(fixture->repository, &reader);
    GitConflictResolutionService service;
    service.setWorkingDirectory(fixture->repository);
    const auto plan = planFor(fixture->repository, snapshot.files.first(), service.repositoryGeneration(), QStringLiteral("new\n"));
    QVERIFY(writeFile(fixture->repository, fixture->path, "external change\n"));
    QCOMPARE(apply(&service, plan).error, ConflictApplyError::ConflictChangedExternally);
    QCOMPARE(readFile(fixture->repository, fixture->path), QByteArray("external change\n"));
}

void TestConflictResolutionService::staleRepositorySessionIsRefused() {
    auto fixture = makeBothModified();
    QVERIFY(fixture);
    GitConflictService reader;
    const auto snapshot = scan(fixture->repository, &reader);
    GitConflictResolutionService service;
    service.setWorkingDirectory(fixture->repository);
    const auto plan = planFor(fixture->repository, snapshot.files.first(), service.repositoryGeneration(), QStringLiteral("new\n"));
    QTemporaryDir other;
    QVERIFY(other.isValid());
    service.setWorkingDirectory(other.path());
    QCOMPARE(apply(&service, plan).error, ConflictApplyError::StaleSession);
    QVERIFY(readFile(fixture->repository, fixture->path).contains("<<<<<<<"));
}

void TestConflictResolutionService::invalidTraversalIsRefused() {
    auto fixture = makeBothModified();
    QVERIFY(fixture);
    GitConflictResolutionService service;
    service.setWorkingDirectory(fixture->repository);
    ConflictResolutionPlan plan;
    plan.repositoryPath = fixture->repository;
    plan.filePath = QStringLiteral("../outside.txt");
    plan.ready = true;
    plan.sessionGeneration = service.repositoryGeneration();
    plan.action = ResolutionAction::Manual;
    QCOMPARE(apply(&service, plan).error, ConflictApplyError::InvalidPlan);
}

void TestConflictResolutionService::deletePlanUsesGitRm() {
    auto fixture = makeBothModified();
    QVERIFY(fixture);
    GitConflictService reader;
    const auto snapshot = scan(fixture->repository, &reader);
    GitConflictResolutionService service;
    service.setWorkingDirectory(fixture->repository);
    auto plan = planFor(fixture->repository, snapshot.files.first(), service.repositoryGeneration(), {});
    plan.deleteFile = true;
    plan.action = ResolutionAction::Delete;
    QVERIFY(apply(&service, plan).success());
    QVERIFY(!QFileInfo(QDir(fixture->repository).filePath(fixture->path)).exists());
}

void TestConflictResolutionService::binaryOursAndTheirsBlobPlansApply() {
    for (const auto &choice : {qMakePair(ResolutionAction::Ours, QByteArray("theirs\0", 7)),
                               qMakePair(ResolutionAction::Theirs, QByteArray("ours\0", 5))}) {
        auto fixture = makeBothModified(QStringLiteral("image.bin"), QByteArray("base\0", 5),
                                        QByteArray("ours\0", 5), QByteArray("theirs\0", 7));
        QVERIFY(fixture);
        GitConflictService reader;
        const auto snapshot = scan(fixture->repository, &reader);
        GitConflictResolutionService service;
        service.setWorkingDirectory(fixture->repository);
        auto plan = planFor(fixture->repository, snapshot.files.first(), service.repositoryGeneration(), {}, choice.first);
        plan.binary = true;
        QVERIFY(apply(&service, plan).success());
        QCOMPARE(readFile(fixture->repository, fixture->path), choice.second);
    }
}

void TestConflictResolutionService::writeFailureNeverStages() {
    auto fixture = makeBothModified();
    QVERIFY(fixture);
    GitConflictService reader;
    const auto snapshot = scan(fixture->repository, &reader);
    GitConflictResolutionService service;
    service.setWorkingDirectory(fixture->repository);
    service.setFailureInjectionForTesting({true, false, false, false});
    QCOMPARE(apply(&service, planFor(fixture->repository, snapshot.files.first(), service.repositoryGeneration(), QStringLiteral("new\n"))).error,
             ConflictApplyError::WriteFailed);
    QVERIFY(readFile(fixture->repository, fixture->path).contains("<<<<<<<"));
}

void TestConflictResolutionService::stageFailureReportsWorktreeMutation() {
    auto fixture = makeBothModified();
    QVERIFY(fixture);
    GitConflictService reader;
    const auto snapshot = scan(fixture->repository, &reader);
    GitConflictResolutionService service;
    service.setWorkingDirectory(fixture->repository);
    service.setFailureInjectionForTesting({false, true, false, false});
    const auto result = apply(&service, planFor(fixture->repository, snapshot.files.first(), service.repositoryGeneration(), QStringLiteral("new\n")));
    QCOMPARE(result.error, ConflictApplyError::StageFailed);
    QVERIFY(result.workingTreeUpdated);
    QCOMPARE(readFile(fixture->repository, fixture->path), QByteArray("new\n"));
}

void TestConflictResolutionService::verifyFailureIsHonest() {
    auto fixture = makeBothModified();
    QVERIFY(fixture);
    GitConflictService reader;
    const auto snapshot = scan(fixture->repository, &reader);
    GitConflictResolutionService service;
    service.setWorkingDirectory(fixture->repository);
    service.setFailureInjectionForTesting({false, false, true, false});
    QCOMPARE(apply(&service, planFor(fixture->repository, snapshot.files.first(), service.repositoryGeneration(), QStringLiteral("new\n"))).error,
             ConflictApplyError::StageVerificationFailed);
}

void TestConflictResolutionService::applyAllReportsPartialSuccess() {
    auto fixture = makeBothModified(QStringLiteral("one.txt"));
    QVERIFY(fixture);
    // Replaying the ready plan forces a second, externally-changed validation
    // after the first staged success and exercises the non-atomic batch report.
    GitConflictService reader;
    const auto snapshot = scan(fixture->repository, &reader);
    GitConflictResolutionService service;
    service.setWorkingDirectory(fixture->repository);
    auto first = planFor(fixture->repository, snapshot.files.first(), service.repositoryGeneration(), QStringLiteral("one\n"));
    auto second = first;
    ConflictApplyResult result;
    bool done = false;
    service.applyAllReadyAsync({first, second}, [&](ConflictApplyResult value) { result = std::move(value); done = true; });
    QVERIFY(waitUntil([&] { return done; }));
    QCOMPARE(result.error, ConflictApplyError::PartialSuccess);
    QCOMPARE(result.successFiles, QStringList{QStringLiteral("one.txt")});
}

void TestConflictResolutionService::repositorySwitchBetweenWriteAndStageStopsAtFence() {
    auto fixture = makeBothModified();
    QVERIFY(fixture);
    QTemporaryDir repositoryB;
    QVERIFY(repositoryB.isValid());
    GitConflictService reader;
    const auto snapshot = scan(fixture->repository, &reader);
    GitConflictResolutionService service;
    service.setWorkingDirectory(fixture->repository);
    service.setBeforeStageHookForTesting([&service, &repositoryB] {
        service.setWorkingDirectory(repositoryB.path());
    });
    const auto result = apply(&service, planFor(fixture->repository, snapshot.files.first(),
                                                 service.repositoryGeneration(), QStringLiteral("written-in-a\n")));
    QCOMPARE(result.error, ConflictApplyError::StaleSession);
    QCOMPARE(readFile(fixture->repository, fixture->path), QByteArray("written-in-a\n"));
    QVERIFY(!QFileInfo(QDir(repositoryB.path()).filePath(fixture->path)).exists());
    QVERIFY(git(fixture->repository, {QStringLiteral("ls-files"), QStringLiteral("-u"),
                                      QStringLiteral("--"), fixture->path}));
    qInfo().noquote() << QStringLiteral("CROSS_REPO_FENCE after_write_before_stage=1 repo_a=%1 repo_b=%2")
                            .arg(fixture->repository, repositoryB.path());
}

QTEST_MAIN(TestConflictResolutionService)
#include "test_conflict_resolution_service.moc"
