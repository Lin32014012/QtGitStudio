// v4-P3-4 Tag Management tests:
//   1. repository without tags -> empty list
//   2. create lightweight tag
//   3. create annotated tag
//   4. delete tag
//   5. tag shown in commit graph
// Every scenario uses its own temporary repository; async operations
// are awaited with a QEventLoop.
#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QEventLoop>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include "GitTagService.h"
#include "GitCommitGraphService.h"
#include "CommitGraphWidget.h"

class TestTagManagement : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;

    bool runGit(const QString &dir, const QStringList &args,
                QString *output = nullptr) {
        QProcess git;
        git.setWorkingDirectory(dir);
        git.start("git", args);
        if (!git.waitForFinished(15000)) return false;
        if (output) *output = QString::fromUtf8(git.readAllStandardOutput());
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

    QString headHash(const QString &dir) {
        QString out;
        runGit(dir, {"rev-parse", "HEAD"}, &out);
        return out.trimmed();
    }

    // Await an async tag operation; returns the result.
    GitTagService::OperationResult awaitOperation(
        GitTagService *service,
        std::function<void(GitTagService::OperationCompletion)> launch) {
        GitTagService::OperationResult out;
        QEventLoop loop;
        launch([&out, &loop](GitTagService::OperationResult result) {
            out = result;
            loop.quit();
        });
        QTimer::singleShot(15000, &loop, &QEventLoop::quit);
        loop.exec();
        return out;
    }

private slots:
        void initTestCase() {
        // v4-P6-21R6: user home is a git work tree on this machine - temp-dir
        // fixtures must cap git discovery at %TEMP% or commands resolve into
        // the parent work tree (false-green/empty results).
        qputenv("GIT_CEILING_DIRECTORIES", QDir::tempPath().toUtf8());
        QVERIFY(m_dir.isValid());
    }

    void noTagRepository() {
        const QString repo = mkRepo("tag_none");
        writeFile(repo, "a.txt", "1\n");
        commitAll(repo, "first");

        GitTagService service;
        service.setWorkingDirectory(repo);
        const GitTagService::ListResult result = service.listTags();
        QVERIFY(result.success);
        QVERIFY(result.tags.isEmpty());
    }

    void createLightweightTag() {
        const QString repo = mkRepo("tag_light");
        writeFile(repo, "a.txt", "1\n");
        commitAll(repo, "first");
        const QString head = headHash(repo);

        GitTagService service;
        service.setWorkingDirectory(repo);
        const GitTagService::OperationResult create = awaitOperation(
            &service,
            [&service](GitTagService::OperationCompletion cb) {
                service.createTagAsync(QStringLiteral("v1.0"), QString(),
                                       QString(), false, cb);
            });
        QVERIFY2(create.success, qPrintable(create.error));

        const GitTagService::ListResult list = service.listTags();
        QVERIFY(list.success);
        QCOMPARE(list.tags.size(), 1);
        const GitTag &tag = list.tags.at(0);
        QCOMPARE(tag.name, QString("v1.0"));
        QVERIFY(!tag.annotated);
        QCOMPARE(tag.commitHash, head);
    }

    void createAnnotatedTag() {
        const QString repo = mkRepo("tag_annot");
        writeFile(repo, "a.txt", "1\n");
        commitAll(repo, "first");
        const QString head = headHash(repo);

        GitTagService service;
        service.setWorkingDirectory(repo);
        const GitTagService::OperationResult create = awaitOperation(
            &service,
            [&service](GitTagService::OperationCompletion cb) {
                service.createTagAsync(QStringLiteral("v2.0"), QString(),
                                       QStringLiteral("release notes"), true,
                                       cb);
            });
        QVERIFY2(create.success, qPrintable(create.error));

        const GitTagService::ListResult list = service.listTags();
        QVERIFY(list.success);
        QCOMPARE(list.tags.size(), 1);
        const GitTag &tag = list.tags.at(0);
        QCOMPARE(tag.name, QString("v2.0"));
        QVERIFY(tag.annotated);
        QCOMPARE(tag.commitHash, head);
        QCOMPARE(tag.message, QString("release notes"));
    }

    void deleteTag() {
        const QString repo = mkRepo("tag_del");
        writeFile(repo, "a.txt", "1\n");
        commitAll(repo, "first");

        GitTagService service;
        service.setWorkingDirectory(repo);
        awaitOperation(
            &service,
            [&service](GitTagService::OperationCompletion cb) {
                service.createTagAsync(QStringLiteral("v1.0"), QString(),
                                       QString(), false, cb);
            });
        QCOMPARE(service.listTags().tags.size(), 1);

        const GitTagService::OperationResult del = awaitOperation(
            &service,
            [&service](GitTagService::OperationCompletion cb) {
                service.deleteTagAsync(QStringLiteral("v1.0"), cb);
            });
        QVERIFY2(del.success, qPrintable(del.error));

        const GitTagService::ListResult list = service.listTags();
        QVERIFY(list.success);
        QVERIFY(list.tags.isEmpty());

        // Deleting a missing tag fails cleanly.
        const GitTagService::OperationResult delMissing = awaitOperation(
            &service,
            [&service](GitTagService::OperationCompletion cb) {
                service.deleteTagAsync(QStringLiteral("nope"), cb);
            });
        QVERIFY(!delMissing.success);
        QVERIFY(!delMissing.error.isEmpty());
    }

    void tagShownInCommitGraph() {
        const QString repo = mkRepo("tag_graph");
        writeFile(repo, "a.txt", "1\n");
        commitAll(repo, "first");

        GitTagService service;
        service.setWorkingDirectory(repo);
        const GitTagService::OperationResult create = awaitOperation(
            &service,
            [&service](GitTagService::OperationCompletion cb) {
                service.createTagAsync(QStringLiteral("v1.0"), QString(),
                                       QString(), false, cb);
            });
        QVERIFY2(create.success, qPrintable(create.error));

        // Service pipeline: the tagged commit carries the tag name.
        const GitCommitGraphService::LoadResult result =
            GitCommitGraphService().load(repo);
        QVERIFY(result.success);
        QVERIFY(!result.commits.isEmpty());
        const CommitNode &head = result.commits.at(0);
        QVERIFY(head.tags.contains(QStringLiteral("v1.0")));

        // Widget pipeline: a green tag label is drawn for that commit.
        CommitGraphWidget widget;
        widget.setCommits(result.commits);
        bool tagLabelFound = false;
        const QList<QGraphicsItem *> items = widget.scene()->items();
        for (QGraphicsItem *item : items) {
            auto *text = qgraphicsitem_cast<QGraphicsSimpleTextItem *>(item);
            if (text && text->text() == QStringLiteral("v1.0")) {
                tagLabelFound = true;
                break;
            }
        }
        QVERIFY(tagLabelFound);
    }
};

QTEST_MAIN(TestTagManagement)
#include "test_tag_management.moc"
