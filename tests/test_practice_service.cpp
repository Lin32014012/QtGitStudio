#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDir>

#include "PracticeService.h"
#include "GitCommandExecutor.h"

namespace {

// Same ceiling rule as the service: the temp directory on this machine
// lives under a home directory that is itself a git work tree, so every
// child process must be fenced or git resolves upward.
bool runGit(const QString &repoPath, const QStringList &args,
            QByteArray *output = nullptr) {
    GitCommandExecutor executor;
    executor.setWorkingDirectory(repoPath);
    executor.setExtraEnvironment({
        QStringLiteral("GIT_CEILING_DIRECTORIES=%1")
            .arg(QFileInfo(repoPath).absolutePath()),
        // --continue paths open an editor for the message; keep them
        // non-interactive or the test would hang.
        QStringLiteral("GIT_EDITOR=true")});
    const auto result = executor.execute(args.first(), args.mid(1));
    if (output) {
        *output = result.output;
    }
    return result.exitCode == 0;
}

bool writeFile(const QString &path, const QString &content) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(content.toUtf8());
    file.close();
    return true;
}

QString readFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

}  // namespace

class TestPracticeService : public QObject {
    Q_OBJECT

private slots:
    void catalogIsComplete();
    void unknownPracticeRejected();
    void emptyParentRejected();

    void firstCommitSandboxStartsStagedButUncommitted();
    void firstCommitCheckProgressesToPass();
    void regeneratingResetsTheSandbox();

    void branchMergeSandboxHasUnmergedBranch();
    void branchMergeCheckPassesAfterMerge();

    void conflictSandboxStartsInConflict();
    void conflictCheckPassesOnlyWhenBothSidesKept();

    void partialStagingCheckRequiresExactlyOneFile();
    void revertPracticeDetectsRevertCommit();

    void keepOneSideRejectsTheOtherBranch();
    void multipleFilesRejectsLeavingOneUnresolved();
    void rebaseConflictPassesAfterContinue();
    void cherryPickConflictPassesAfterContinue();
    void abortPracticeRejectsCommittingTheMerge();
};

void TestPracticeService::catalogIsComplete() {
    const auto catalog = PracticeService::catalog();
    QVERIFY(catalog.size() >= 10);
    QStringList ids;
    QStringList chapters;
    for (const auto &item : catalog) {
        QVERIFY(!item.id.isEmpty());
        QVERIFY(!item.title.isEmpty());
        QVERIFY(!item.goal.isEmpty());
        QVERIFY(!item.chapter.isEmpty());
        if (!chapters.contains(item.chapter)) {
            chapters.append(item.chapter);
        }
        QVERIFY(!ids.contains(item.id));
        ids.append(item.id);
        // every catalog entry must round-trip through definition()
        QCOMPARE(PracticeService::definition(item.id).title, item.title);
    }
    // Both chapters must be represented, and the conflict chapter is the
    // point of the tool - keep it from silently disappearing.
    QVERIFY(chapters.contains(QStringLiteral("基础")));
    QVERIFY(chapters.contains(QStringLiteral("冲突")));
    int conflictCount = 0;
    for (const auto &item : catalog) {
        if (item.chapter == QStringLiteral("冲突")) {
            ++conflictCount;
        }
    }
    QVERIFY2(conflictCount >= 6, qPrintable(QString::number(conflictCount)));
}

void TestPracticeService::unknownPracticeRejected() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString repo;
    QString error;
    QVERIFY(!PracticeService::generate(QStringLiteral("no-such-practice"),
                                      dir.path(), &repo, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(repo.isEmpty());
}

void TestPracticeService::emptyParentRejected() {
    QString repo;
    QString error;
    QVERIFY(!PracticeService::generate(QStringLiteral("first-commit"),
                                      QString(), &repo, &error));
    QVERIFY(!error.isEmpty());
}

void TestPracticeService::firstCommitSandboxStartsStagedButUncommitted() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString repo;
    QString error;
    QVERIFY2(PracticeService::generate(QStringLiteral("first-commit"),
                                       dir.path(), &repo, &error),
             qPrintable(error));
    QVERIFY(QFileInfo(repo + QStringLiteral("/.git")).isDir());

    // README exists and is staged; there is no commit yet.
    QVERIFY(QFileInfo::exists(repo + QStringLiteral("/README.md")));
    QByteArray staged;
    QVERIFY(runGit(repo, {QStringLiteral("diff"), QStringLiteral("--cached"),
                          QStringLiteral("--name-only")}, &staged));
    QVERIFY(QString::fromUtf8(staged).contains(QStringLiteral("README.md")));

    QByteArray count;
    QVERIFY(!runGit(repo, {QStringLiteral("rev-list"), QStringLiteral("--count"),
                           QStringLiteral("HEAD")}, &count));

    const auto check = PracticeService::check(QStringLiteral("first-commit"), repo);
    QVERIFY(!check.passed);
    QVERIFY(!check.detail.isEmpty());
}

void TestPracticeService::firstCommitCheckProgressesToPass() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString repo, error;
    QVERIFY2(PracticeService::generate(QStringLiteral("first-commit"),
                                       dir.path(), &repo, &error), qPrintable(error));

    QVERIFY(runGit(repo, {QStringLiteral("commit"), QStringLiteral("-m"),
                          QStringLiteral("完成第一次提交")}));
    const auto check = PracticeService::check(QStringLiteral("first-commit"), repo);
    QVERIFY2(check.passed, qPrintable(check.detail));
    QVERIFY(!check.detail.isEmpty());
}

void TestPracticeService::regeneratingResetsTheSandbox() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString repo, error;
    QVERIFY(PracticeService::generate(QStringLiteral("first-commit"),
                                      dir.path(), &repo, &error));
    QVERIFY(runGit(repo, {QStringLiteral("commit"), QStringLiteral("-m"),
                          QStringLiteral("学生做完了")}));
    QVERIFY(PracticeService::check(QStringLiteral("first-commit"), repo).passed);

    // Restarting a practice must restore the initial state.
    QVERIFY2(PracticeService::generate(QStringLiteral("first-commit"),
                                       dir.path(), &repo, &error), qPrintable(error));
    QVERIFY(!PracticeService::check(QStringLiteral("first-commit"), repo).passed);
}

void TestPracticeService::branchMergeSandboxHasUnmergedBranch() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString repo, error;
    QVERIFY2(PracticeService::generate(QStringLiteral("branch-and-merge"),
                                       dir.path(), &repo, &error), qPrintable(error));

    QByteArray current;
    QVERIFY(runGit(repo, {QStringLiteral("rev-parse"),
                          QStringLiteral("--abbrev-ref"),
                          QStringLiteral("HEAD")}, &current));
    QCOMPARE(QString::fromUtf8(current).trimmed(), QStringLiteral("main"));

    QByteArray merged;
    QVERIFY(runGit(repo, {QStringLiteral("branch"), QStringLiteral("--merged"),
                          QStringLiteral("main")}, &merged));
    QVERIFY(!QString::fromUtf8(merged).contains(QStringLiteral("feature/hello")));

    const auto check = PracticeService::check(QStringLiteral("branch-and-merge"), repo);
    QVERIFY(!check.passed);
}

void TestPracticeService::branchMergeCheckPassesAfterMerge() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString repo, error;
    QVERIFY(PracticeService::generate(QStringLiteral("branch-and-merge"),
                                      dir.path(), &repo, &error));

    QVERIFY(runGit(repo, {QStringLiteral("merge"), QStringLiteral("--no-ff"),
                          QStringLiteral("-m"), QStringLiteral("合并 feature/hello"),
                          QStringLiteral("feature/hello")}));
    const auto check = PracticeService::check(QStringLiteral("branch-and-merge"), repo);
    QVERIFY2(check.passed, qPrintable(check.detail));
}

void TestPracticeService::conflictSandboxStartsInConflict() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString repo, error;
    QVERIFY2(PracticeService::generate(QStringLiteral("resolve-merge-conflict"),
                                       dir.path(), &repo, &error), qPrintable(error));

    // MERGE_HEAD proves the sandbox really is mid-merge.
    QByteArray out;
    QVERIFY(runGit(repo, {QStringLiteral("rev-parse"), QStringLiteral("--verify"),
                          QStringLiteral("MERGE_HEAD")}, &out));

    const QString app = readFile(repo + QStringLiteral("/app.js"));
    QVERIFY(app.contains(QStringLiteral("<<<<<<<")));
    QVERIFY(app.contains(QStringLiteral("main 分支的说法")));
    QVERIFY(app.contains(QStringLiteral("feature 分支的说法")));

    const auto check = PracticeService::check(QStringLiteral("resolve-merge-conflict"),
                                              repo);
    QVERIFY(!check.passed);
    QVERIFY(check.detail.contains(QStringLiteral("合并尚未完成")));
}

void TestPracticeService::conflictCheckPassesOnlyWhenBothSidesKept() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString repo, error;
    QVERIFY(PracticeService::generate(QStringLiteral("resolve-merge-conflict"),
                                      dir.path(), &repo, &error));

    // Keeping only one side must not pass: the goal is to preserve intent
    // from both branches.
    QVERIFY(writeFile(repo + QStringLiteral("/app.js"),
                      QStringLiteral("const greeting = \"main 分支的说法\";\n"
                                     "module.exports = greeting;\n")));
    QVERIFY(runGit(repo, {QStringLiteral("add"), QStringLiteral("app.js")}));
    QVERIFY(runGit(repo, {QStringLiteral("commit"), QStringLiteral("--no-edit")}));
    auto oneSided = PracticeService::check(QStringLiteral("resolve-merge-conflict"), repo);
    QVERIFY(!oneSided.passed);
    QVERIFY(oneSided.detail.contains(QStringLiteral("feature 一侧")));

    // Redo the sandbox and keep both sides this time.
    QVERIFY(PracticeService::generate(QStringLiteral("resolve-merge-conflict"),
                                      dir.path(), &repo, &error));
    QVERIFY(writeFile(repo + QStringLiteral("/app.js"),
                      QStringLiteral("const greetingMain = \"main 分支的说法\";\n"
                                     "const greetingFeature = \"feature 分支的说法\";\n"
                                     "module.exports = { greetingMain, greetingFeature };\n")));
    QVERIFY(runGit(repo, {QStringLiteral("add"), QStringLiteral("app.js")}));
    QVERIFY(runGit(repo, {QStringLiteral("commit"), QStringLiteral("--no-edit")}));
    const auto bothSides =
        PracticeService::check(QStringLiteral("resolve-merge-conflict"), repo);
    QVERIFY2(bothSides.passed, qPrintable(bothSides.detail));
}

void TestPracticeService::partialStagingCheckRequiresExactlyOneFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString repo, error;
    QVERIFY2(PracticeService::generate(QStringLiteral("partial-staging"),
                                       dir.path(), &repo, &error), qPrintable(error));

    auto none = PracticeService::check(QStringLiteral("partial-staging"), repo);
    QVERIFY(!none.passed);

    // Staging both is not the exercise either.
    QVERIFY(runGit(repo, {QStringLiteral("add"), QStringLiteral("-A")}));
    auto both = PracticeService::check(QStringLiteral("partial-staging"), repo);
    QVERIFY(!both.passed);

    // Exactly one staged, one left in the working tree.
    QVERIFY(runGit(repo, {QStringLiteral("reset")}));
    QVERIFY(runGit(repo, {QStringLiteral("add"), QStringLiteral("alpha.txt")}));
    const auto exact = PracticeService::check(QStringLiteral("partial-staging"), repo);
    QVERIFY2(exact.passed, qPrintable(exact.detail));
    QVERIFY(exact.detail.contains(QStringLiteral("alpha.txt")));
}

void TestPracticeService::revertPracticeDetectsRevertCommit() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString repo, error;
    QVERIFY2(PracticeService::generate(QStringLiteral("revert-a-commit"),
                                       dir.path(), &repo, &error), qPrintable(error));

    QVERIFY(!PracticeService::check(QStringLiteral("revert-a-commit"), repo).passed);

    // reset would also restore the file, but the practice forbids rewriting
    // history, so only a revert commit satisfies it.
    QVERIFY(runGit(repo, {QStringLiteral("revert"), QStringLiteral("--no-edit"),
                          QStringLiteral("HEAD")}));
    const auto check = PracticeService::check(QStringLiteral("revert-a-commit"), repo);
    QVERIFY2(check.passed, qPrintable(check.detail));

    QByteArray subject;
    QVERIFY(runGit(repo, {QStringLiteral("log"), QStringLiteral("-1"),
                          QStringLiteral("--format=%s")}, &subject));
    QVERIFY(QString::fromUtf8(subject).trimmed().startsWith(QStringLiteral("Revert")));
}


void TestPracticeService::keepOneSideRejectsTheOtherBranch() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString repo, error;
    QVERIFY2(PracticeService::generate(QStringLiteral("conflict-keep-one-side"),
                                       dir.path(), &repo, &error), qPrintable(error));
    QVERIFY(!PracticeService::check(QStringLiteral("conflict-keep-one-side"), repo).passed);

    // Keeping the wrong side must fail: the practice pins main.
    QVERIFY(writeFile(repo + QStringLiteral("/app.js"),
                      QStringLiteral("const who = \"feature 版本\";\n")));
    QVERIFY(runGit(repo, {QStringLiteral("add"), QStringLiteral("app.js")}));
    QVERIFY(runGit(repo, {QStringLiteral("commit"), QStringLiteral("--no-edit")}));
    auto wrong = PracticeService::check(QStringLiteral("conflict-keep-one-side"), repo);
    QVERIFY(!wrong.passed);
    QVERIFY(wrong.detail.contains(QStringLiteral("main")));

    // Redo and keep main.
    QVERIFY(PracticeService::generate(QStringLiteral("conflict-keep-one-side"),
                                      dir.path(), &repo, &error));
    QVERIFY(writeFile(repo + QStringLiteral("/app.js"),
                      QStringLiteral("const who = \"main 版本\";\n")));
    QVERIFY(runGit(repo, {QStringLiteral("add"), QStringLiteral("app.js")}));
    QVERIFY(runGit(repo, {QStringLiteral("commit"), QStringLiteral("--no-edit")}));
    const auto ok = PracticeService::check(QStringLiteral("conflict-keep-one-side"), repo);
    QVERIFY2(ok.passed, qPrintable(ok.detail));
}

void TestPracticeService::multipleFilesRejectsLeavingOneUnresolved() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString repo, error;
    QVERIFY2(PracticeService::generate(QStringLiteral("conflict-multiple-files"),
                                       dir.path(), &repo, &error), qPrintable(error));
    QVERIFY(!PracticeService::check(QStringLiteral("conflict-multiple-files"), repo).passed);

    // Resolving only alpha must still fail - missing a file is the mistake
    // this practice exists to catch.
    QVERIFY(writeFile(repo + QStringLiteral("/alpha.txt"),
                      QStringLiteral("alpha = 主线改的\n")));
    QVERIFY(runGit(repo, {QStringLiteral("add"), QStringLiteral("alpha.txt")}));
    auto partial = PracticeService::check(QStringLiteral("conflict-multiple-files"), repo);
    QVERIFY(!partial.passed);

    QVERIFY(writeFile(repo + QStringLiteral("/beta.txt"),
                      QStringLiteral("beta = 主线改的\n")));
    QVERIFY(runGit(repo, {QStringLiteral("add"), QStringLiteral("beta.txt")}));
    QVERIFY(runGit(repo, {QStringLiteral("commit"), QStringLiteral("--no-edit")}));
    const auto ok = PracticeService::check(QStringLiteral("conflict-multiple-files"), repo);
    QVERIFY2(ok.passed, qPrintable(ok.detail));
}

void TestPracticeService::rebaseConflictPassesAfterContinue() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString repo, error;
    QVERIFY2(PracticeService::generate(QStringLiteral("conflict-rebase"),
                                       dir.path(), &repo, &error), qPrintable(error));

    auto before = PracticeService::check(QStringLiteral("conflict-rebase"), repo);
    QVERIFY(!before.passed);
    QVERIFY(before.detail.contains(QStringLiteral("rebase")));

    QVERIFY(writeFile(repo + QStringLiteral("/config.ini"),
                      QStringLiteral("mode = main\nmode_feature = feature\n")));
    QVERIFY(runGit(repo, {QStringLiteral("add"), QStringLiteral("config.ini")}));
    QVERIFY(runGit(repo, {QStringLiteral("rebase"), QStringLiteral("--continue")}));

    const auto after = PracticeService::check(QStringLiteral("conflict-rebase"), repo);
    QVERIFY2(after.passed, qPrintable(after.detail));
}

void TestPracticeService::cherryPickConflictPassesAfterContinue() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString repo, error;
    QVERIFY2(PracticeService::generate(QStringLiteral("conflict-cherry-pick"),
                                       dir.path(), &repo, &error), qPrintable(error));

    auto before = PracticeService::check(QStringLiteral("conflict-cherry-pick"), repo);
    QVERIFY(!before.passed);
    QVERIFY(before.detail.contains(QStringLiteral("cherry-pick")));

    QVERIFY(writeFile(repo + QStringLiteral("/version.txt"),
                      QStringLiteral("version = 主线 + 修复分支\n")));
    QVERIFY(runGit(repo, {QStringLiteral("add"), QStringLiteral("version.txt")}));
    QVERIFY(runGit(repo, {QStringLiteral("cherry-pick"), QStringLiteral("--continue")}));

    const auto after = PracticeService::check(QStringLiteral("conflict-cherry-pick"), repo);
    QVERIFY2(after.passed, qPrintable(after.detail));
}

void TestPracticeService::abortPracticeRejectsCommittingTheMerge() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString repo, error;
    QVERIFY2(PracticeService::generate(QStringLiteral("conflict-abort"),
                                       dir.path(), &repo, &error), qPrintable(error));
    QVERIFY(!PracticeService::check(QStringLiteral("conflict-abort"), repo).passed);

    // Resolving and committing is NOT the goal here: that leaves a merge
    // commit behind, which the history count catches.
    QVERIFY(writeFile(repo + QStringLiteral("/app.js"),
                      QStringLiteral("const who = \"main 版本\";\n")));
    QVERIFY(runGit(repo, {QStringLiteral("add"), QStringLiteral("app.js")}));
    QVERIFY(runGit(repo, {QStringLiteral("commit"), QStringLiteral("--no-edit")}));
    auto committed = PracticeService::check(QStringLiteral("conflict-abort"), repo);
    QVERIFY(!committed.passed);
    QVERIFY(committed.detail.contains(QStringLiteral("放弃")));

    // Aborting is.
    QVERIFY(PracticeService::generate(QStringLiteral("conflict-abort"),
                                      dir.path(), &repo, &error));
    QVERIFY(runGit(repo, {QStringLiteral("merge"), QStringLiteral("--abort")}));
    const auto ok = PracticeService::check(QStringLiteral("conflict-abort"), repo);
    QVERIFY2(ok.passed, qPrintable(ok.detail));
}

QTEST_MAIN(TestPracticeService)
#include "test_practice_service.moc"
