#include "UserFacingError.h"

UserFacingError UserFacingError::fromGit(const QString &gitOutput,
                                         const QString &operationName) {
    const QString op =
        operationName.trimmed().isEmpty() ? QStringLiteral("操作") : operationName;
    const QString out = gitOutput.trimmed();

    UserFacingError error;
    error.title = QStringLiteral("%1失败").arg(op);

    const QString lower = out.toLower();
    if (lower.contains(QStringLiteral("not a git repository"))) {
        error.reason = QStringLiteral("当前目录不是 Git 仓库。");
        error.suggestion =
            QStringLiteral("请打开一个 Git 仓库，或先执行“初始化仓库”。");
    } else if (lower.contains(QStringLiteral("conflict"))
               || lower.contains(QStringLiteral("automatic merge failed"))) {
        error.reason = QStringLiteral("操作产生冲突，Git 无法自动合并。");
        error.suggestion =
            QStringLiteral("请手动解决冲突文件并暂存，或放弃本次操作。");
    } else if (lower.contains(QStringLiteral("permission denied"))) {
        error.reason = QStringLiteral("远程服务器拒绝了访问（权限不足）。");
        error.suggestion =
            QStringLiteral("请检查远程地址与凭据（SSH 密钥或访问令牌）。");
    } else if (lower.contains(QStringLiteral("authentication failed"))
               || lower.contains(QStringLiteral("could not read username"))
               || lower.contains(QStringLiteral("could not read password"))) {
        error.reason = QStringLiteral("远程服务器认证失败。");
        error.suggestion =
            QStringLiteral("请检查 GitHub 访问令牌，或重新登录。");
    } else if (lower.contains(QStringLiteral("cannot lock ref"))) {
        error.reason = QStringLiteral("Git 引用被锁定，可能有其他进程正在操作。");
        error.suggestion = QStringLiteral("请稍后重试，或关闭其他 Git 程序。");
    } else if (lower.contains(QStringLiteral("no such file or directory"))
               || lower.contains(QStringLiteral("does not exist"))) {
        error.reason = QStringLiteral("指定的路径或目标不存在。");
        error.suggestion = QStringLiteral("请检查路径、分支或标签名称。");
    } else if (lower.contains(QStringLiteral("not found"))
               || lower.contains(QStringLiteral("did not match"))) {
        error.reason = QStringLiteral("找不到指定的目标（分支、标签或仓库）。");
        error.suggestion = QStringLiteral("请检查名称是否正确，或先拉取最新内容。");
    } else if (lower.contains(QStringLiteral("already exists"))) {
        error.reason = QStringLiteral("同名的目标已经存在。");
        error.suggestion = QStringLiteral("请换一个名称，或先删除已有目标。");
    } else if (lower.contains(QStringLiteral("unable to access"))) {
        error.reason = QStringLiteral("无法连接到远程服务器。");
        error.suggestion = QStringLiteral("请检查网络连接与远程地址。");
    } else {
        error.reason = QStringLiteral("Git 返回了错误。");
        error.suggestion = QStringLiteral("请查看下方错误详情，或检查仓库状态。");
    }

    // Always append the raw git output as detail (not the suggestion).
    if (!out.isEmpty()) {
        error.reason += QStringLiteral("\n\n%1").arg(out.left(400));
    }
    return error;
}

QString UserFacingError::toString() const {
    QString text = title;
    if (!reason.isEmpty()) {
        text += QStringLiteral("\n\n%1").arg(reason);
    }
    if (!suggestion.isEmpty()) {
        text += QStringLiteral("\n\n建议：%1").arg(suggestion);
    }
    return text;
}
