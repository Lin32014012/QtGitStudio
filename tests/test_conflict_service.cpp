#include <QtTest>

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>

#include "GitCommandExecutor.h"
#include "GitConflictParser.h"
#include "GitConflictService.h"

namespace {

struct ProcessResult {
    bool started = false;
    bool finished = false;
    int exitCode = -1;
    QByteArray output;
};

ProcessResult runGit(const QString &repository, const QStringList &arguments) {
    QProcess process;
    process.setWorkingDirectory(repository);
    process.start(QStringLiteral("git"), arguments);
    ProcessResult result;
    result.started = process.waitForStarted(15000);
    result.finished = result.started && process.waitForFinished(15000);
    result.exitCode = process.exitCode();
    result.output = process.readAllStandardOutput();
    return result;
}

bool gitSucceeds(const QString &repository, const QStringList &arguments) {
    const ProcessResult result = runGit(repository, arguments);
    return result.started && result.finished && result.exitCode == 0;
}

bool writeFile(const QString &repository, const QString &path, const QByteArray &data) {
    const QString absolutePath = QDir(repository).filePath(path);
    if (!QDir().mkpath(QFileInfo(absolutePath).dir().absolutePath())) {
        return false;
    }
    QFile file(absolutePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    return file.write(data) == data.size();
}

QByteArray readFile(const QString &repository, const QString &path) {
    QFile file(QDir(repository).filePath(path));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

bool commitAll(const QString &repository, const QString &message) {
    return gitSucceeds(repository, {QStringLiteral("add"), QStringLiteral("-A")})
        && gitSucceeds(repository,
                       {QStringLiteral("commit"), QStringLiteral("-q"),
                        QStringLiteral("-m"), message});
}

const ConflictFile *conflictForPath(const ConflictSnapshot &snapshot,
                                    const QString &path) {
    for (const ConflictFile &file : snapshot.files) {
        if (file.path == path) {
            return &file;
        }
    }
    return nullptr;
}

bool waitForScan(GitConflictService &service, ConflictScanResult *out,
                 int timeoutMs = 15000) {
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool completed = false;
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    service.scanConflictsAsync([&](ConflictScanResult result) {
        *out = std::move(result);
        completed = true;
        loop.quit();
    });
    timeout.start(timeoutMs);
    loop.exec();
    return completed;
}

bool waitForContent(GitConflictService &service, const ConflictFile &file,
                    ConflictFileContentResult *out, int timeoutMs = 15000) {
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool completed = false;
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    service.loadConflictContentAsync(file, [&](ConflictFileContentResult result) {
        *out = std::move(result);
        completed = true;
        loop.quit();
    });
    timeout.start(timeoutMs);
    loop.exec();
    return completed;
}

GitCommandExecutor::Result successfulResult(const QByteArray &output = {}) {
    GitCommandExecutor::Result result;
    result.output = output;
    result.exitCode = 0;
    result.exitStatus = QProcess::NormalExit;
    return result;
}

class DelayedConflictExecutor final : public GitCommandExecutor {
public:
    using GitCommandExecutor::GitCommandExecutor;
    QString repositoryA;
    Completion pendingA;
    QStringList commands;
    QStringList repositories;

    void executeAsync(const QString &command, const QStringList &arguments,
                      Completion completion, int timeoutMs) override {
        Q_UNUSED(arguments);
        Q_UNUSED(timeoutMs);
        commands.append(command);
        repositories.append(workingDirectory());
        if (workingDirectory() == repositoryA && !pendingA) {
            pendingA = std::move(completion);
            return;
        }
        completion(successfulResult());
    }

    void releaseA() {
        Q_ASSERT(pendingA);
        Completion completion = std::move(pendingA);
        pendingA = {};
        completion(successfulResult());
    }
};

class RecordingConflictExecutor final : public GitCommandExecutor {
public:
    using GitCommandExecutor::GitCommandExecutor;
    QStringList commands;

    void executeAsync(const QString &command, const QStringList &arguments,
                      Completion completion, int timeoutMs) override {
        Q_UNUSED(timeoutMs);
        commands.append(command);
        if (command == QStringLiteral("status")) {
            completion(successfulResult(
                "u UU N... 100644 100644 100644 "
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa "
                "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb "
                "cccccccccccccccccccccccccccccccccccccccc file.txt\0"));
            return;
        }
        if (command == QStringLiteral("ls-files")) {
            completion(successfulResult(
                "100644 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa 1\tfile.txt\0"
                "100644 bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb 2\tfile.txt\0"
                "100644 cccccccccccccccccccccccccccccccccccccccc 3\tfile.txt\0"));
            return;
        }
        if (command == QStringLiteral("rev-parse")) {
            completion(successfulResult("C:/fake/MERGE_HEAD\nC:/fake/rebase-merge\n"
                                       "C:/fake/rebase-apply\nC:/fake/CHERRY_PICK_HEAD\n"
                                       "C:/fake/REVERT_HEAD\n"));
            return;
        }
        if (command == QStringLiteral("cat-file")) {
            QVERIFY(arguments.contains(QStringLiteral("-s")));
            completion(successfulResult("5\n"));
            return;
        }
        if (command == QStringLiteral("show")) {
            completion(successfulResult("text\n"));
            return;
        }
        GitCommandExecutor::Result failure;
        failure.exitCode = 1;
        failure.exitStatus = QProcess::NormalExit;
        failure.error.type = GitErrorType::ProcessFailed;
        failure.error.message = QStringLiteral("unexpected command");
        completion(failure);
    }
};

} // namespace

class TestConflictService : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_temporaryDirectory;

    QString createRepository(const QString &name) {
        const QString path = QDir(m_temporaryDirectory.path()).filePath(name);
        if (!QDir().mkpath(path)
            || !gitSucceeds(path, {QStringLiteral("init"), QStringLiteral("-q"),
                                   QStringLiteral("-b"), QStringLiteral("main")})
            || !gitSucceeds(path, {QStringLiteral("config"), QStringLiteral("user.name"),
                                   QStringLiteral("Conflict Tester")})
            || !gitSucceeds(path, {QStringLiteral("config"), QStringLiteral("user.email"),
                                   QStringLiteral("conflict@test.invalid")})) {
            return {};
        }
        return path;
    }

    QString createBothModifiedConflict(const QString &name, const QString &path,
                                       const QByteArray &base, const QByteArray &ours,
                                       const QByteArray &theirs) {
        const QString repository = createRepository(name);
        if (repository.isEmpty()
            || !writeFile(repository, path, base)
            || !commitAll(repository, QStringLiteral("base"))
            || !gitSucceeds(repository,
                            {QStringLiteral("checkout"), QStringLiteral("-q"),
                             QStringLiteral("-b"), QStringLiteral("feature")})
            || !writeFile(repository, path, theirs)
            || !commitAll(repository, QStringLiteral("theirs"))
            || !gitSucceeds(repository,
                            {QStringLiteral("checkout"), QStringLiteral("-q"),
                             QStringLiteral("main")})
            || !writeFile(repository, path, ours)
            || !commitAll(repository, QStringLiteral("ours"))) {
            return {};
        }
        const ProcessResult merge = runGit(repository,
                                           {QStringLiteral("merge"), QStringLiteral("feature")});
        if (!merge.started || !merge.finished || merge.exitCode == 0) {
            return {};
        }
        return repository;
    }

private slots:
    void initTestCase() {
        QVERIFY(m_temporaryDirectory.isValid());
        qputenv("GIT_CEILING_DIRECTORIES", QDir::tempPath().toUtf8());
    }

    void porcelainCodesMapEveryRequiredUnmergedState() {
        QCOMPARE(GitConflictParser::conflictTypeForPorcelainCode("UU"),
                 ConflictType::BothModified);
        QCOMPARE(GitConflictParser::conflictTypeForPorcelainCode("AA"),
                 ConflictType::BothAdded);
        QCOMPARE(GitConflictParser::conflictTypeForPorcelainCode("DD"),
                 ConflictType::BothDeleted);
        QCOMPARE(GitConflictParser::conflictTypeForPorcelainCode("AU"),
                 ConflictType::AddedByUs);
        QCOMPARE(GitConflictParser::conflictTypeForPorcelainCode("UA"),
                 ConflictType::AddedByThem);
        QCOMPARE(GitConflictParser::conflictTypeForPorcelainCode("DU"),
                 ConflictType::DeletedByUs);
        QCOMPARE(GitConflictParser::conflictTypeForPorcelainCode("UD"),
                 ConflictType::DeletedByThem);
    }

    void bothModifiedMapsStagesAndReadsContentWithoutMutation() {
        const QString repository = createBothModifiedConflict(
            QStringLiteral("both-modified"), QStringLiteral("file.txt"),
            "base\n", "ours\n", "theirs\n");
        QVERIFY(!repository.isEmpty());
        const QByteArray worktreeBefore = readFile(repository, QStringLiteral("file.txt"));
        const QByteArray indexBefore = runGit(repository,
                                              {QStringLiteral("ls-files"), QStringLiteral("-u"),
                                               QStringLiteral("-z")}).output;
        GitConflictService service;
        service.setWorkingDirectory(repository);
        ConflictScanResult scan;
        QVERIFY(waitForScan(service, &scan));
        QVERIFY2(!scan.error.hasError(), qPrintable(scan.error.message));
        QVERIFY(scan.snapshot.hasConflicts);
        QCOMPARE(scan.snapshot.operationType, ConflictOperationType::Merge);
        QCOMPARE(scan.snapshot.conflictCount, 1);
        const ConflictFile *file = conflictForPath(scan.snapshot, QStringLiteral("file.txt"));
        QVERIFY(file);
        QCOMPARE(file->type, ConflictType::BothModified);
        QVERIFY(file->hasBase);
        QVERIFY(file->hasOurs);
        QVERIFY(file->hasTheirs);
        ConflictFileContentResult content;
        QVERIFY(waitForContent(service, *file, &content));
        QVERIFY2(!content.error.hasError(), qPrintable(content.error.message));
        QCOMPARE(content.content.baseContent, QByteArray("base\n"));
        QCOMPARE(content.content.oursContent, QByteArray("ours\n"));
        QCOMPARE(content.content.theirsContent, QByteArray("theirs\n"));
        QVERIFY(!content.content.isBinary);
        QVERIFY(!content.content.contentTruncated);
        QCOMPARE(readFile(repository, QStringLiteral("file.txt")), worktreeBefore);
        QCOMPARE(runGit(repository,
                        {QStringLiteral("ls-files"), QStringLiteral("-u"),
                         QStringLiteral("-z")}).output,
                 indexBefore);
    }

    void bothAddedAndDeleteStatesAllowMissingStages() {
        const QString added = createRepository(QStringLiteral("both-added"));
        QVERIFY(!added.isEmpty());
        QVERIFY(writeFile(added, QStringLiteral("base.txt"), "base\n"));
        QVERIFY(commitAll(added, QStringLiteral("base")));
        QVERIFY(gitSucceeds(added, {QStringLiteral("checkout"), QStringLiteral("-q"),
                                    QStringLiteral("-b"), QStringLiteral("feature")}));
        QVERIFY(writeFile(added, QStringLiteral("added.txt"), "theirs\n"));
        QVERIFY(commitAll(added, QStringLiteral("theirs")));
        QVERIFY(gitSucceeds(added, {QStringLiteral("checkout"), QStringLiteral("-q"),
                                    QStringLiteral("main")}));
        QVERIFY(writeFile(added, QStringLiteral("added.txt"), "ours\n"));
        QVERIFY(commitAll(added, QStringLiteral("ours")));
        QVERIFY(runGit(added, {QStringLiteral("merge"), QStringLiteral("feature")}).exitCode != 0);
        GitConflictService service;
        service.setWorkingDirectory(added);
        ConflictScanResult scan;
        QVERIFY(waitForScan(service, &scan));
        const ConflictFile *bothAdded = conflictForPath(scan.snapshot, QStringLiteral("added.txt"));
        QVERIFY(bothAdded);
        QCOMPARE(bothAdded->type, ConflictType::BothAdded);
        QVERIFY(!bothAdded->hasBase);
        QVERIFY(bothAdded->hasOurs);
        QVERIFY(bothAdded->hasTheirs);

        const QString deletedByUs = createRepository(QStringLiteral("deleted-by-us"));
        QVERIFY(!deletedByUs.isEmpty());
        QVERIFY(writeFile(deletedByUs, QStringLiteral("delete.txt"), "base\n"));
        QVERIFY(commitAll(deletedByUs, QStringLiteral("base")));
        QVERIFY(gitSucceeds(deletedByUs, {QStringLiteral("checkout"), QStringLiteral("-q"),
                                          QStringLiteral("-b"), QStringLiteral("feature")}));
        QVERIFY(writeFile(deletedByUs, QStringLiteral("delete.txt"), "theirs\n"));
        QVERIFY(commitAll(deletedByUs, QStringLiteral("theirs")));
        QVERIFY(gitSucceeds(deletedByUs, {QStringLiteral("checkout"), QStringLiteral("-q"),
                                          QStringLiteral("main")}));
        QVERIFY(QFile::remove(QDir(deletedByUs).filePath(QStringLiteral("delete.txt"))));
        QVERIFY(commitAll(deletedByUs, QStringLiteral("ours deletes")));
        QVERIFY(runGit(deletedByUs, {QStringLiteral("merge"), QStringLiteral("feature")}).exitCode != 0);
        service.setWorkingDirectory(deletedByUs);
        QVERIFY(waitForScan(service, &scan));
        const ConflictFile *du = conflictForPath(scan.snapshot, QStringLiteral("delete.txt"));
        QVERIFY(du);
        QCOMPARE(du->type, ConflictType::DeletedByUs);
        QVERIFY(du->hasBase);
        QVERIFY(!du->hasOurs);
        QVERIFY(du->hasTheirs);

        const QString deletedByThem = createRepository(QStringLiteral("deleted-by-them"));
        QVERIFY(!deletedByThem.isEmpty());
        QVERIFY(writeFile(deletedByThem, QStringLiteral("delete.txt"), "base\n"));
        QVERIFY(commitAll(deletedByThem, QStringLiteral("base")));
        QVERIFY(gitSucceeds(deletedByThem, {QStringLiteral("checkout"), QStringLiteral("-q"),
                                            QStringLiteral("-b"), QStringLiteral("feature")}));
        QVERIFY(QFile::remove(QDir(deletedByThem).filePath(QStringLiteral("delete.txt"))));
        QVERIFY(commitAll(deletedByThem, QStringLiteral("theirs deletes")));
        QVERIFY(gitSucceeds(deletedByThem, {QStringLiteral("checkout"), QStringLiteral("-q"),
                                            QStringLiteral("main")}));
        QVERIFY(writeFile(deletedByThem, QStringLiteral("delete.txt"), "ours\n"));
        QVERIFY(commitAll(deletedByThem, QStringLiteral("ours")));
        QVERIFY(runGit(deletedByThem, {QStringLiteral("merge"), QStringLiteral("feature")}).exitCode != 0);
        service.setWorkingDirectory(deletedByThem);
        QVERIFY(waitForScan(service, &scan));
        const ConflictFile *ud = conflictForPath(scan.snapshot, QStringLiteral("delete.txt"));
        QVERIFY(ud);
        QCOMPARE(ud->type, ConflictType::DeletedByThem);
        QVERIFY(ud->hasBase);
        QVERIFY(ud->hasOurs);
        QVERIFY(!ud->hasTheirs);
    }

    void unicodeSpaceAndMultipleConflictsUseNulDelimitedPaths() {
        const QString repository = createRepository(QStringLiteral("encoded-paths"));
        QVERIFY(!repository.isEmpty());
        const QStringList paths = {QStringLiteral("测试 文件.txt"),
                                   QStringLiteral("with space.txt"),
                                   QStringLiteral("plain.txt")};
        for (const QString &path : paths) QVERIFY(writeFile(repository, path, "base\n"));
        QVERIFY(commitAll(repository, QStringLiteral("base")));
        QVERIFY(gitSucceeds(repository, {QStringLiteral("checkout"), QStringLiteral("-q"),
                                         QStringLiteral("-b"), QStringLiteral("feature")}));
        for (const QString &path : paths) QVERIFY(writeFile(repository, path, "theirs\n"));
        QVERIFY(commitAll(repository, QStringLiteral("theirs")));
        QVERIFY(gitSucceeds(repository, {QStringLiteral("checkout"), QStringLiteral("-q"),
                                         QStringLiteral("main")}));
        for (const QString &path : paths) QVERIFY(writeFile(repository, path, "ours\n"));
        QVERIFY(commitAll(repository, QStringLiteral("ours")));
        QVERIFY(runGit(repository, {QStringLiteral("merge"), QStringLiteral("feature")}).exitCode != 0);
        GitConflictService service;
        service.setWorkingDirectory(repository);
        ConflictScanResult scan;
        QVERIFY(waitForScan(service, &scan));
        QCOMPARE(scan.snapshot.conflictCount, 3);
        for (const QString &path : paths) {
            const ConflictFile *file = conflictForPath(scan.snapshot, path);
            QVERIFY2(file, qPrintable(path));
            QCOMPARE(file->type, ConflictType::BothModified);
        }
    }

    void binaryAndLargePreviewAreReportedWithoutTextAssumptions() {
        const QByteArray base("\0base", 5);
        const QByteArray ours("\0ours", 5);
        const QByteArray theirs("\0theirs", 7);
        const QString binaryRepo = createBothModifiedConflict(
            QStringLiteral("binary"), QStringLiteral("image.bin"), base, ours, theirs);
        QVERIFY(!binaryRepo.isEmpty());
        GitConflictService service;
        service.setWorkingDirectory(binaryRepo);
        ConflictScanResult scan;
        QVERIFY(waitForScan(service, &scan));
        const ConflictFile *binary = conflictForPath(scan.snapshot, QStringLiteral("image.bin"));
        QVERIFY(binary);
        ConflictFileContentResult content;
        QVERIFY(waitForContent(service, *binary, &content));
        QVERIFY(content.content.isBinary);
        QCOMPARE(content.content.baseContent, base);
        QCOMPARE(content.content.oursContent, ours);
        QCOMPARE(content.content.theirsContent, theirs);

        const QString largeRepo = createBothModifiedConflict(
            QStringLiteral("large"), QStringLiteral("large.txt"), QByteArray(256, 'b'),
            QByteArray(256, 'o'), QByteArray(256, 't'));
        QVERIFY(!largeRepo.isEmpty());
        service.setPreviewLimit(32);
        service.setWorkingDirectory(largeRepo);
        QVERIFY(waitForScan(service, &scan));
        const ConflictFile *largeFile = conflictForPath(scan.snapshot, QStringLiteral("large.txt"));
        QVERIFY(largeFile);
        QVERIFY(waitForContent(service, *largeFile, &content));
        QVERIFY(content.content.contentTruncated);
        QVERIFY(content.content.baseContent.isEmpty());
        QVERIFY(content.content.oursContent.isEmpty());
        QVERIFY(content.content.theirsContent.isEmpty());
    }

    void noConflictAndCommandFailureAreDistinct() {
        const QString repository = createRepository(QStringLiteral("clean"));
        QVERIFY(!repository.isEmpty());
        QVERIFY(writeFile(repository, QStringLiteral("clean.txt"), "clean\n"));
        QVERIFY(commitAll(repository, QStringLiteral("clean")));
        GitConflictService service;
        service.setWorkingDirectory(repository);
        ConflictScanResult scan;
        QVERIFY(waitForScan(service, &scan));
        QVERIFY(!scan.error.hasError());
        QVERIFY(!scan.snapshot.hasConflicts);
        QCOMPARE(scan.snapshot.conflictCount, 0);
        service.setWorkingDirectory(QDir(m_temporaryDirectory.path()).filePath(QStringLiteral("missing")));
        QVERIFY(waitForScan(service, &scan));
        QVERIFY(scan.error.hasError());
        QVERIFY(!scan.snapshot.hasConflicts);
    }

    void worktreeConflictUsesGitPathOperationLookup() {
        const QString repository = createRepository(QStringLiteral("worktree-primary"));
        QVERIFY(!repository.isEmpty());
        QVERIFY(writeFile(repository, QStringLiteral("worktree.txt"), "base\n"));
        QVERIFY(commitAll(repository, QStringLiteral("base")));
        const QString worktree = QDir(m_temporaryDirectory.path()).filePath(QStringLiteral("worktree-linked"));
        QVERIFY(gitSucceeds(repository,
                            {QStringLiteral("worktree"), QStringLiteral("add"),
                             QStringLiteral("-q"), QStringLiteral("-b"),
                             QStringLiteral("feature"), worktree}));
        QVERIFY(QFileInfo(QDir(worktree).filePath(QStringLiteral(".git"))).isFile());
        QVERIFY(writeFile(worktree, QStringLiteral("worktree.txt"), "theirs\n"));
        QVERIFY(commitAll(worktree, QStringLiteral("theirs")));
        QVERIFY(writeFile(repository, QStringLiteral("worktree.txt"), "ours\n"));
        QVERIFY(commitAll(repository, QStringLiteral("ours")));
        QVERIFY(runGit(worktree, {QStringLiteral("merge"), QStringLiteral("main")}).exitCode != 0);
        GitConflictService service;
        service.setWorkingDirectory(worktree);
        ConflictScanResult scan;
        QVERIFY(waitForScan(service, &scan));
        QVERIFY(scan.snapshot.hasConflicts);
        QCOMPARE(scan.snapshot.operationType, ConflictOperationType::Merge);
        QVERIFY(conflictForPath(scan.snapshot, QStringLiteral("worktree.txt")));
    }

    void staleRepositoryScanCannotReturnIntoNewSession() {
        DelayedConflictExecutor executor;
        executor.repositoryA = QStringLiteral("A");
        GitConflictService service(&executor);
        ConflictScanResult fromA;
        ConflictScanResult fromB;
        bool receivedA = false;
        bool receivedB = false;
        service.setWorkingDirectory(QStringLiteral("A"));
        service.scanConflictsAsync([&](ConflictScanResult result) {
            fromA = std::move(result);
            receivedA = true;
        });
        service.setWorkingDirectory(QStringLiteral("B"));
        service.scanConflictsAsync([&](ConflictScanResult result) {
            fromB = std::move(result);
            receivedB = true;
        });
        QVERIFY(receivedB);
        executor.releaseA();
        QVERIFY(receivedA);
        QVERIFY(fromA.stale);
        QVERIFY(fromA.snapshot.files.isEmpty());
        QVERIFY(!fromB.stale);
        QCOMPARE(fromB.snapshot.repositoryPath, QStringLiteral("B"));
        QCOMPARE(executor.repositories.count(QStringLiteral("A")), 1);
        QCOMPARE(executor.commands.count(QStringLiteral("status")), 2);
        QCOMPARE(executor.commands.count(QStringLiteral("ls-files")), 1);
    }

    void serviceInvokesOnlyReadOnlyGitCommands() {
        RecordingConflictExecutor executor;
        GitConflictService service(&executor);
        service.setWorkingDirectory(QStringLiteral("recording"));
        ConflictScanResult scan;
        QVERIFY(waitForScan(service, &scan));
        QVERIFY(!scan.error.hasError());
        QCOMPARE(scan.snapshot.conflictCount, 1);
        ConflictFileContentResult content;
        QVERIFY(waitForContent(service, scan.snapshot.files.first(), &content));
        QVERIFY(!content.error.hasError());
        const QSet<QString> allowed = {QStringLiteral("status"),
                                       QStringLiteral("ls-files"),
                                       QStringLiteral("rev-parse"),
                                       QStringLiteral("cat-file"),
                                       QStringLiteral("show")};
        const QSet<QString> forbidden = {QStringLiteral("add"), QStringLiteral("checkout"),
                                         QStringLiteral("restore"), QStringLiteral("reset"),
                                         QStringLiteral("commit"), QStringLiteral("merge"),
                                         QStringLiteral("rebase"), QStringLiteral("rm")};
        QVERIFY(!executor.commands.isEmpty());
        for (const QString &command : executor.commands) {
            QVERIFY2(allowed.contains(command), qPrintable(command));
            QVERIFY(!forbidden.contains(command));
        }
    }
};

QTEST_MAIN(TestConflictService)
#include "test_conflict_service.moc"
