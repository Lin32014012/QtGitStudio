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

}  // namespace

QVector<PracticeDefinition> PracticeService::catalog() {
    return {
        {QStringLiteral("first-commit"),
         QStringLiteral("第一次提交"),
         QStringLiteral("README.md 已经暂存，请完成这个仓库的第一次提交。"),
         QStringLiteral("入门"),
         QStringLiteral("提交说明写清楚「做了什么」，而不是「改了文件」。")},
        {QStringLiteral("branch-and-merge"),
         QStringLiteral("创建分支并合并"),
         QStringLiteral("把 feature/hello 合并进 main，并让工作区保持干净。"),
         QStringLiteral("入门"),
         QStringLiteral("在 main 上执行合并；合并成功后工作区应无改动。")},
        {QStringLiteral("resolve-merge-conflict"),
         QStringLiteral("解决合并冲突"),
         QStringLiteral("app.js 存在真实冲突：保留两边的意图并完成这次合并。"),
         QStringLiteral("进阶"),
         QStringLiteral("解决后要暂存文件，然后再完成合并提交。")},
        {QStringLiteral("partial-staging"),
         QStringLiteral("部分暂存"),
         QStringLiteral("alpha.txt 和 beta.txt 都改了，只把其中一个加入暂存区。"),
         QStringLiteral("进阶"),
         QStringLiteral("在文件列表里只勾选/暂存一个文件即可。")},
        {QStringLiteral("revert-a-commit"),
         QStringLiteral("撤销一次提交"),
         QStringLiteral("最近一次提交把配置改坏了，请撤销它，但不要改写历史。"),
         QStringLiteral("进阶"),
         QStringLiteral("用 revert 生成一个反向提交，而不是 reset。")},
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

    result.detail = QStringLiteral("未知的练习：%1").arg(id);
    return result;
}
