// v4-P5-2 Release Notes Generator MVP: build GitHub-style Markdown
// release notes from the commit range oldTag..newTag.
//   - git log --format=%H%x09%an%x09%s <oldTag>..<newTag>
//   - commit messages are classified by prefix:
//       feat:/feature:/add:  -> Features
//       fix:/bugfix:/repair: -> Bug Fixes
//       anything else       -> Changes
// No AI summarization, no translation, no auto-publish.
#ifndef RELEASENOTESSERVICE_H
#define RELEASENOTESSERVICE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QList>
#include <functional>

class GitCommandExecutor;

struct ReleaseNoteCommit {
    QString hash;
    QString message;
    QString author;
};

struct ReleaseNotesResult {
    bool success = false;
    QString markdown;
    QList<ReleaseNoteCommit> commits;
    QString errorMessage;
};

class ReleaseNotesService : public QObject {
    Q_OBJECT
public:
    using NotesCompletion = std::function<void(const ReleaseNotesResult &)>;

    explicit ReleaseNotesService(QObject *parent = nullptr);

    void setWorkingDirectory(const QString &path);
    QString workingDirectory() const;

    // Generate notes for oldTag..newTag. Invalid tags yield a clean
    // failure; an empty range yields success with no commits and a
    // short "no changes" markdown.
    void generate(const QString &oldTag, const QString &newTag,
                  NotesCompletion completion);

    // Pure parser for
    // `git log --format=%H%x09%an%x09%s <old>..<new>` output.
    static QList<ReleaseNoteCommit> parseLog(const QByteArray &output);

    // Classification section for a commit message prefix.
    // Returns "Features", "Bug Fixes" or "Changes".
    static QString categorize(const QString &message);

    // Build the Markdown document from commits (grouped by category).
    static QString generateMarkdown(const QList<ReleaseNoteCommit> &commits);

private:
    GitCommandExecutor *m_executor;
};

#endif // RELEASENOTESSERVICE_H
