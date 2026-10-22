#include "GitTagService.h"
#include "GitCommandExecutor.h"

GitTagService::GitTagService(QObject *parent)
    : QObject(parent)
    , m_executor(new GitCommandExecutor(this)) {
}

void GitTagService::setWorkingDirectory(const QString &path) {
    m_executor->setWorkingDirectory(path);
}

QString GitTagService::workingDirectory() const {
    return m_executor->workingDirectory();
}

QList<GitTag> GitTagService::parseShowRefs(const QByteArray &output) {
    QList<GitTag> tags;
    const QList<QByteArray> lines = output.split('\n');
    for (const QByteArray &line : lines) {
        if (line.isEmpty()) continue;
        // `git show-ref` separates hash and ref with a single space
        // ("<sha1> SP <ref>"); accept a tab too for robustness.
        const int tab = line.indexOf('\t');
        const int sp = line.indexOf(' ');
        int cut = tab;
        if (sp >= 0 && (cut < 0 || sp < cut)) cut = sp;
        if (cut <= 0) continue;
        const QByteArray hash = line.left(cut);
        const QByteArray ref = line.mid(cut + 1).trimmed();
        if (!ref.startsWith("refs/tags/")) continue;
        QByteArray name = ref.mid(10);
        if (name.endsWith("^{}")) {
            // Peeled entry: the tag object's commit target.
            name.chop(3);
            for (GitTag &tag : tags) {
                if (tag.name == QString::fromUtf8(name)) {
                    tag.commitHash = QString::fromUtf8(hash);
                    tag.annotated = true;
                    break;
                }
            }
            continue;
        }
        GitTag tag;
        tag.name = QString::fromUtf8(name);
        tag.commitHash = QString::fromUtf8(hash);
        tags.append(tag);
    }
    return tags;
}

void GitTagService::applyTagMessages(QList<GitTag> *tags,
                                     const QByteArray &tagNOutput) {
    if (!tags) return;
    // `git tag -n20` prints "<name>  <message>" (name is the first
    // whitespace-delimited token; tag names never contain spaces).
    const QList<QByteArray> lines = tagNOutput.split('\n');
    for (const QByteArray &line : lines) {
        if (line.isEmpty()) continue;
        const int firstWs = line.indexOf(' ');
        const int firstTab = line.indexOf('\t');
        int cut = firstTab;
        if (firstWs >= 0 && (cut < 0 || firstWs < cut)) cut = firstWs;
        if (cut < 0) continue;
        const QString name = QString::fromUtf8(line.left(cut));
        const QString message = QString::fromUtf8(line.mid(cut).trimmed());
        for (GitTag &tag : *tags) {
            if (tag.name == name) {
                tag.message = message;
                break;
            }
        }
    }
}

GitTagService::ListResult GitTagService::listTags() {
    ListResult result;
    const GitCommandExecutor::Result showRef =
        m_executor->execute("show-ref", {"--tags", "-d"}, 30000);
    if (showRef.exitCode == 1 && showRef.output.isEmpty()) {
        // No tags exist yet: `git show-ref --tags` exits 1 with no
        // output. That is a valid empty list, not an error.
        result.success = true;
        result.tags.clear();
        return result;
    }
    if (showRef.error.hasError() && showRef.exitCode != 0) {
        // Not a repository or git unavailable.
        result.success = false;
        result.error = QString::fromUtf8(showRef.errorOutput)
            + QString::fromUtf8(showRef.output);
        return result;
    }
    QList<GitTag> tags = parseShowRefs(showRef.output);
    if (tags.isEmpty()) {
        result.success = true;
        result.tags.clear();
        return result;
    }
    const GitCommandExecutor::Result tagN =
        m_executor->execute("tag", {"-n20"}, 30000);
    if (tagN.exitCode == 0) {
        applyTagMessages(&tags, tagN.output);
    }
    result.success = true;
    result.tags = tags;
    return result;
}

void GitTagService::createTagAsync(const QString &name, const QString &target,
                                   const QString &message, bool annotated,
                                   OperationCompletion completion) {
    QStringList args;
    if (annotated) {
        args << "-a" << name;
        if (!target.isEmpty()) {
            args << target;
        }
        args << "-m" << message;
    } else {
        args << name;
        if (!target.isEmpty()) {
            args << target;
        }
    }
    m_executor->executeAsync(
        "tag", args,
        [completion](GitCommandExecutor::Result result) {
            OperationResult out;
            out.success = (result.exitCode == 0);
            if (!out.success) {
                out.error = QString::fromUtf8(result.errorOutput)
                    + QString::fromUtf8(result.output);
            }
            completion(out);
        },
        30000);
}

void GitTagService::deleteTagAsync(const QString &name,
                                   OperationCompletion completion) {
    m_executor->executeAsync(
        "tag", {"-d", name},
        [completion](GitCommandExecutor::Result result) {
            OperationResult out;
            out.success = (result.exitCode == 0);
            if (!out.success) {
                out.error = QString::fromUtf8(result.errorOutput)
                    + QString::fromUtf8(result.output);
            }
            completion(out);
        },
        30000);
}
