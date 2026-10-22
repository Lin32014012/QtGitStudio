// v4-P3-4 Tag Management: list / create (lightweight + annotated) /
// delete git tags.
//   - list:  git show-ref --tags -d  (name + target commit; peeled ^{}
//            lines mark annotated tags) + git tag -n20 (annotation text)
//   - create: git tag <name> [<commit>]            (lightweight)
//             git tag -a <name> [<commit>] -m <msg> (annotated)
//   - delete: git tag -d <name>
// Annotated tags are NEVER auto-resolved - the tag object's hash is the
// ref target while the peeled ^{} entry gives the underlying commit.
#ifndef GITTAGSERVICE_H
#define GITTAGSERVICE_H

#include <QObject>
#include <QString>
#include <QList>
#include <QByteArray>
#include <functional>

class GitCommandExecutor;

struct GitTag {
    QString name;
    QString commitHash;   // commit the tag points at (peeled for annotated)
    QString message;      // annotation subject (annotated tags only)
    bool annotated = false;
};

class GitTagService : public QObject {
    Q_OBJECT
public:
    struct ListResult {
        bool success = false;
        QList<GitTag> tags;
        QString error;
    };
    struct OperationResult {
        bool success = false;
        QString error;
    };
    using OperationCompletion = std::function<void(OperationResult)>;

    explicit GitTagService(QObject *parent = nullptr);

    void setWorkingDirectory(const QString &path);
    QString workingDirectory() const;

    // Synchronous list (used by the dialog and tests).
    ListResult listTags();

    // git tag <name> [<target>]  /  git tag -a <name> [<target>] -m <msg>.
    // Empty target = HEAD. Completion is always called once.
    void createTagAsync(const QString &name, const QString &target,
                        const QString &message, bool annotated,
                        OperationCompletion completion);

    // git tag -d <name>.
    void deleteTagAsync(const QString &name, OperationCompletion completion);

    // Pure parser for `git show-ref --tags -d` output. Exposed for unit
    // testing without git. Peeled lines (refs/tags/<name>^{}) upgrade the
    // matching entry to annotated and refine its commit hash.
    static QList<GitTag> parseShowRefs(const QByteArray &output);

    // Merge `git tag -n20` messages into tags by name.
    static void applyTagMessages(QList<GitTag> *tags,
                                 const QByteArray &tagNOutput);

private:
    GitCommandExecutor *m_executor;
};

#endif // GITTAGSERVICE_H
