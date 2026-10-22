#include <QtTest>

#include <QCoreApplication>
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDialog>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>

#include "ConflictResolverWidget.h"
#include "GitCommandExecutor.h"
#include "GitConflictService.h"
#include "GitConflictResolutionService.h"
#include "MainWindow.h"

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

const ConflictFile *fileForPath(const ConflictSnapshot &snapshot, const QString &path) {
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

bool waitUntil(const std::function<bool()> &predicate, int timeoutMs = 15000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QTest::qWait(10);
    }
    return predicate();
}

GitCommandExecutor::Result success(const QByteArray &output = {}) {
    GitCommandExecutor::Result result;
    result.output = output;
    result.exitCode = 0;
    result.exitStatus = QProcess::NormalExit;
    return result;
}

class RecordingExecutor final : public GitCommandExecutor {
public:
    using GitCommandExecutor::GitCommandExecutor;
    QStringList commands;

    void executeAsync(const QString &command, const QStringList &arguments,
                      Completion completion, int timeoutMs) override {
        Q_UNUSED(timeoutMs);
        commands.append(command);
        if (command == QStringLiteral("cat-file")) {
            QVERIFY(arguments.contains(QStringLiteral("-s")));
            completion(success("5\n"));
            return;
        }
        if (command == QStringLiteral("show")) {
            completion(success("text\n"));
            return;
        }
        QFAIL("Unexpected Git command in resolver test");
    }
};

class DelayedContentExecutor final : public GitCommandExecutor {
public:
    using GitCommandExecutor::GitCommandExecutor;
    Completion pending;

    void executeAsync(const QString &command, const QStringList &arguments,
                      Completion completion, int timeoutMs) override {
        Q_UNUSED(arguments);
        Q_UNUSED(timeoutMs);
        if (command == QStringLiteral("cat-file") && !pending) {
            pending = std::move(completion);
            return;
        }
        completion(success(command == QStringLiteral("cat-file") ? "5\n" : "text\n"));
    }

    void release() {
        QVERIFY(pending);
        Completion completion = std::move(pending);
        pending = {};
        completion(success("5\n"));
    }
};

} // namespace

class TestConflictResolver : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_temporaryDirectory;

    QString createRepository(const QString &name) {
        const QString repository = QDir(m_temporaryDirectory.path()).filePath(name);
        if (!QDir().mkpath(repository)
            || !gitSucceeds(repository, {QStringLiteral("init"), QStringLiteral("-q"),
                                         QStringLiteral("-b"), QStringLiteral("main")})
            || !gitSucceeds(repository, {QStringLiteral("config"), QStringLiteral("user.name"),
                                         QStringLiteral("Resolver Tester")})
            || !gitSucceeds(repository, {QStringLiteral("config"), QStringLiteral("user.email"),
                                         QStringLiteral("resolver@test.invalid")})
            || !gitSucceeds(repository, {QStringLiteral("config"), QStringLiteral("core.autocrlf"),
                                         QStringLiteral("false")})) {
            return {};
        }
        return repository;
    }

    QString createBothModified(const QString &name, const QString &path,
                               const QByteArray &base, const QByteArray &ours,
                               const QByteArray &theirs, bool diff3 = false) {
        const QString repository = createRepository(name);
        if (repository.isEmpty() || !writeFile(repository, path, base)
            || !commitAll(repository, QStringLiteral("base"))
            || !gitSucceeds(repository, {QStringLiteral("checkout"), QStringLiteral("-q"),
                                         QStringLiteral("-b"), QStringLiteral("feature")})
            || !writeFile(repository, path, theirs)
            || !commitAll(repository, QStringLiteral("theirs"))
            || !gitSucceeds(repository, {QStringLiteral("checkout"), QStringLiteral("-q"),
                                         QStringLiteral("main")})
            || !writeFile(repository, path, ours)
            || !commitAll(repository, QStringLiteral("ours"))) {
            return {};
        }
        if (diff3 && !gitSucceeds(repository,
                                  {QStringLiteral("config"), QStringLiteral("merge.conflictStyle"),
                                   QStringLiteral("diff3")})) {
            return {};
        }
        const ProcessResult merge = runGit(repository, {QStringLiteral("merge"), QStringLiteral("feature")});
        return merge.started && merge.finished && merge.exitCode != 0 ? repository : QString{};
    }

    QString createTwoFileConflict(const QString &name) {
        const QString repository = createRepository(name);
        if (repository.isEmpty()
            || !writeFile(repository, QStringLiteral("first.txt"), "base first\n")
            || !writeFile(repository, QStringLiteral("second.txt"), "base second\n")
            || !commitAll(repository, QStringLiteral("base"))
            || !gitSucceeds(repository, {QStringLiteral("checkout"), QStringLiteral("-q"),
                                         QStringLiteral("-b"), QStringLiteral("feature")})
            || !writeFile(repository, QStringLiteral("first.txt"), "theirs first\n")
            || !writeFile(repository, QStringLiteral("second.txt"), "theirs second\n")
            || !commitAll(repository, QStringLiteral("theirs"))
            || !gitSucceeds(repository, {QStringLiteral("checkout"), QStringLiteral("-q"),
                                         QStringLiteral("main")})
            || !writeFile(repository, QStringLiteral("first.txt"), "ours first\n")
            || !writeFile(repository, QStringLiteral("second.txt"), "ours second\n")
            || !commitAll(repository, QStringLiteral("ours"))) {
            return {};
        }
        const ProcessResult merge = runGit(repository, {QStringLiteral("merge"), QStringLiteral("feature")});
        return merge.started && merge.finished && merge.exitCode != 0 ? repository : QString{};
    }

    QString createBothAdded(const QString &name) {
        const QString repository = createRepository(name);
        if (repository.isEmpty() || !writeFile(repository, QStringLiteral("README"), "base\n")
            || !commitAll(repository, QStringLiteral("base"))
            || !gitSucceeds(repository, {QStringLiteral("checkout"), QStringLiteral("-q"),
                                         QStringLiteral("-b"), QStringLiteral("feature")})
            || !writeFile(repository, QStringLiteral("added.txt"), "theirs\n")
            || !commitAll(repository, QStringLiteral("theirs"))
            || !gitSucceeds(repository, {QStringLiteral("checkout"), QStringLiteral("-q"),
                                         QStringLiteral("main")})
            || !writeFile(repository, QStringLiteral("added.txt"), "ours\n")
            || !commitAll(repository, QStringLiteral("ours"))) {
            return {};
        }
        const ProcessResult merge = runGit(repository, {QStringLiteral("merge"), QStringLiteral("feature")});
        return merge.started && merge.finished && merge.exitCode != 0 ? repository : QString{};
    }

    QString createDeletionConflict(const QString &name, bool oursDeleted) {
        const QString repository = createRepository(name);
        if (repository.isEmpty() || !writeFile(repository, QStringLiteral("delete.txt"), "base\n")
            || !commitAll(repository, QStringLiteral("base"))
            || !gitSucceeds(repository, {QStringLiteral("checkout"), QStringLiteral("-q"),
                                         QStringLiteral("-b"), QStringLiteral("feature")})
            || (oursDeleted ? !writeFile(repository, QStringLiteral("delete.txt"), "theirs changed\n")
                            : !QFile::remove(QDir(repository).filePath(QStringLiteral("delete.txt"))))
            || !commitAll(repository, QStringLiteral("feature change"))
            || !gitSucceeds(repository, {QStringLiteral("checkout"), QStringLiteral("-q"),
                                         QStringLiteral("main")})
            || (oursDeleted ? !QFile::remove(QDir(repository).filePath(QStringLiteral("delete.txt")))
                            : !writeFile(repository, QStringLiteral("delete.txt"), "ours changed\n"))
            || !commitAll(repository, QStringLiteral("main change"))) {
            return {};
        }
        const ProcessResult merge = runGit(repository, {QStringLiteral("merge"), QStringLiteral("feature")});
        return merge.started && merge.finished && merge.exitCode != 0 ? repository : QString{};
    }

    ConflictSnapshot scan(const QString &repository, GitConflictService *service) {
        service->setWorkingDirectory(repository);
        ConflictScanResult result;
        if (!waitForScan(*service, &result)) {
            QTest::qFail("conflict scan did not complete", __FILE__, __LINE__);
            return {};
        }
        if (result.error.hasError()) {
            QTest::qFail(qPrintable(result.error.message), __FILE__, __LINE__);
            return {};
        }
        if (!result.snapshot.hasConflicts) {
            QTest::qFail("expected a conflict snapshot", __FILE__, __LINE__);
            return {};
        }
        return result.snapshot;
    }

    void prepare(ConflictResolverWidget *resolver, const ConflictSnapshot &snapshot) {
        resolver->resize(1100, 720);
        resolver->show();
        resolver->setSnapshot(snapshot);
        QVERIFY(waitUntil([resolver]() {
            auto *status = resolver->findChild<QLabel *>(QStringLiteral("conflictResolverStatus"));
            return status && !status->text().contains(QStringLiteral("正在按需加载"));
        }));
    }

    QPlainTextEdit *editor(ConflictResolverWidget *resolver, const QString &name) {
        auto *result = resolver->findChild<QPlainTextEdit *>(name);
        if (!result) {
            QTest::qFail(qPrintable(name), __FILE__, __LINE__);
        }
        return result;
    }

    QPushButton *button(ConflictResolverWidget *resolver, const QString &name) {
        auto *result = resolver->findChild<QPushButton *>(name);
        if (!result) {
            QTest::qFail(qPrintable(name), __FILE__, __LINE__);
        }
        return result;
    }

private slots:
    void uuAcceptOurs();
    void uuAcceptTheirs();
    void acceptBoth();
    void multipleHunksTracksRemaining();
    void manualEditProducesDirtyPlan();
    void switchingFilesPreservesResult();
    void bothAddedShowsNoBaseVersion();
    void deletedByUsAndThemShowDeletedSide();
    void binaryConflictUsesSidePlansOnly();
    void truncatedConflictIsUnavailable();
    void unicodeFilenameLoads();
    void crlfConflictLoadsSafely();
    void diff3MarkersLoad();
    void invalidMarkersAreSafe();
    void staleRepositoryCallbackIsIgnored();
    void rapidFileSwitchingKeepsCurrentFile();
    void filesystemRemainsUnchanged();
    void resolverInvokesNoGitWrites();
    void noConflictShowsEmptyState();
    void operationBannerReflectsSnapshot();
    void mainWindowShowsAndResetsConflictEntry();
    void applyCurrentUsesBusyStateAndRefreshesGitSnapshot();
    void applyFailureExplainsWrittenButUnstagedFile();
    void applyAllReportsPartialSuccessThroughClickedUi();
    void continueOnlyBecomesAvailableAfterVerifiedResolution();
    void abortRequiresVisibleConfirmation();
};

void TestConflictResolver::uuAcceptOurs() {
    const QString repo = createBothModified(QStringLiteral("ours"), QStringLiteral("file.txt"),
                                             "base\n", "ours\n", "theirs\n");
    QVERIFY(!repo.isEmpty());
    GitConflictService service;
    const ConflictSnapshot snapshot = scan(repo, &service);
    ConflictResolverWidget resolver(&service);
    prepare(&resolver, snapshot);
    QCOMPARE(editor(&resolver, QStringLiteral("conflictBaseEditor"))->toPlainText(),
             QStringLiteral("base\n"));
    QCOMPARE(editor(&resolver, QStringLiteral("conflictOursEditor"))->toPlainText(),
             QStringLiteral("ours\n"));
    QCOMPARE(editor(&resolver, QStringLiteral("conflictTheirsEditor"))->toPlainText(),
             QStringLiteral("theirs\n"));
    QTest::mouseClick(button(&resolver, QStringLiteral("acceptOursButton")), Qt::LeftButton);
    QVERIFY(waitUntil([&resolver]() { return resolver.isFileReadyForTesting(QStringLiteral("file.txt")); }));
    const auto plan = resolver.resolutionPlanForFile(QStringLiteral("file.txt"));
    QCOMPARE(plan.action, ResolutionAction::Ours);
    QCOMPARE(plan.resultContent, QStringLiteral("ours\n"));
    QVERIFY(!plan.resultContent.contains(QStringLiteral("<<<<<<<")));
}

void TestConflictResolver::uuAcceptTheirs() {
    const QString repo = createBothModified(QStringLiteral("theirs"), QStringLiteral("file.txt"),
                                             "base\n", "ours\n", "theirs\n");
    QVERIFY(!repo.isEmpty());
    GitConflictService service;
    ConflictResolverWidget resolver(&service);
    prepare(&resolver, scan(repo, &service));
    QTest::mouseClick(button(&resolver, QStringLiteral("acceptTheirsButton")), Qt::LeftButton);
    QVERIFY(waitUntil([&resolver]() { return resolver.isFileReadyForTesting(QStringLiteral("file.txt")); }));
    QCOMPARE(resolver.resolutionPlanForFile(QStringLiteral("file.txt")).resultContent,
             QStringLiteral("theirs\n"));
}

void TestConflictResolver::acceptBoth() {
    const QString repo = createBothModified(QStringLiteral("both"), QStringLiteral("file.txt"),
                                             "base\n", "ours\n", "theirs\n");
    QVERIFY(!repo.isEmpty());
    GitConflictService service;
    ConflictResolverWidget resolver(&service);
    prepare(&resolver, scan(repo, &service));
    QTest::mouseClick(button(&resolver, QStringLiteral("acceptBothButton")), Qt::LeftButton);
    QVERIFY(waitUntil([&resolver]() { return resolver.isFileReadyForTesting(QStringLiteral("file.txt")); }));
    const auto plan = resolver.resolutionPlanForFile(QStringLiteral("file.txt"));
    QCOMPARE(plan.action, ResolutionAction::Both);
    QCOMPARE(plan.resultContent, QStringLiteral("ours\ntheirs\n"));
}

void TestConflictResolver::multipleHunksTracksRemaining() {
    const QString repo = createBothModified(QStringLiteral("multi"), QStringLiteral("file.txt"),
                                             "base\n", "ours\n", "theirs\n");
    QVERIFY(!repo.isEmpty());
    const QString markers = QStringLiteral("<<<<<<< ours\none\n=======\ntwo\n>>>>>>> theirs\n"
                                           "middle\n<<<<<<< ours\nthree\n=======\nfour\n>>>>>>> theirs\n");
    QVERIFY(writeFile(repo, QStringLiteral("file.txt"), markers.toUtf8()));
    GitConflictService service;
    ConflictResolverWidget resolver(&service);
    prepare(&resolver, scan(repo, &service));
    QCOMPARE(resolver.unresolvedHunksForTesting(QStringLiteral("file.txt")), 2);
    QTest::mouseClick(button(&resolver, QStringLiteral("acceptOursButton")), Qt::LeftButton);
    QCOMPARE(resolver.unresolvedHunksForTesting(QStringLiteral("file.txt")), 1);
    QVERIFY(!resolver.isFileReadyForTesting(QStringLiteral("file.txt")));
}

void TestConflictResolver::manualEditProducesDirtyPlan() {
    const QString repo = createBothModified(QStringLiteral("manual"), QStringLiteral("file.txt"),
                                             "base\n", "ours\n", "theirs\n");
    QVERIFY(!repo.isEmpty());
    GitConflictService service;
    ConflictResolverWidget resolver(&service);
    prepare(&resolver, scan(repo, &service));
    auto *result = resolver.resultEditorForTesting();
    result->selectAll();
    result->insertPlainText(QStringLiteral("manual result\n"));
    QVERIFY(waitUntil([&resolver]() { return resolver.isFileReadyForTesting(QStringLiteral("file.txt")); }));
    const auto plan = resolver.resolutionPlanForFile(QStringLiteral("file.txt"));
    QCOMPARE(plan.action, ResolutionAction::Manual);
    QCOMPARE(plan.resultContent, QStringLiteral("manual result\n"));
}

void TestConflictResolver::switchingFilesPreservesResult() {
    const QString repo = createTwoFileConflict(QStringLiteral("switch"));
    QVERIFY(!repo.isEmpty());
    GitConflictService service;
    ConflictResolverWidget resolver(&service);
    prepare(&resolver, scan(repo, &service));
    auto *list = resolver.fileListForTesting();
    QCOMPARE(list->count(), 2);
    const QString first = list->item(0)->data(Qt::UserRole).toString();
    const QString second = list->item(1)->data(Qt::UserRole).toString();
    resolver.resultEditorForTesting()->setPlainText(QStringLiteral("first manual\n"));
    QVERIFY(waitUntil([&resolver, &first]() { return resolver.isFileReadyForTesting(first); }));
    list->setCurrentRow(1);
    QVERIFY(waitUntil([&resolver, &second]() { return resolver.currentPathForTesting() == second; }));
    list->setCurrentRow(0);
    QVERIFY(waitUntil([&resolver, &first]() { return resolver.currentPathForTesting() == first; }));
    QCOMPARE(resolver.resultEditorForTesting()->toPlainText(), QStringLiteral("first manual\n"));
}

void TestConflictResolver::bothAddedShowsNoBaseVersion() {
    const QString repo = createBothAdded(QStringLiteral("added"));
    QVERIFY(!repo.isEmpty());
    GitConflictService service;
    ConflictResolverWidget resolver(&service);
    prepare(&resolver, scan(repo, &service));
    QCOMPARE(editor(&resolver, QStringLiteral("conflictBaseEditor"))->toPlainText(),
             QStringLiteral("[No Base Version]"));
}

void TestConflictResolver::deletedByUsAndThemShowDeletedSide() {
    for (const bool oursDeleted : {true, false}) {
        const QString repo = createDeletionConflict(oursDeleted ? QStringLiteral("du") : QStringLiteral("ud"),
                                                    oursDeleted);
        QVERIFY(!repo.isEmpty());
        GitConflictService service;
        ConflictResolverWidget resolver(&service);
        prepare(&resolver, scan(repo, &service));
        const auto snapshot = resolver.snapshot();
        const ConflictFile *file = fileForPath(snapshot, QStringLiteral("delete.txt"));
        QVERIFY(file);
        if (!file->hasOurs) {
            QCOMPARE(editor(&resolver, QStringLiteral("conflictOursEditor"))->toPlainText(),
                     QStringLiteral("[Deleted]"));
        }
        if (!file->hasTheirs) {
            QCOMPARE(editor(&resolver, QStringLiteral("conflictTheirsEditor"))->toPlainText(),
                     QStringLiteral("[Deleted]"));
        }
    }
}

void TestConflictResolver::binaryConflictUsesSidePlansOnly() {
    const QString repo = createBothModified(QStringLiteral("binary"), QStringLiteral("image.bin"),
                                             QByteArray("base\0", 5), QByteArray("ours\0", 5),
                                             QByteArray("theirs\0", 7));
    QVERIFY(!repo.isEmpty());
    GitConflictService service;
    ConflictResolverWidget resolver(&service);
    prepare(&resolver, scan(repo, &service));
    QVERIFY(!resolver.resultEditorForTesting()->isEnabled());
    QVERIFY(!button(&resolver, QStringLiteral("acceptBothButton"))->isEnabled());
    QTest::mouseClick(button(&resolver, QStringLiteral("acceptOursButton")), Qt::LeftButton);
    const auto plan = resolver.resolutionPlanForFile(QStringLiteral("image.bin"));
    QVERIFY(plan.ready);
    QCOMPARE(plan.action, ResolutionAction::Ours);
}

void TestConflictResolver::truncatedConflictIsUnavailable() {
    const QString repo = createBothModified(QStringLiteral("large"), QStringLiteral("large.txt"),
                                             "base content\n", "ours content\n", "theirs content\n");
    QVERIFY(!repo.isEmpty());
    GitConflictService service;
    service.setPreviewLimit(2);
    ConflictResolverWidget resolver(&service);
    prepare(&resolver, scan(repo, &service));
    QVERIFY(!resolver.resultEditorForTesting()->isEnabled());
    auto *status = resolver.findChild<QLabel *>(QStringLiteral("conflictResolverStatus"));
    QVERIFY(status->text().contains(QStringLiteral("文件过大")));
    QVERIFY(!resolver.resolutionPlanForFile(QStringLiteral("large.txt")).ready);
}

void TestConflictResolver::unicodeFilenameLoads() {
    const QString path = QStringLiteral("目录/冲突 文件.txt");
    const QString repo = createBothModified(QStringLiteral("unicode"), path,
                                             "base\n", "ours\n", "theirs\n");
    QVERIFY(!repo.isEmpty());
    GitConflictService service;
    ConflictResolverWidget resolver(&service);
    prepare(&resolver, scan(repo, &service));
    QCOMPARE(resolver.currentPathForTesting(), path);
    QVERIFY(editor(&resolver, QStringLiteral("conflictOursEditor"))->toPlainText().contains(QStringLiteral("ours")));
}

void TestConflictResolver::crlfConflictLoadsSafely() {
    const QString repo = createBothModified(QStringLiteral("crlf"), QStringLiteral("file.txt"),
                                             "base\r\n", "ours\r\n", "theirs\r\n");
    QVERIFY(!repo.isEmpty());
    GitConflictService service;
    ConflictResolverWidget resolver(&service);
    prepare(&resolver, scan(repo, &service));
    QVERIFY(resolver.unresolvedHunksForTesting(QStringLiteral("file.txt")) == 1);
    QTest::mouseClick(button(&resolver, QStringLiteral("acceptTheirsButton")), Qt::LeftButton);
    QVERIFY(resolver.isFileReadyForTesting(QStringLiteral("file.txt")));
}

void TestConflictResolver::diff3MarkersLoad() {
    const QString repo = createBothModified(QStringLiteral("diff3"), QStringLiteral("file.txt"),
                                             "base\n", "ours\n", "theirs\n", true);
    QVERIFY(!repo.isEmpty());
    GitConflictService service;
    ConflictResolverWidget resolver(&service);
    prepare(&resolver, scan(repo, &service));
    QVERIFY(editor(&resolver, QStringLiteral("conflictBaseEditor"))->toPlainText().contains(QStringLiteral("base")));
    QTest::mouseClick(button(&resolver, QStringLiteral("acceptBothButton")), Qt::LeftButton);
    QVERIFY(resolver.isFileReadyForTesting(QStringLiteral("file.txt")));
}

void TestConflictResolver::invalidMarkersAreSafe() {
    const QString repo = createBothModified(QStringLiteral("invalid"), QStringLiteral("file.txt"),
                                             "base\n", "ours\n", "theirs\n");
    QVERIFY(!repo.isEmpty());
    QVERIFY(writeFile(repo, QStringLiteral("file.txt"), "<<<<<<< ours\nbroken\n>>>>>>> theirs\n"));
    GitConflictService service;
    ConflictResolverWidget resolver(&service);
    prepare(&resolver, scan(repo, &service));
    auto *status = resolver.findChild<QLabel *>(QStringLiteral("conflictResolverStatus"));
    QVERIFY(status->text().contains(QStringLiteral("格式错误")));
    QVERIFY(!button(&resolver, QStringLiteral("acceptOursButton"))->isEnabled());
    QVERIFY(!resolver.resolutionPlanForFile(QStringLiteral("file.txt")).ready);
}

void TestConflictResolver::staleRepositoryCallbackIsIgnored() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString repoA = QDir(temp.path()).filePath(QStringLiteral("repo-a"));
    const QString repoB = QDir(temp.path()).filePath(QStringLiteral("repo-b"));
    QVERIFY(QDir().mkpath(repoA));
    QVERIFY(QDir().mkpath(repoB));
    DelayedContentExecutor executor;
    GitConflictService service(&executor);
    service.setWorkingDirectory(repoA);
    ConflictSnapshot a;
    a.repositoryPath = repoA;
    a.hasConflicts = true;
    a.conflictCount = 1;
    a.files = {ConflictFile{QStringLiteral("a.txt"), ConflictType::BothModified,
                            true, true, true, QStringLiteral("a"), QStringLiteral("b"),
                            QStringLiteral("c")}};
    ConflictResolverWidget resolver(&service);
    resolver.show();
    resolver.setSnapshot(a);
    QVERIFY(executor.pending);
    service.setWorkingDirectory(repoB);
    ConflictSnapshot b;
    b.repositoryPath = repoB;
    resolver.setSnapshot(b);
    executor.release();
    QCoreApplication::processEvents();
    QVERIFY(resolver.isEmptyStateForTesting());
    QVERIFY(resolver.currentPathForTesting().isEmpty());
    qInfo().noquote() << QStringLiteral("STALE_CONFLICT_CONTENT ignored=1 old_repo=%1 active_repo=%2")
                            .arg(repoA, repoB);
}

void TestConflictResolver::rapidFileSwitchingKeepsCurrentFile() {
    const QString repo = createTwoFileConflict(QStringLiteral("rapid"));
    QVERIFY(!repo.isEmpty());
    GitConflictService service;
    ConflictResolverWidget resolver(&service);
    prepare(&resolver, scan(repo, &service));
    auto *list = resolver.fileListForTesting();
    for (int i = 0; i < 20; ++i) {
        list->setCurrentRow(i % 2);
        QCoreApplication::processEvents();
    }
    list->setCurrentRow(1);
    const QString expected = list->item(1)->data(Qt::UserRole).toString();
    QVERIFY(waitUntil([&resolver, &expected]() { return resolver.currentPathForTesting() == expected; }));
    QVERIFY(editor(&resolver, QStringLiteral("conflictOursEditor"))->toPlainText().contains(QStringLiteral("ours")));
}

void TestConflictResolver::filesystemRemainsUnchanged() {
    const QString repo = createBothModified(QStringLiteral("unchanged"), QStringLiteral("file.txt"),
                                             "base\n", "ours\n", "theirs\n");
    QVERIFY(!repo.isEmpty());
    const QByteArray before = readFile(repo, QStringLiteral("file.txt"));
    const QByteArray beforeHash = QCryptographicHash::hash(before, QCryptographicHash::Sha256);
    GitConflictService service;
    {
        ConflictResolverWidget resolver(&service);
        prepare(&resolver, scan(repo, &service));
        QTest::mouseClick(button(&resolver, QStringLiteral("acceptBothButton")), Qt::LeftButton);
        resolver.resultEditorForTesting()->setPlainText(QStringLiteral("manual only\n"));
        QVERIFY(resolver.isFileReadyForTesting(QStringLiteral("file.txt")));
    }
    const QByteArray after = readFile(repo, QStringLiteral("file.txt"));
    QCOMPARE(QCryptographicHash::hash(after, QCryptographicHash::Sha256), beforeHash);
    QCOMPARE(after, before);
    qInfo().noquote() << QStringLiteral("FILESYSTEM_UNCHANGED path=%1 sha256=%2 byte_equal=1")
                            .arg(QDir(repo).filePath(QStringLiteral("file.txt")),
                                 QString::fromLatin1(beforeHash.toHex()));
}

void TestConflictResolver::resolverInvokesNoGitWrites() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QStringLiteral("file.txt");
    const QString content = QStringLiteral("<<<<<<< ours\nours\n=======\ntheirs\n>>>>>>> theirs\n");
    QVERIFY(writeFile(temp.path(), path, content.toUtf8()));
    RecordingExecutor executor;
    GitConflictService service(&executor);
    service.setWorkingDirectory(temp.path());
    ConflictSnapshot snapshot;
    snapshot.repositoryPath = temp.path();
    snapshot.hasConflicts = true;
    snapshot.conflictCount = 1;
    snapshot.files = {ConflictFile{path, ConflictType::BothModified, true, true, true,
                                   QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}};
    ConflictResolverWidget resolver(&service);
    prepare(&resolver, snapshot);
    QTest::mouseClick(button(&resolver, QStringLiteral("acceptOursButton")), Qt::LeftButton);
    QVERIFY(resolver.isFileReadyForTesting(path));
    const QSet<QString> forbidden = {QStringLiteral("add"), QStringLiteral("rm"),
                                     QStringLiteral("checkout"), QStringLiteral("restore"),
                                     QStringLiteral("reset"), QStringLiteral("commit"),
                                     QStringLiteral("merge"), QStringLiteral("rebase"),
                                     QStringLiteral("cherry-pick"), QStringLiteral("revert")};
    for (const QString &command : executor.commands) {
        QVERIFY2(!forbidden.contains(command), qPrintable(command));
    }
    qInfo().noquote() << QStringLiteral("RESOLVER_GIT_WRITES=0 read_commands=%1")
                            .arg(executor.commands.join(QLatin1Char(',')));
}

void TestConflictResolver::noConflictShowsEmptyState() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    GitConflictService service;
    service.setWorkingDirectory(temp.path());
    ConflictSnapshot snapshot;
    snapshot.repositoryPath = temp.path();
    ConflictResolverWidget resolver(&service);
    resolver.show();
    resolver.setSnapshot(snapshot);
    QVERIFY(resolver.isEmptyStateForTesting());
    QVERIFY(!button(&resolver, QStringLiteral("acceptOursButton"))->isEnabled());
    auto *banner = resolver.findChild<QLabel *>(QStringLiteral("conflictOperationBanner"));
    auto *hint = resolver.findChild<QLabel *>(QStringLiteral("conflictOperationHint"));
    auto *continueButton = button(&resolver, QStringLiteral("continueConflictOperationButton"));
    QCOMPARE(banner->text(), QStringLiteral("当前没有冲突"));
    QCOMPARE(hint->text(), QStringLiteral("当前仓库没有未解决冲突。"));
    QVERIFY(!continueButton->text().contains(QStringLiteral("未知操作")));
}

void TestConflictResolver::operationBannerReflectsSnapshot() {
    const QString repo = createBothModified(QStringLiteral("operation-banner"),
                                             QStringLiteral("file.txt"),
                                             "base\n", "ours\n", "theirs\n");
    QVERIFY(!repo.isEmpty());
    GitConflictService service;
    ConflictSnapshot snapshot = scan(repo, &service);
    snapshot.operationType = ConflictOperationType::Rebase;
    ConflictResolverWidget resolver(&service);
    prepare(&resolver, snapshot);
    auto *banner = resolver.findChild<QLabel *>(QStringLiteral("conflictOperationBanner"));
    auto *hint = resolver.findChild<QLabel *>(QStringLiteral("conflictOperationHint"));
    auto *planHint = resolver.findChild<QLabel *>(QStringLiteral("conflictResolutionPlanHint"));
    QCOMPARE(banner->text(), QStringLiteral("Rebase Conflict"));
    QVERIFY(hint->text().contains(QStringLiteral("Stage 2")));
    QVERIFY(!hint->text().contains(QStringLiteral("当前分支")));
    QVERIFY(planHint);
    QCOMPARE(planHint->text(),
             QStringLiteral("Accept 操作仅更新当前解决方案；点击 Apply 后才会写入并暂存。"));
}

void TestConflictResolver::mainWindowShowsAndResetsConflictEntry() {
    const QString repoA = createBothModified(QStringLiteral("main-window-a"),
                                              QStringLiteral("file.txt"),
                                              "base\n", "ours\n", "theirs\n");
    const QString repoB = createRepository(QStringLiteral("main-window-b"));
    QVERIFY(!repoA.isEmpty());
    QVERIFY(!repoB.isEmpty());
    MainWindow window;
    window.resize(1200, 820);
    window.show();
    QVERIFY(QMetaObject::invokeMethod(&window, "onProjectSelected",
                                      Q_ARG(QString, repoA)));
    auto *entry = window.findChild<QPushButton *>(QStringLiteral("resolveConflictsButton"));
    QVERIFY(entry);
    QVERIFY(waitUntil([entry]() { return entry->isVisible() && entry->isEnabled(); }, 20000));
    auto *summary = window.findChild<QLabel *>(QStringLiteral("conflictSummaryLabel"));
    QVERIFY(summary->text().contains(QStringLiteral("检测到 1 个冲突文件")));
    QTest::mouseClick(entry, Qt::LeftButton);
    QVERIFY(waitUntil([&window]() {
        return window.findChild<QDialog *>(QStringLiteral("conflictResolverDialog")) != nullptr;
    }));
    auto *dialog = window.findChild<QDialog *>(QStringLiteral("conflictResolverDialog"));
    QCOMPARE(dialog->windowTitle(), QStringLiteral("Git 冲突解决"));
    QVERIFY(QMetaObject::invokeMethod(&window, "onProjectSelected",
                                      Q_ARG(QString, repoB)));
    QVERIFY(waitUntil([entry, &window]() {
        return !entry->isVisible()
            && window.findChild<QDialog *>(QStringLiteral("conflictResolverDialog")) == nullptr;
    }, 20000));
    qInfo().noquote() << QStringLiteral("MAINWINDOW_CONFLICT_SESSION switched_from=%1 switched_to=%2 old_resolver_closed=1")
                            .arg(repoA, repoB);
}

void TestConflictResolver::applyCurrentUsesBusyStateAndRefreshesGitSnapshot() {
    const QString repo = createBothModified(QStringLiteral("workflow-apply"), QStringLiteral("file.txt"),
                                             "base\n", "ours\n", "theirs\n");
    QVERIFY(!repo.isEmpty());
    GitConflictService reader;
    const ConflictSnapshot snapshot = scan(repo, &reader);
    GitConflictResolutionService writer;
    writer.setWorkingDirectory(repo);
    ConflictResolverWidget resolver(&reader);
    resolver.setResolutionService(&writer);
    prepare(&resolver, snapshot);
    QTest::mouseClick(button(&resolver, QStringLiteral("acceptOursButton")), Qt::LeftButton);
    QVERIFY(waitUntil([&resolver] { return resolver.isFileReadyForTesting(QStringLiteral("file.txt")); }));
    auto *apply = button(&resolver, QStringLiteral("applyCurrentConflictButton"));
    QTest::mouseClick(apply, Qt::LeftButton);
    QVERIFY(!apply->isEnabled());
    QVERIFY(waitUntil([&resolver] { return resolver.isEmptyStateForTesting(); }));
    auto *workflow = resolver.findChild<QLabel *>(QStringLiteral("conflictWorkflowStatus"));
    auto *banner = resolver.findChild<QLabel *>(QStringLiteral("conflictOperationBanner"));
    auto *hint = resolver.findChild<QLabel *>(QStringLiteral("conflictOperationHint"));
    auto *continueButton = button(&resolver, QStringLiteral("continueConflictOperationButton"));
    QVERIFY(workflow->text().contains(QStringLiteral("已解决并暂存")));
    QCOMPARE(banner->text(), QStringLiteral("所有冲突已解决"));
    QCOMPARE(hint->text(),
             QStringLiteral("所有冲突文件均已处理，可以继续当前 Git 操作。"));
    QCOMPARE(continueButton->text(), QStringLiteral("继续 Merge"));
    QVERIFY(continueButton->isEnabled());
}

void TestConflictResolver::applyFailureExplainsWrittenButUnstagedFile() {
    const QString repo = createBothModified(QStringLiteral("workflow-stage-failure"), QStringLiteral("file.txt"),
                                             "base\n", "ours\n", "theirs\n");
    QVERIFY(!repo.isEmpty());
    GitConflictService reader;
    const ConflictSnapshot snapshot = scan(repo, &reader);
    GitConflictResolutionService writer;
    writer.setWorkingDirectory(repo);
    writer.setFailureInjectionForTesting({false, true, false, false});
    ConflictResolverWidget resolver(&reader);
    resolver.setResolutionService(&writer);
    prepare(&resolver, snapshot);
    QTest::mouseClick(button(&resolver, QStringLiteral("acceptOursButton")), Qt::LeftButton);
    QVERIFY(waitUntil([&resolver] { return resolver.isFileReadyForTesting(QStringLiteral("file.txt")); }));
    QTest::mouseClick(button(&resolver, QStringLiteral("applyCurrentConflictButton")), Qt::LeftButton);
    auto *workflow = resolver.findChild<QLabel *>(QStringLiteral("conflictWorkflowStatus"));
    QVERIFY(waitUntil([workflow] { return workflow->text().contains(QStringLiteral("文件已写回，但暂存失败")); }));
    QVERIFY(!resolver.isEmptyStateForTesting());
}

void TestConflictResolver::applyAllReportsPartialSuccessThroughClickedUi() {
    const QString repo = createTwoFileConflict(QStringLiteral("workflow-partial"));
    QVERIFY(!repo.isEmpty());
    GitConflictService reader;
    const ConflictSnapshot snapshot = scan(repo, &reader);
    GitConflictResolutionService writer;
    writer.setWorkingDirectory(repo);
    QTemporaryDir repositoryB;
    QVERIFY(repositoryB.isValid());
    int stageBoundary = 0;
    writer.setBeforeStageHookForTesting([&writer, &repositoryB, &stageBoundary] {
        if (++stageBoundary == 3) {
            writer.setWorkingDirectory(repositoryB.path());
        }
    });
    ConflictResolverWidget resolver(&reader);
    resolver.setResolutionService(&writer);
    prepare(&resolver, snapshot);
    auto *list = resolver.fileListForTesting();
    QCOMPARE(list->count(), 2);
    QTest::mouseClick(button(&resolver, QStringLiteral("acceptOursButton")), Qt::LeftButton);
    QVERIFY(waitUntil([&resolver] { return resolver.isFileReadyForTesting(resolver.currentPathForTesting()); }));
    list->setCurrentRow(1);
    const QString secondPath = list->item(1)->data(Qt::UserRole).toString();
    auto *acceptTheirs = button(&resolver, QStringLiteral("acceptTheirsButton"));
    QVERIFY(waitUntil([&resolver, acceptTheirs, &secondPath] {
        return resolver.currentPathForTesting() == secondPath && acceptTheirs->isEnabled();
    }));
    QTest::mouseClick(acceptTheirs, Qt::LeftButton);
    QVERIFY(waitUntil([&resolver, &secondPath] { return resolver.isFileReadyForTesting(secondPath); }));
    QTest::mouseClick(button(&resolver, QStringLiteral("applyAllReadyConflictButton")), Qt::LeftButton);
    auto *workflow = resolver.findChild<QLabel *>(QStringLiteral("conflictWorkflowStatus"));
    QVERIFY(waitUntil([workflow] { return workflow->text().contains(QStringLiteral("部分成功")); }));
}

void TestConflictResolver::continueOnlyBecomesAvailableAfterVerifiedResolution() {
    const QString repo = createBothModified(QStringLiteral("workflow-continue"), QStringLiteral("file.txt"),
                                             "base\n", "ours\n", "theirs\n");
    QVERIFY(!repo.isEmpty());
    GitConflictService reader;
    const ConflictSnapshot snapshot = scan(repo, &reader);
    QCOMPARE(snapshot.operationType, ConflictOperationType::Merge);
    GitConflictResolutionService writer;
    writer.setWorkingDirectory(repo);
    ConflictResolverWidget resolver(&reader);
    resolver.setResolutionService(&writer);
    prepare(&resolver, snapshot);
    auto *continueButton = button(&resolver, QStringLiteral("continueConflictOperationButton"));
    QVERIFY(!continueButton->isEnabled());
    QTest::mouseClick(button(&resolver, QStringLiteral("acceptTheirsButton")), Qt::LeftButton);
    QVERIFY(waitUntil([&resolver] { return resolver.isFileReadyForTesting(QStringLiteral("file.txt")); }));
    QTest::mouseClick(button(&resolver, QStringLiteral("applyCurrentConflictButton")), Qt::LeftButton);
    QVERIFY(waitUntil([&resolver] { return resolver.isEmptyStateForTesting(); }));
    QVERIFY(continueButton->isEnabled());
    const ProcessResult mergeHead = runGit(repo, {QStringLiteral("rev-parse"), QStringLiteral("-q"),
                                                 QStringLiteral("--verify"), QStringLiteral("MERGE_HEAD")});
    QCOMPARE(mergeHead.exitCode, 0); // Resolver never auto-continues.
    QTest::mouseClick(continueButton, Qt::LeftButton);
    auto *banner = resolver.findChild<QLabel *>(QStringLiteral("conflictOperationBanner"));
    auto *hint = resolver.findChild<QLabel *>(QStringLiteral("conflictOperationHint"));
    auto *workflow = resolver.findChild<QLabel *>(QStringLiteral("conflictWorkflowStatus"));
    QVERIFY(waitUntil([workflow] {
        return workflow->text().contains(QStringLiteral("继续完成"));
    }, 20000));
    QCOMPARE(banner->text(), QStringLiteral("Merge 已完成"));
    QCOMPARE(hint->text(), QStringLiteral("当前 Git 操作已成功完成。"));
    QVERIFY(!banner->text().contains(QStringLiteral("Unknown Conflict")));
    QVERIFY(!continueButton->text().contains(QStringLiteral("未知操作")));
    QVERIFY(!continueButton->isEnabled());
    const ProcessResult completedMergeHead = runGit(repo,
        {QStringLiteral("rev-parse"), QStringLiteral("-q"),
         QStringLiteral("--verify"), QStringLiteral("MERGE_HEAD")});
    QVERIFY(completedMergeHead.exitCode != 0);
}

void TestConflictResolver::abortRequiresVisibleConfirmation() {
    const QString repo = createBothModified(QStringLiteral("workflow-abort"), QStringLiteral("file.txt"),
                                             "base\n", "ours\n", "theirs\n");
    QVERIFY(!repo.isEmpty());
    GitConflictService reader;
    const ConflictSnapshot snapshot = scan(repo, &reader);
    GitConflictResolutionService writer;
    writer.setWorkingDirectory(repo);
    ConflictResolverWidget resolver(&reader);
    resolver.setResolutionService(&writer);
    prepare(&resolver, snapshot);
    auto *abort = button(&resolver, QStringLiteral("abortConflictOperationButton"));
    QTimer::singleShot(0, [] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (box) QTest::mouseClick(box->button(QMessageBox::Yes), Qt::LeftButton);
    });
    QTest::mouseClick(abort, Qt::LeftButton);
    QVERIFY(waitUntil([&resolver] { return resolver.isEmptyStateForTesting(); }));
}

QTEST_MAIN(TestConflictResolver)
#include "test_conflict_resolver.moc"
