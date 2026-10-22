// v4-P3-5 Stash Management: list / create / apply / pop / drop stashes.
//   - list:   git log -g refs/stash --format=%gd%x09%gs%x09%cI
//             (%gd = stash@{N}, %gs = "WIP on <branch>: <subject>",
//             %cI = stash commit date in ISO format). `git stash list`
//             itself carries no date, so the reflog is used; the parser
//             also accepts plain `git stash list` lines.
//   - create: git stash push [-m <message>]
//   - apply:  git stash apply <id>      (keeps the stash)
//   - pop:    git stash pop <id>        (applies and drops)
//   - drop:   git stash drop <id>
#ifndef GITSTASHSERVICE_H
#define GITSTASHSERVICE_H

#include <QObject>
#include <QString>
#include <QList>
#include <QByteArray>
#include <functional>

class GitCommandExecutor;

struct GitStash {
    QString id;        // stash@{N}
    QString message;   // subject (without the hash prefix)
    QString branch;    // branch the stash was created on
    QString date;      // ISO date of the stash commit
};

class GitStashService : public QObject {
    Q_OBJECT
public:
    struct ListResult {
        bool success = false;
        QList<GitStash> stashes;
        QString error;
    };
    struct OperationResult {
        bool success = false;
        QString error;
    };
    using OperationCompletion = std::function<void(OperationResult)>;

    explicit GitStashService(QObject *parent = nullptr);

    void setWorkingDirectory(const QString &path);
    QString workingDirectory() const;

    // Synchronous list (used by the widget and tests). Empty stash list
    // yields success=true with an empty list.
    ListResult listStashes();

    // git stash push [-m <message>]. Empty message = default subject.
    void createStashAsync(const QString &message, OperationCompletion completion);

    // git stash apply <id> (keeps the stash).
    void applyStashAsync(const QString &id, OperationCompletion completion);

    // git stash pop <id> (applies and drops).
    void popStashAsync(const QString &id, OperationCompletion completion);

    // git stash drop <id>.
    void dropStashAsync(const QString &id, OperationCompletion completion);

    // Pure parser for `git log -g refs/stash --format=%gd%x09%gs%x09%cI`
    // output (also tolerates plain `git stash list` lines). Exposed for
    // unit testing without git.
    static QList<GitStash> parseList(const QByteArray &output);

private:
    GitCommandExecutor *m_executor;
};

#endif // GITSTASHSERVICE_H
