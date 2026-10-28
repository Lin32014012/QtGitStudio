#include "PracticeService.h"

#include "GitCommandExecutor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

namespace {

// Every child git process runs with GIT_CEILING_DIRECTORIES pinned to the
// sandbox's parent. Without it a sandbox created inside a directory that
// itself sits in a work tree (a home directory that is a repo, as on the
// development machine) resolves upward and the practice would quietly
// operate on somebody else's history.
// Note: GitCommandExecutor::execute() builds `git <command> <args>`, so the
// first element of `args` is treated as the subcommand, never the executable
// name - passing "git" there runs `git git ...`.
bool runGit(const QString &repoPath, const QStringList &args,
            QByteArray *output, QString *error) {
    GitCommandExecutor executor;
    executor.setWorkingDirectory(repoPath);
    executor.setExtraEnvironment({QStringLiteral("GIT_CEILING_DIRECTORIES=%1")
                                      .arg(QFileInfo(repoPath).absolutePath())});
    const auto result = executor.execute(args.first(), args.mid(1));
    if (output) {
        *output = result.output;
    }
    if (result.exitCode != 0) {
        if (error) {
            *error = QStringLiteral("git %1 失败：%2")
                         .arg(args.join(QLatin1Char(' ')),
                              QString::fromUtf8(result.errorOutput).trimmed());
        }
        return false;
    }
    return true;
}

bool runGitExpectFailure(const QString &repoPath, const QStringList &args) {
    GitCommandExecutor executor;
    executor.setWorkingDirectory(repoPath);
    executor.setExtraEnvironment({QStringLiteral("GIT_CEILING_DIRECTORIES=%1")
                                      .arg(QFileInfo(repoPath).absolutePath())});
    const auto result = executor.execute(args.first(), args.mid(1));
    return result.exitCode != 0;
}

bool writeFile(const QString &path, const QString &content) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << content;
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

// Identity is set per sandbox so the practice never depends on - or
// mutates - the machine's global git configuration.
bool initSandbox(const QString &repoPath, QString *error) {
    if (!runGit(repoPath, {QStringLiteral("init"), QStringLiteral("-b"),
                           QStringLiteral("main")}, nullptr, error)) {
        return false;
    }
    if (!runGit(repoPath, {QStringLiteral("config"), QStringLiteral("user.name"),
                           QStringLiteral("QtGitStudio Practice")}, nullptr, error)) {
        return false;
    }
    if (!runGit(repoPath, {QStringLiteral("config"), QStringLiteral("user.email"),
                           QStringLiteral("practice@qtgitstudio.invalid")},
                nullptr, error)) {
        return false;
    }
    return runGit(repoPath, {QStringLiteral("config"),
                             QStringLiteral("core.autocrlf"),
                             QStringLiteral("false")}, nullptr, error);
}

bool commitAll(const QString &repoPath, const QString &message, QString *error) {
    if (!runGit(repoPath, {QStringLiteral("add"), QStringLiteral("-A")},
                nullptr, error)) {
        return false;
    }
    return runGit(repoPath, {QStringLiteral("commit"), QStringLiteral("-m"), message},
                  nullptr, error);
}

bool isClean(const QString &repoPath) {
    QByteArray out;
    if (!runGit(repoPath, {QStringLiteral("status"), QStringLiteral("--porcelain")},
                &out, nullptr)) {
        return false;
    }
    return out.trimmed().isEmpty();
}

bool mergeInProgress(const QString &repoPath) {
    // MERGE_HEAD exists only while a merge is unresolved, so a *successful*
    // rev-parse is the signal - it fails once the merge is committed.
    QByteArray out;
    return runGit(repoPath,
                  {QStringLiteral("rev-parse"), QStringLiteral("--verify"),
                   QStringLiteral("--quiet"), QStringLiteral("MERGE_HEAD")},
                  &out, nullptr);
}

bool rebaseInProgress(const QString &repoPath) {
    // Rebase keeps its state in one of two directories depending on the
    // backend (`-m` interactive vs `--apply`).
    return QFileInfo(repoPath + QStringLiteral("/.git/rebase-merge")).isDir() ||
           QFileInfo(repoPath + QStringLiteral("/.git/rebase-apply")).isDir();
}

bool cherryPickInProgress(const QString &repoPath) {
    QByteArray out;
    return runGit(repoPath,
                  {QStringLiteral("rev-parse"), QStringLiteral("--verify"),
                   QStringLiteral("--quiet"), QStringLiteral("CHERRY_PICK_HEAD")},
                  &out, nullptr);
}

int commitCount(const QString &repoPath) {
    QByteArray out;
    if (!runGit(repoPath, {QStringLiteral("rev-list"), QStringLiteral("--count"),
                           QStringLiteral("HEAD")}, &out, nullptr)) {
        return -1;
    }
    bool ok = false;
    const int count = out.trimmed().toInt(&ok);
    return ok ? count : -1;
}

QStringList lines(const QByteArray &raw) {
    QStringList result;
    const QString text = QString::fromUtf8(raw);
    for (const QString &line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            result.append(trimmed);
        }
    }
    return result;
}

// ---- per-practice generators -------------------------------------------

bool generateFirstCommit(const QString &repoPath, QString *error) {
    if (!initSandbox(repoPath, error)) {
        return false;
    }
    if (!writeFile(repoPath + QStringLiteral("/README.md"),
                   QStringLiteral("# 我的第一个仓库\n\n"
                                  "把这一行下面的内容改掉，然后完成第一次提交。\n"))) {
        if (error) *error = QStringLiteral("写入 README 失败");
        return false;
    }
    // Staged but never committed: the practice is the commit itself.
    return runGit(repoPath, {QStringLiteral("add"), QStringLiteral("README.md")},
                  nullptr, error);
}

bool generateBranchMerge(const QString &repoPath, QString *error) {
    if (!initSandbox(repoPath, error)) {
        return false;
    }
    if (!writeFile(repoPath + QStringLiteral("/hello.txt"),
                   QStringLiteral("main 分支的初始内容\n"))) {
        if (error) *error = QStringLiteral("写入文件失败");
        return false;
    }
    if (!commitAll(repoPath, QStringLiteral("main: 初始提交"), error)) {
        return false;
    }
    if (!writeFile(repoPath + QStringLiteral("/notes.md"),
                   QStringLiteral("主线上的第二个提交\n"))) {
        if (error) *error = QStringLiteral("写入文件失败");
        return false;
    }
    if (!commitAll(repoPath, QStringLiteral("main: 第二个提交"), error)) {
        return false;
    }
    if (!runGit(repoPath, {QStringLiteral("checkout"), QStringLiteral("-b"),
                           QStringLiteral("feature/hello")}, nullptr, error)) {
        return false;
    }
    if (!writeFile(repoPath + QStringLiteral("/hello.txt"),
                   QStringLiteral("main 分支的初始内容\n"
                                  "feature/hello 追加的一行\n"))) {
        if (error) *error = QStringLiteral("写入文件失败");
        return false;
    }
    if (!commitAll(repoPath, QStringLiteral("feature: 追加一行"), error)) {
        return false;
    }
    // Student starts on main, with the branch waiting to be merged.
    return runGit(repoPath, {QStringLiteral("checkout"), QStringLiteral("main")},
                  nullptr, error);
}

bool generateResolveConflict(const QString &repoPath, QString *error) {
    if (!initSandbox(repoPath, error)) {
        return false;
    }
    const QString base =
        QStringLiteral("const greeting = \"hello\";\n"
                       "module.exports = greeting;\n");
    if (!writeFile(repoPath + QStringLiteral("/app.js"), base)) {
        if (error) *error = QStringLiteral("写入文件失败");
        return false;
    }
    if (!commitAll(repoPath, QStringLiteral("base: 初始 app.js"), error)) {
        return false;
    }
    if (!runGit(repoPath, {QStringLiteral("checkout"), QStringLiteral("-b"),
                           QStringLiteral("feature/conflict")}, nullptr, error)) {
        return false;
    }
    if (!writeFile(repoPath + QStringLiteral("/app.js"),
                   QStringLiteral("const greeting = \"feature 分支的说法\";\n"
                                  "module.exports = greeting;\n"))) {
        if (error) *error = QStringLiteral("写入文件失败");
        return false;
    }
    if (!commitAll(repoPath, QStringLiteral("feature: 改问候语"), error)) {
        return false;
    }
    if (!runGit(repoPath, {QStringLiteral("checkout"), QStringLiteral("main")},
                nullptr, error)) {
        return false;
    }
    if (!writeFile(repoPath + QStringLiteral("/app.js"),
                   QStringLiteral("const greeting = \"main 分支的说法\";\n"
                                  "module.exports = greeting;\n"))) {
        if (error) *error = QStringLiteral("写入文件失败");
        return false;
    }
    if (!commitAll(repoPath, QStringLiteral("main: 改问候语"), error)) {
        return false;
    }
    // A conflicted merge is the sandbox's starting state, so a failing exit
    // code here is the expected outcome, not an error.
    if (!runGitExpectFailure(repoPath,
                             {QStringLiteral("merge"), QStringLiteral("feature/conflict")})) {
        if (error) *error = QStringLiteral("未能制造合并冲突");
        return false;
    }
    if (!mergeInProgress(repoPath)) {
        if (error) *error = QStringLiteral("合并未进入冲突状态");
        return false;
    }
    return true;
}

bool generatePartialStaging(const QString &repoPath, QString *error) {
    if (!initSandbox(repoPath, error)) {
        return false;
    }
    if (!writeFile(repoPath + QStringLiteral("/alpha.txt"),
                   QStringLiteral("alpha 原始内容\n")) ||
        !writeFile(repoPath + QStringLiteral("/beta.txt"),
                   QStringLiteral("beta 原始内容\n")) ||
        !writeFile(repoPath + QStringLiteral("/README.md"),
                   QStringLiteral("# 部分暂存练习\n"))) {
        if (error) *error = QStringLiteral("写入文件失败");
        return false;
    }
    if (!commitAll(repoPath, QStringLiteral("init: 两个文件"), error)) {
        return false;
    }
    // Both files modified, nothing staged: the student must stage exactly one.
    if (!writeFile(repoPath + QStringLiteral("/alpha.txt"),
                   QStringLiteral("alpha 第一次修改\n")) ||
        !writeFile(repoPath + QStringLiteral("/beta.txt"),
                   QStringLiteral("beta 第一次修改\n"))) {
        if (error) *error = QStringLiteral("写入文件失败");
        return false;
    }
    return true;
}

bool generateRevertCommit(const QString &repoPath, QString *error) {
    if (!initSandbox(repoPath, error)) {
        return false;
    }
    if (!writeFile(repoPath + QStringLiteral("/config.txt"),
                   QStringLiteral("timeout=30\nretries=3\n"))) {
        if (error) *error = QStringLiteral("写入文件失败");
        return false;
    }
    if (!commitAll(repoPath, QStringLiteral("config: 合理默认值"), error)) {
        return false;
    }
    if (!writeFile(repoPath + QStringLiteral("/config.txt"),
                   QStringLiteral("timeout=3\nretries=0\n"))) {
        if (error) *error = QStringLiteral("写入文件失败");
        return false;
    }
    // The mistake the student has to undo without rewriting published history.
    return commitAll(repoPath, QStringLiteral("config: 一次不该上线的调整"), error);
}


bool generateKeepOneSide(const QString &repoPath, QString *error) {
    if (!initSandbox(repoPath, error)) {
        return false;
    }
    if (!writeFile(repoPath + QStringLiteral("/app.js"),
                   QStringLiteral("const who = \"base\";\n"))) {
        if (error) *error = QStringLiteral("写入文件失败");
        return false;
    }
    if (!commitAll(repoPath, QStringLiteral("base: 一行代码"), error)) {
        return false;
    }
    if (!runGit(repoPath, {QStringLiteral("checkout"), QStringLiteral("-b"),
                           QStringLiteral("feature/other")}, nullptr, error)) {
        return false;
    }
    if (!writeFile(repoPath + QStringLiteral("/app.js"),
                   QStringLiteral("const who = \"feature 版本\";\n"))) {
        if (error) *error = QStringLiteral("写入文件失败");
        return false;
    }
    if (!commitAll(repoPath, QStringLiteral("feature: 自己的版本"), error)) {
        return false;
    }
    if (!runGit(repoPath, {QStringLiteral("checkout"), QStringLiteral("main")},
                nullptr, error)) {
        return false;
    }
    if (!writeFile(repoPath + QStringLiteral("/app.js"),
                   QStringLiteral("const who = \"main 版本\";\n"))) {
        if (error) *error = QStringLiteral("写入文件失败");
        return false;
    }
    if (!commitAll(repoPath, QStringLiteral("main: 自己的版本"), error)) {
        return false;
    }
    if (!runGitExpectFailure(repoPath, {QStringLiteral("merge"),
                                        QStringLiteral("feature/other")})) {
        if (error) *error = QStringLiteral("未能制造合并冲突");
        return false;
    }
    return mergeInProgress(repoPath);
}

bool generateMultipleFiles(const QString &repoPath, QString *error) {
    if (!initSandbox(repoPath, error)) {
        return false;
    }
    const QString a = QStringLiteral("alpha = 基准\n");
    const QString b = QStringLiteral("beta = 基准\n");
    if (!writeFile(repoPath + QStringLiteral("/alpha.txt"), a) ||
        !writeFile(repoPath + QStringLiteral("/beta.txt"), b)) {
        if (error) *error = QStringLiteral("写入文件失败");
        return false;
    }
    if (!commitAll(repoPath, QStringLiteral("base: 两个文件"), error)) {
        return false;
    }
    if (!runGit(repoPath, {QStringLiteral("checkout"), QStringLiteral("-b"),
                           QStringLiteral("feature/two-files")}, nullptr, error)) {
        return false;
    }
    if (!writeFile(repoPath + QStringLiteral("/alpha.txt"),
                   QStringLiteral("alpha = 功能分支改的\n")) ||
        !writeFile(repoPath + QStringLiteral("/beta.txt"),
                   QStringLiteral("beta = 功能分支改的\n"))) {
        if (error) *error = QStringLiteral("写入文件失败");
        return false;
    }
    if (!commitAll(repoPath, QStringLiteral("feature: 两个文件都改"), error)) {
        return false;
    }
    if (!runGit(repoPath, {QStringLiteral("checkout"), QStringLiteral("main")},
                nullptr, error)) {
        return false;
    }
    if (!writeFile(repoPath + QStringLiteral("/alpha.txt"),
                   QStringLiteral("alpha = 主线改的\n")) ||
        !writeFile(repoPath + QStringLiteral("/beta.txt"),
                   QStringLiteral("beta = 主线改的\n"))) {
        if (error) *error = QStringLiteral("写入文件失败");
        return false;
    }
    if (!commitAll(repoPath, QStringLiteral("main: 两个文件都改"), error)) {
        return false;
    }
    if (!runGitExpectFailure(repoPath, {QStringLiteral("merge"),
                                        QStringLiteral("feature/two-files")})) {
        if (error) *error = QStringLiteral("未能制造合并冲突");
        return false;
    }
    return mergeInProgress(repoPath);
}

bool generateRebaseConflict(const QString &repoPath, QString *error) {
    if (!initSandbox(repoPath, error)) {
        return false;
    }
    if (!writeFile(repoPath + QStringLiteral("/config.ini"),
                   QStringLiteral("mode = base\n"))) {
        if (error) *error = QStringLiteral("写入文件失败");
        return false;
    }
    if (!commitAll(repoPath, QStringLiteral("base: 配置"), error)) {
        return false;
    }
    const QString baseSha = [&repoPath]() {
        QByteArray out;
        runGit(repoPath, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")},
               &out, nullptr);
        return QString::fromUtf8(out).trimmed();
    }();

    // main moves on...
    if (!writeFile(repoPath + QStringLiteral("/config.ini"),
                   QStringLiteral("mode = main\n"))) {
        if (error) *error = QStringLiteral("写入文件失败");
        return false;
    }
    if (!commitAll(repoPath, QStringLiteral("main: 配置改为 main"), error)) {
        return false;
    }

    // ...and a feature branch, cut from the same base, edits the same line.
    if (!runGit(repoPath, {QStringLiteral("checkout"), QStringLiteral("-b"),
                           QStringLiteral("feature/rebase"),
                           baseSha}, nullptr, error)) {
        return false;
    }
    if (!writeFile(repoPath + QStringLiteral("/config.ini"),
                   QStringLiteral("mode = feature\n"))) {
        if (error) *error = QStringLiteral("写入文件失败");
        return false;
    }
    if (!commitAll(repoPath, QStringLiteral("feature: 配置改为 feature"), error)) {
        return false;
    }
    if (!runGitExpectFailure(repoPath, {QStringLiteral("rebase"),
                                        QStringLiteral("main")})) {
        if (error) *error = QStringLiteral("未能制造 rebase 冲突");
        return false;
    }
    return rebaseInProgress(repoPath);
}

bool generateCherryPickConflict(const QString &repoPath, QString *error) {
    if (!initSandbox(repoPath, error)) {
        return false;
    }
    if (!writeFile(repoPath + QStringLiteral("/version.txt"),
                   QStringLiteral("version = base\n"))) {
        if (error) *error = QStringLiteral("写入文件失败");
        return false;
    }
    if (!commitAll(repoPath, QStringLiteral("base: 版本号"), error)) {
        return false;
    }
    const QString baseSha = [&repoPath]() {
        QByteArray out;
        runGit(repoPath, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")},
               &out, nullptr);
        return QString::fromUtf8(out).trimmed();
    }();

    if (!runGit(repoPath, {QStringLiteral("checkout"), QStringLiteral("-b"),
                           QStringLiteral("feature/fix"), baseSha}, nullptr, error)) {
        return false;
    }
    if (!writeFile(repoPath + QStringLiteral("/version.txt"),
                   QStringLiteral("version = 修复分支\n"))) {
        if (error) *error = QStringLiteral("写入文件失败");
        return false;
    }
    if (!commitAll(repoPath, QStringLiteral("feature: 修复版本号"), error)) {
        return false;
    }
    QByteArray fixShaOut;
    if (!runGit(repoPath, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")},
                &fixShaOut, nullptr)) {
        if (error) *error = QStringLiteral("读取提交失败");
        return false;
    }
    const QString fixSha = QString::fromUtf8(fixShaOut).trimmed();

    if (!runGit(repoPath, {QStringLiteral("checkout"), QStringLiteral("main")},
                nullptr, error)) {
        return false;
    }
    if (!writeFile(repoPath + QStringLiteral("/version.txt"),
                   QStringLiteral("version = 主线\n"))) {
        if (error) *error = QStringLiteral("写入文件失败");
        return false;
    }
    if (!commitAll(repoPath, QStringLiteral("main: 主线版本号"), error)) {
        return false;
    }
    if (!runGitExpectFailure(repoPath, {QStringLiteral("cherry-pick"), fixSha})) {
        if (error) *error = QStringLiteral("未能制造 cherry-pick 冲突");
        return false;
    }
    return cherryPickInProgress(repoPath);
}

bool generateAbortPractice(const QString &repoPath, QString *error) {
    if (!initSandbox(repoPath, error)) {
        return false;
    }
    if (!writeFile(repoPath + QStringLiteral("/app.js"),
                   QStringLiteral("const who = \"base\";\n"))) {
        if (error) *error = QStringLiteral("写入文件失败");
        return false;
    }
    if (!commitAll(repoPath, QStringLiteral("base: 一行代码"), error)) {
        return false;
    }
    if (!writeFile(repoPath + QStringLiteral("/app.js"),
                   QStringLiteral("const who = \"main 版本\";\n"))) {
        if (error) *error = QStringLiteral("写入文件失败");
        return false;
    }
    if (!commitAll(repoPath, QStringLiteral("main: 自己的版本"), error)) {
        return false;
    }
    const QString baseSha = [&repoPath]() {
        QByteArray out;
        runGit(repoPath, {QStringLiteral("rev-parse"), QStringLiteral("HEAD~1")},
               &out, nullptr);
        return QString::fromUtf8(out).trimmed();
    }();
    if (!runGit(repoPath, {QStringLiteral("checkout"), QStringLiteral("-b"),
                           QStringLiteral("feature/unwanted"), baseSha},
                nullptr, error)) {
        return false;
    }
    if (!writeFile(repoPath + QStringLiteral("/app.js"),
                   QStringLiteral("const who = \"feature 版本\";\n"))) {
        if (error) *error = QStringLiteral("写入文件失败");
        return false;
    }
    if (!commitAll(repoPath, QStringLiteral("feature: 不该合的改动"), error)) {
        return false;
    }
    if (!runGit(repoPath, {QStringLiteral("checkout"), QStringLiteral("main")},
                nullptr, error)) {
        return false;
    }
    if (!runGitExpectFailure(repoPath, {QStringLiteral("merge"),
                                        QStringLiteral("feature/unwanted")})) {
        if (error) *error = QStringLiteral("未能制造合并冲突");
        return false;
    }
    return mergeInProgress(repoPath);
}

}  // namespace

QVector<PracticeDefinition> PracticeService::catalog() {
    const QString basic = QStringLiteral("基础");
    const QString conflict = QStringLiteral("冲突");
    return {
        // ---- chapter: 基础 ----
        {QStringLiteral("first-commit"),
         QStringLiteral("第一次提交"),
         QStringLiteral("README.md 已经暂存，请完成这个仓库的第一次提交。"),
         QStringLiteral("入门"),
         QStringLiteral("提交说明写清楚「做了什么」，而不是「改了文件」。"),
         basic},
        {QStringLiteral("branch-and-merge"),
         QStringLiteral("创建分支并合并"),
         QStringLiteral("把 feature/hello 合并进 main，并让工作区保持干净。"),
         QStringLiteral("入门"),
         QStringLiteral("在 main 上执行合并；合并成功后工作区应无改动。"),
         basic},
        {QStringLiteral("partial-staging"),
         QStringLiteral("部分暂存"),
         QStringLiteral("alpha.txt 和 beta.txt 都改了，只把其中一个加入暂存区。"),
         QStringLiteral("进阶"),
         QStringLiteral("在文件列表里只勾选/暂存一个文件即可。"),
         basic},
        {QStringLiteral("revert-a-commit"),
         QStringLiteral("撤销一次提交"),
         QStringLiteral("最近一次提交把配置改坏了，请撤销它，但不要改写历史。"),
         QStringLiteral("进阶"),
         QStringLiteral("用 revert 生成一个反向提交，而不是 reset。"),
         basic},

        // ---- chapter: 冲突 ----
        {QStringLiteral("conflict-keep-one-side"),
         QStringLiteral("冲突：保留一边"),
         QStringLiteral("app.js 冲突了，这次只保留 main 的版本，并完成合并。"),
         QStringLiteral("入门"),
         QStringLiteral("点冲突提示里的「解决冲突」可以看到 Base/Ours/Theirs 三栏对比，"
                        "先看清两边各自改了什么，再删掉不需要的一侧。"),
         conflict},
        {QStringLiteral("resolve-merge-conflict"),
         QStringLiteral("冲突：保留两边的意图"),
         QStringLiteral("app.js 存在真实冲突：保留两边的意图并完成这次合并。"),
         QStringLiteral("进阶"),
         QStringLiteral("对比 Base 就能看出两边各改了什么；目标是让两条改动都活下来，"
                        "而不是二选一。解决后记得暂存再完成合并。"),
         conflict},
        {QStringLiteral("conflict-multiple-files"),
         QStringLiteral("冲突：多个文件"),
         QStringLiteral("两个文件同时冲突：每个都只保留主线一侧，别漏文件。"),
         QStringLiteral("进阶"),
         QStringLiteral("漏掉任何一个文件，合并都无法完成。"),
         conflict},
        {QStringLiteral("conflict-rebase"),
         QStringLiteral("冲突：rebase 过程中"),
         QStringLiteral("rebase 停在冲突处：解决 config.ini 后让 rebase 继续完成。"),
         QStringLiteral("进阶"),
         QStringLiteral("解决并暂存后执行 rebase --continue。"),
         conflict},
        {QStringLiteral("conflict-cherry-pick"),
         QStringLiteral("冲突：cherry-pick 过程中"),
         QStringLiteral("cherry-pick 停在冲突处：解决 version.txt 后让操作继续。"),
         QStringLiteral("进阶"),
         QStringLiteral("解决并暂存后执行 cherry-pick --continue。"),
         conflict},
        {QStringLiteral("conflict-abort"),
         QStringLiteral("冲突：判断该放弃"),
         QStringLiteral("这次合并本来就该放弃：让仓库回到合并前的干净状态。"),
         QStringLiteral("进阶"),
         QStringLiteral("放弃不会留下合并提交，历史应停在合并之前。"),
         conflict},
    };
}

PracticeDefinition PracticeService::definition(const QString &id) {
    for (const PracticeDefinition &item : catalog()) {
        if (item.id == id) {
            return item;
        }
    }
    return {};
}

bool PracticeService::generate(const QString &id,
                               const QString &parentDir,
                               QString *repoPath,
                               QString *error) {
    if (repoPath) {
        repoPath->clear();
    }
    if (parentDir.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("练习目录不能为空");
        return false;
    }
    if (definition(id).id.isEmpty()) {
        if (error) *error = QStringLiteral("未知的练习：%1").arg(id);
        return false;
    }

    const QString target = QDir(parentDir).absoluteFilePath(id);
    // A sandbox is disposable: an existing copy is replaced so a student can
    // restart from the initial state.
    QDir(target).removeRecursively();
    if (!QDir().mkpath(target)) {
        if (error) *error = QStringLiteral("无法创建练习目录：%1").arg(target);
        return false;
    }

    bool ok = false;
    if (id == QStringLiteral("first-commit")) {
        ok = generateFirstCommit(target, error);
    } else if (id == QStringLiteral("branch-and-merge")) {
        ok = generateBranchMerge(target, error);
    } else if (id == QStringLiteral("resolve-merge-conflict")) {
        ok = generateResolveConflict(target, error);
    } else if (id == QStringLiteral("partial-staging")) {
        ok = generatePartialStaging(target, error);
    } else if (id == QStringLiteral("revert-a-commit")) {
        ok = generateRevertCommit(target, error);
    } else if (id == QStringLiteral("conflict-keep-one-side")) {
        ok = generateKeepOneSide(target, error);
    } else if (id == QStringLiteral("conflict-multiple-files")) {
        ok = generateMultipleFiles(target, error);
    } else if (id == QStringLiteral("conflict-rebase")) {
        ok = generateRebaseConflict(target, error);
    } else if (id == QStringLiteral("conflict-cherry-pick")) {
        ok = generateCherryPickConflict(target, error);
    } else if (id == QStringLiteral("conflict-abort")) {
        ok = generateAbortPractice(target, error);
    }

    if (!ok) {
        return false;
    }
    if (repoPath) {
        *repoPath = QDir::toNativeSeparators(target);
    }
    return true;
}

PracticeCheck PracticeService::check(const QString &id, const QString &repoPath) {
    PracticeCheck result;

    // Every practice ends with a clean working tree except the ones whose
    // goal is explicitly about staged or unstaged content.
    if (id == QStringLiteral("first-commit")) {
        const int count = commitCount(repoPath);
        if (count < 1) {
            result.detail = QStringLiteral("还没有提交：仓库里至少有 1 个提交才算完成。");
            return result;
        }
        if (!isClean(repoPath)) {
            result.detail = QStringLiteral("提交之后工作区又出现了改动，请把剩下的内容也提交。");
            return result;
        }
        result.passed = true;
        result.detail = QStringLiteral("完成：仓库已有 %1 个提交，工作区干净。").arg(count);
        return result;
    }

    if (id == QStringLiteral("branch-and-merge")) {
        QByteArray merged;
        if (!runGit(repoPath, {QStringLiteral("branch"), QStringLiteral("--merged"),
                               QStringLiteral("main")}, &merged, nullptr)) {
            result.detail = QStringLiteral("读不到合并状态，请确认这是本练习的仓库。");
            return result;
        }
        bool found = false;
        for (const QString &name : lines(merged)) {
            if (name.endsWith(QStringLiteral("feature/hello"))) {
                found = true;
                break;
            }
        }
        if (!found) {
            result.detail = QStringLiteral("feature/hello 还没有合并进 main。");
            return result;
        }
        if (!isClean(repoPath)) {
            result.detail = QStringLiteral("合并已进入 main，但工作区还有未提交改动。");
            return result;
        }
        result.passed = true;
        result.detail = QStringLiteral("完成：feature/hello 已合并进 main。");
        return result;
    }

    if (id == QStringLiteral("resolve-merge-conflict")) {
        if (mergeInProgress(repoPath)) {
            result.detail = QStringLiteral("合并尚未完成：仍有冲突没解决或没暂存。");
            return result;
        }
        const QString content = readFile(repoPath + QStringLiteral("/app.js"));
        if (content.isEmpty()) {
            result.detail = QStringLiteral("找不到 app.js，请确认这是练习仓库。");
            return result;
        }
        const bool hasMain = content.contains(QStringLiteral("main 分支的说法"));
        const bool hasFeature = content.contains(QStringLiteral("feature 分支的说法"));
        if (!hasMain || !hasFeature) {
            result.detail = QStringLiteral("app.js 应同时体现两边的说法，当前缺了%1。")
                                .arg(!hasMain ? QStringLiteral(" main 一侧")
                                              : QStringLiteral(" feature 一侧"));
            return result;
        }
        if (!isClean(repoPath)) {
            result.detail = QStringLiteral("冲突已解决，但还有未提交的改动。");
            return result;
        }
        result.passed = true;
        result.detail = QStringLiteral("完成：冲突已解决，合并已提交。");
        return result;
    }

    if (id == QStringLiteral("partial-staging")) {
        QByteArray staged;
        QByteArray unstaged;
        if (!runGit(repoPath, {QStringLiteral("diff"), QStringLiteral("--cached"),
                               QStringLiteral("--name-only")}, &staged, nullptr) ||
            !runGit(repoPath, {QStringLiteral("diff"), QStringLiteral("--name-only")},
                    &unstaged, nullptr)) {
            result.detail = QStringLiteral("读不到暂存状态，请确认这是练习仓库。");
            return result;
        }
        const QStringList stagedFiles = lines(staged);
        const QStringList unstagedFiles = lines(unstaged);
        if (stagedFiles.size() != 1) {
            result.detail = QStringLiteral("暂存区应恰好有 1 个文件，当前有 %1 个。")
                                .arg(stagedFiles.size());
            return result;
        }
        if (unstagedFiles.size() != 1) {
            result.detail = QStringLiteral("工作区应恰好剩 1 个未暂存文件，当前有 %1 个。")
                                .arg(unstagedFiles.size());
            return result;
        }
        result.passed = true;
        result.detail = QStringLiteral("完成：已暂存 %1，%2 仍在工作区。")
                            .arg(stagedFiles.first(), unstagedFiles.first());
        return result;
    }

    if (id == QStringLiteral("revert-a-commit")) {
        QByteArray subject;
        if (!runGit(repoPath, {QStringLiteral("log"), QStringLiteral("-1"),
                               QStringLiteral("--format=%s")}, &subject, nullptr)) {
            result.detail = QStringLiteral("读不到提交历史，请确认这是练习仓库。");
            return result;
        }
        const QString head = QString::fromUtf8(subject).trimmed();
        if (!head.startsWith(QStringLiteral("Revert"))) {
            result.detail = QStringLiteral("最新提交不是 revert 提交（当前：%1）。").arg(head);
            return result;
        }
        if (!isClean(repoPath)) {
            result.detail = QStringLiteral("revert 之后工作区应保持干净。");
            return result;
        }
        const QString config = readFile(repoPath + QStringLiteral("/config.txt"));
        if (!config.contains(QStringLiteral("timeout=30"))) {
            result.detail = QStringLiteral("config.txt 还没回到被改坏之前的内容。");
            return result;
        }
        result.passed = true;
        result.detail = QStringLiteral("完成：已用 revert 撤销，历史未被改写。");
        return result;
    }


    if (id == QStringLiteral("conflict-keep-one-side")) {
        if (mergeInProgress(repoPath)) {
            result.detail = QStringLiteral("合并尚未完成：冲突还没解决或没暂存。");
            return result;
        }
        if (!isClean(repoPath)) {
            result.detail = QStringLiteral("还有未提交的改动。");
            return result;
        }
        const QString app = readFile(repoPath + QStringLiteral("/app.js"));
        if (!app.contains(QStringLiteral("main 版本"))) {
            result.detail = QStringLiteral("app.js 应保留 main 的版本。");
            return result;
        }
        if (app.contains(QStringLiteral("feature 版本"))) {
            result.detail = QStringLiteral("本次只保留 main 一侧，feature 的内容应删除。");
            return result;
        }
        result.passed = true;
        result.detail = QStringLiteral("完成：只保留了 main 的版本，合并已提交。");
        return result;
    }

    if (id == QStringLiteral("conflict-multiple-files")) {
        if (mergeInProgress(repoPath)) {
            result.detail = QStringLiteral("合并尚未完成：还有文件带冲突。");
            return result;
        }
        if (!isClean(repoPath)) {
            result.detail = QStringLiteral("还有未提交的改动。");
            return result;
        }
        QStringList wrong;
        for (const QString &name : {QStringLiteral("alpha.txt"), QStringLiteral("beta.txt")}) {
            const QString content = readFile(repoPath + QStringLiteral("/") + name);
            if (!content.contains(QStringLiteral("主线改的"))) {
                wrong.append(name);
            } else if (content.contains(QStringLiteral("功能分支改的"))) {
                wrong.append(name);
            }
        }
        if (!wrong.isEmpty()) {
            result.detail = QStringLiteral("%1 应只保留主线一侧。").arg(wrong.join(QStringLiteral("、")));
            return result;
        }
        result.passed = true;
        result.detail = QStringLiteral("完成：两个文件都只保留主线一侧，合并已提交。");
        return result;
    }

    if (id == QStringLiteral("conflict-rebase")) {
        if (rebaseInProgress(repoPath)) {
            result.detail = QStringLiteral("rebase 还在进行中：解决冲突后需要 rebase --continue。");
            return result;
        }
        if (!isClean(repoPath)) {
            result.detail = QStringLiteral("还有未提交的改动。");
            return result;
        }
        QByteArray branchOut;
        if (!runGit(repoPath, {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"),
                               QStringLiteral("HEAD")}, &branchOut, nullptr)) {
            result.detail = QStringLiteral("读不到当前分支，请确认这是练习仓库。");
            return result;
        }
        const QString branch = QString::fromUtf8(branchOut).trimmed();
        if (branch != QStringLiteral("feature/rebase")) {
            result.detail = QStringLiteral("当前应在 feature/rebase 上，实际在 %1。").arg(branch);
            return result;
        }
        // --is-ancestor exits 0 when main IS an ancestor of HEAD, which is
        // exactly what a finished rebase produces; a non-zero exit means the
        // branch never moved onto main.
        if (!runGit(repoPath, {QStringLiteral("merge-base"),
                               QStringLiteral("--is-ancestor"),
                               QStringLiteral("main"), QStringLiteral("HEAD")},
                    nullptr, nullptr)) {
            result.detail = QStringLiteral("rebase 没有真正落到 main 之上。");
            return result;
        }
        const QString config = readFile(repoPath + QStringLiteral("/config.ini"));
        if (!config.contains(QStringLiteral("main")) ||
            !config.contains(QStringLiteral("feature"))) {
            result.detail = QStringLiteral("config.ini 要同时体现两边的内容。");
            return result;
        }
        result.passed = true;
        result.detail = QStringLiteral("完成：rebase 已完成，历史线性且保留了两边内容。");
        return result;
    }

    if (id == QStringLiteral("conflict-cherry-pick")) {
        if (cherryPickInProgress(repoPath)) {
            result.detail = QStringLiteral("cherry-pick 还在进行中：解决后需要 --continue。");
            return result;
        }
        if (!isClean(repoPath)) {
            result.detail = QStringLiteral("还有未提交的改动。");
            return result;
        }
        const QString version = readFile(repoPath + QStringLiteral("/version.txt"));
        if (!version.contains(QStringLiteral("主线")) ||
            !version.contains(QStringLiteral("修复分支"))) {
            result.detail = QStringLiteral("version.txt 要同时体现两边的内容。");
            return result;
        }
        result.passed = true;
        result.detail = QStringLiteral("完成：cherry-pick 已提交，两边内容都在。");
        return result;
    }

    if (id == QStringLiteral("conflict-abort")) {
        if (mergeInProgress(repoPath)) {
            result.detail = QStringLiteral("合并仍在进行：这次应当放弃它。");
            return result;
        }
        if (!isClean(repoPath)) {
            result.detail = QStringLiteral("工作区还没回到干净状态。");
            return result;
        }
        const int count = commitCount(repoPath);
        if (count != 2) {
            result.detail = QStringLiteral("历史应停在合并之前（2 个提交），当前 %1 个："
                                           "放弃合并不应产生新的提交。").arg(count);
            return result;
        }
        const QString app = readFile(repoPath + QStringLiteral("/app.js"));
        if (app.contains(QStringLiteral("feature 版本"))) {
            result.detail = QStringLiteral("app.js 里仍有 feature 的内容，改动没有被真正丢弃。");
            return result;
        }
        result.passed = true;
        result.detail = QStringLiteral("完成：合并已放弃，仓库回到合并前的状态。");
        return result;
    }

    result.detail = QStringLiteral("未知的练习：%1").arg(id);
    return result;
}
