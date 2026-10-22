#include "GitRemoteService.h"
#include "GitCommandExecutor.h"

GitRemoteService::GitRemoteService(QObject *parent)
    : QObject(parent)
    , m_executor(new GitCommandExecutor(this)) {
}

void GitRemoteService::setWorkingDirectory(const QString &path) {
    m_executor->setWorkingDirectory(path);
}

QString GitRemoteService::workingDirectory() const {
    return m_executor->workingDirectory();
}

QList<GitRemote> GitRemoteService::parseRemotes(const QByteArray &output) const {
    QList<GitRemote> remotes;
    const QStringList lines =
        QString::fromUtf8(output).split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        // Format: name<tab>url<space>(fetch|push)
        const int tab = line.indexOf('\t');
        if (tab < 0) continue;
        const QString name = line.left(tab);
        const QString rest = line.mid(tab + 1);
        const int type = rest.lastIndexOf(" (fetch)");
        const bool isFetch = type >= 0;
        const int typePos = isFetch ? type : rest.lastIndexOf(" (push)");
        if (typePos < 0) continue;
        const QString url = rest.left(typePos);

        bool found = false;
        for (GitRemote &r : remotes) {
            if (r.name == name) {
                if (isFetch) r.fetchUrl = url;
                else r.pushUrl = url;
                found = true;
                break;
            }
        }
        if (!found) {
            GitRemote r;
            r.name = name;
            if (isFetch) r.fetchUrl = url;
            else r.pushUrl = url;
            remotes.append(r);
        }
    }
    return remotes;
}

QList<GitRemote> GitRemoteService::remotesSync() {
    auto result = m_executor->execute("remote", {"-v"});
    if (result.error.hasError()) {
        return {};
    }
    return parseRemotes(result.output);
}

void GitRemoteService::remotesAsync(RemotesCompletion completion) {
    m_executor->executeAsync("remote", {"-v"},
                             [this, completion](GitCommandExecutor::Result result) {
                                 // v4-P6-21 ISSUE-05: a failed `git
                                 // remote -v` must NOT be reported as an
                                 // empty remote list - the caller needs
                                 // to distinguish "no remotes" from
                                 // "remote read failed".
                                 if (result.error.hasError()) {
                                     RemotesResult out;
                                     out.success = false;
                                     out.error = result.error.message;
                                     completion(out);
                                     return;
                                 }
                                 RemotesResult out;
                                 out.success = true;
                                 out.remotes = parseRemotes(result.output);
                                 completion(out);
                             });
}

void GitRemoteService::fetchAsync(OperationCompletion completion) {
    m_executor->executeAsync("fetch", {},
                             [this, completion](GitCommandExecutor::Result result) {
                                 OperationResult out = classify(
                                     result.output, result.error.stderrOutput,
                                     result.exitCode);
                                 completion(out);
                             },
                             60000);
}

void GitRemoteService::pullAsync(OperationCompletion completion) {
    m_executor->executeAsync("pull", {},
                             [this, completion](GitCommandExecutor::Result result) {
                                 OperationResult out = classify(
                                     result.output, result.error.stderrOutput,
                                     result.exitCode);
                                 completion(out);
                             },
                             60000);
}

void GitRemoteService::pushAsync(OperationCompletion completion) {
    m_executor->executeAsync("push", {},
                             [this, completion](GitCommandExecutor::Result result) {
                                 OperationResult out = classify(
                                     result.output, result.error.stderrOutput,
                                     result.exitCode);
                                 completion(out);
                             },
                             60000);
}

GitRemoteService::OperationResult GitRemoteService::classify(
    const QByteArray &output, const QString &stderrOutput, int exitCode) const {
    OperationResult out;
    const QString combined = QString::fromUtf8(output)
        + stderrOutput;
    out.success = (exitCode == 0);
    out.error = combined;
    out.alreadyUpToDate =
        combined.contains("Already up to date")
        || combined.contains("Everything up-to-date")
        || combined.contains("up to date");
    out.conflict =
        combined.contains("CONFLICT") || combined.contains("conflict");
    out.hasRemote = !combined.contains("No remote repository specified")
        && !combined.contains("does not appear to be a git repository")
        && !combined.contains("could not read Username");
    // v4-P1-2.1 error classification (keeps API compatible: new field).
    if (out.success) {
        out.errorKind = ErrorKind::None;
    } else if (!out.hasRemote
               || combined.contains("No such remote")
               || combined.contains("does not appear to be a git repository")) {
        out.errorKind = ErrorKind::NoRemote;
    } else if (combined.contains("Could not resolve host")
               || combined.contains("Failed to connect")
               || combined.contains("unable to access")
               || combined.contains("Could not read from remote")
               || combined.contains("Connection timed out")
               || combined.contains("network is unreachable")) {
        out.errorKind = ErrorKind::Network;
    } else if (combined.contains("Permission denied")
               || combined.contains("Authentication failed")
               || combined.contains("could not read Username")
               || combined.contains("access rights")
               || combined.contains("403") || combined.contains("401")) {
        out.errorKind = ErrorKind::Permission;
    } else {
        out.errorKind = ErrorKind::Other;
    }
    return out;
}
