// v4-P5-2 Release Notes Generator tests:
//   1. commit parsing (pure parser)
//   2. feature classification (feat:/feature:/add: -> Features)
//   3. fix classification (fix:/bugfix:/repair: -> Bug Fixes)
//   4. markdown generation (grouped sections)
//   5. empty range (success with no commits)
//   6. invalid tag (clean failure)
// Real git for range scenarios; pure functions for parsing/grouping.
#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QEventLoop>
#include "ReleaseNotesService.h"

class TestReleaseNotes : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;

    bool runGit(const QString &dir, const QStringList &args,
                QString *output = nullptr, QString *errOutput = nullptr) {
        QProcess git;
        git.setWorkingDirectory(dir);
        git.start("git", args);
        if (!git.waitForFinished(15000)) return false;
        if (output) *output = QString::fromUtf8(git.readAllStandardOutput());
        if (errOutput) *errOutput = QString::fromUtf8(git.readAllStandardError());
        return git.exitStatus() == QProcess::NormalExit && git.exitCode() == 0;
    }

    QString mkRepo(const QString &tag) {
        const QString path = m_dir.path() + "/" + tag;
        QDir().mkpath(path);
        runGit(path, {"init", "-q", "-b", "main"});
        runGit(path, {"config", "user.name", "Tester"});
        runGit(path, {"config", "user.email", "t@t.local"});
        return path;
    }

    bool writeFile(const QString &dir, const QString &name,
                   const QByteArray &data) {
        QFile f(dir + "/" + name);
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write(data);
        f.close();
        return true;
    }

    bool commitAll(const QString &dir, const QString &message) {
        return runGit(dir, {"add", "-A"})
            && runGit(dir, {"commit", "-q", "-m", message});
    }

    ReleaseNotesResult awaitGenerate(ReleaseNotesService *service,
                                     const QString &oldTag,
                                     const QString &newTag) {
        ReleaseNotesResult out;
        QEventLoop loop;
        bool done = false;
        service->generate(
            oldTag, newTag,
            [&out, &done, &loop](const ReleaseNotesResult &result) {
                out = result;
                done = true;
                if (loop.isRunning()) loop.quit();
            });
        if (!done) {
            QTimer::singleShot(15000, &loop, &QEventLoop::quit);
            loop.exec();
        }
        return out;
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
    }

    void commitParsing() {
        const QList<ReleaseNoteCommit> commits = ReleaseNotesService::parseLog(
            "abc1234\tAlice\tfeat: login page\n"
            "def5678\tBob\tfix: crash on start\n");
        QCOMPARE(commits.size(), 2);
        QCOMPARE(commits.at(0).hash, QString("abc1234"));
        QCOMPARE(commits.at(0).author, QString("Alice"));
        QCOMPARE(commits.at(0).message, QString("feat: login page"));
        QCOMPARE(commits.at(1).hash, QString("def5678"));
        QCOMPARE(commits.at(1).author, QString("Bob"));

        // Empty output -> empty list.
        QVERIFY(ReleaseNotesService::parseLog(QByteArray()).isEmpty());
    }

    void featureClassification() {
        QCOMPARE(ReleaseNotesService::categorize(QStringLiteral("feat: x")),
                 QString("Features"));
        QCOMPARE(ReleaseNotesService::categorize(QStringLiteral("feature: y")),
                 QString("Features"));
        QCOMPARE(ReleaseNotesService::categorize(QStringLiteral("add: z")),
                 QString("Features"));
        // Case-insensitive prefix.
        QCOMPARE(ReleaseNotesService::categorize(QStringLiteral("Feat: X")),
                 QString("Features"));
    }

    void fixClassification() {
        QCOMPARE(ReleaseNotesService::categorize(QStringLiteral("fix: x")),
                 QString("Bug Fixes"));
        QCOMPARE(ReleaseNotesService::categorize(QStringLiteral("bugfix: y")),
                 QString("Bug Fixes"));
        QCOMPARE(ReleaseNotesService::categorize(QStringLiteral("repair: z")),
                 QString("Bug Fixes"));
        // Unrelated prefix -> Changes.
        QCOMPARE(ReleaseNotesService::categorize(QStringLiteral("docs: w")),
                 QString("Changes"));
        QCOMPARE(ReleaseNotesService::categorize(QStringLiteral("random text")),
                 QString("Changes"));
    }

    void markdownGeneration() {
        const QList<ReleaseNoteCommit> commits = {
            {"1111111", "feat: login", "Alice"},
            {"2222222", "add: export", "Bob"},
            {"3333333", "fix: crash", "Carol"},
            {"4444444", "tweak styling", "Dave"},
        };
        const QString md =
            ReleaseNotesService::generateMarkdown(commits);
        // Features section first with the two feature commits.
        const int featPos = md.indexOf(QStringLiteral("## Features"));
        const int fixPos = md.indexOf(QStringLiteral("## Bug Fixes"));
        const int changePos = md.indexOf(QStringLiteral("## Changes"));
        QVERIFY(featPos >= 0);
        QVERIFY(fixPos > featPos);
        QVERIFY(changePos > fixPos);
        QVERIFY(md.contains(QStringLiteral("- 1111111 feat: login (@Alice)")));
        QVERIFY(md.contains(QStringLiteral("- 2222222 add: export (@Bob)")));
        QVERIFY(md.contains(QStringLiteral("- 3333333 fix: crash (@Carol)")));
        QVERIFY(md.contains(QStringLiteral("- 4444444 tweak styling (@Dave)")));

        // Empty list -> short placeholder, no sections.
        const QString emptyMd = ReleaseNotesService::generateMarkdown({});
        QVERIFY(!emptyMd.contains(QStringLiteral("## ")));
        QVERIFY(!emptyMd.trimmed().isEmpty());
    }

    void emptyRange() {
        const QString repo = mkRepo("rn_empty");
        writeFile(repo, "a.txt", "1\n");
        commitAll(repo, "feat: base");
        runGit(repo, {"tag", "v1.0"});
        writeFile(repo, "a.txt", "1\n2\n");
        commitAll(repo, "fix: second");
        runGit(repo, {"tag", "v2.0"});

        ReleaseNotesService service;
        service.setWorkingDirectory(repo);
        // v2.0..v2.0 selects nothing.
        const ReleaseNotesResult result =
            awaitGenerate(&service, "v2.0", "v2.0");
        QVERIFY(result.success);
        QVERIFY(result.commits.isEmpty());
        QVERIFY(!result.markdown.isEmpty());
    }

    void invalidTag() {
        const QString repo = mkRepo("rn_invalid");
        writeFile(repo, "a.txt", "1\n");
        commitAll(repo, "feat: base");
        runGit(repo, {"tag", "v1.0"});

        ReleaseNotesService service;
        service.setWorkingDirectory(repo);
        const ReleaseNotesResult result =
            awaitGenerate(&service, "no-such-tag", "v1.0");
        QVERIFY(!result.success);
        QVERIFY(!result.errorMessage.isEmpty());
        QVERIFY(result.commits.isEmpty());
    }

    void realRangeClassification() {
        const QString repo = mkRepo("rn_real");
        writeFile(repo, "a.txt", "1\n");
        commitAll(repo, "feat: base");
        runGit(repo, {"tag", "v1.0"});
        writeFile(repo, "b.txt", "b\n");
        commitAll(repo, "feat: new page");
        writeFile(repo, "a.txt", "1\n2\n");
        commitAll(repo, "fix: off-by-one");
        writeFile(repo, "c.txt", "c\n");
        commitAll(repo, "refactor: cleanup");
        runGit(repo, {"tag", "v2.0"});

        ReleaseNotesService service;
        service.setWorkingDirectory(repo);
        const ReleaseNotesResult result =
            awaitGenerate(&service, "v1.0", "v2.0");
        QVERIFY(result.success);
        QCOMPARE(result.commits.size(), 3);
        QVERIFY(result.markdown.contains(QStringLiteral("## Features")));
        QVERIFY(result.markdown.contains(QStringLiteral("## Bug Fixes")));
        QVERIFY(result.markdown.contains(QStringLiteral("## Changes")));
        QVERIFY(result.markdown.contains(QStringLiteral("feat: new page")));
        QVERIFY(result.markdown.contains(QStringLiteral("fix: off-by-one")));
        QVERIFY(result.markdown.contains(QStringLiteral("refactor: cleanup")));
    }
};

QTEST_MAIN(TestReleaseNotes)
#include "test_release_notes.moc"
