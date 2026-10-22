// v4-P3-5 Stash Management tests:
//   1. empty stash list
//   2. create stash (working tree becomes clean)
//   3. list parsing (pure parser: WIP on / On / with date)
//   4. apply (changes restored, stash kept)
//   5. pop (changes restored, stash removed)
//   6. drop (stash removed)
// Every scenario uses its own temporary repository.
#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QEventLoop>
#include "GitStashService.h"

class TestStashManagement : public QObject {
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

    QString workingTreeStatus(const QString &dir) {
        QString out;
        runGit(dir, {"status", "--porcelain"}, &out);
        return out.trimmed();
    }

    GitStashService::OperationResult awaitOperation(
        GitStashService *service,
        std::function<void(GitStashService::OperationCompletion)> launch) {
        GitStashService::OperationResult out;
        QEventLoop loop;
        launch([&out, &loop](GitStashService::OperationResult result) {
            out = result;
            loop.quit();
        });
        QTimer::singleShot(15000, &loop, &QEventLoop::quit);
        loop.exec();
        return out;
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
    }

    void emptyStashList() {
        const QString repo = mkRepo("stash_none");
        writeFile(repo, "a.txt", "1\n");
        commitAll(repo, "first");

        GitStashService service;
        service.setWorkingDirectory(repo);
        const GitStashService::ListResult result = service.listStashes();
        QVERIFY(result.success);
        QVERIFY(result.stashes.isEmpty());
    }

    void createStash() {
        const QString repo = mkRepo("stash_create");
        writeFile(repo, "a.txt", "1\n");
        commitAll(repo, "first");
        writeFile(repo, "a.txt", "1\n2\n");
        QVERIFY(!workingTreeStatus(repo).isEmpty());

        GitStashService service;
        service.setWorkingDirectory(repo);
        const GitStashService::OperationResult create = awaitOperation(
            &service,
            [&service](GitStashService::OperationCompletion cb) {
                service.createStashAsync(QStringLiteral("wip work"), cb);
            });
        QVERIFY2(create.success, qPrintable(create.error));

        // Working tree is clean after stashing.
        QVERIFY(workingTreeStatus(repo).isEmpty());

        const GitStashService::ListResult list = service.listStashes();
        QVERIFY(list.success);
        QCOMPARE(list.stashes.size(), 1);
        const GitStash &stash = list.stashes.at(0);
        QCOMPARE(stash.id, QString("stash@{0}"));
        QCOMPARE(stash.message, QString("wip work"));
        QCOMPARE(stash.branch, QString("main"));
        QVERIFY(!stash.date.isEmpty());
    }

    void listParsing() {
        // Tab-separated reflog format (id / refs / ISO date).
        const QList<GitStash> a = GitStashService::parseList(
            "stash@{0}\tWIP on main: c3b84cb first work\t2026-08-04T20:48:17+08:00\n"
            "stash@{1}\tOn feature/x: custom message\t2026-08-03T10:00:00+08:00\n");
        QCOMPARE(a.size(), 2);
        QCOMPARE(a.at(0).id, QString("stash@{0}"));
        QCOMPARE(a.at(0).branch, QString("main"));
        QCOMPARE(a.at(0).message, QString("first work"));
        QCOMPARE(a.at(0).date, QString("2026-08-04T20:48:17+08:00"));
        QCOMPARE(a.at(1).id, QString("stash@{1}"));
        QCOMPARE(a.at(1).branch, QString("feature/x"));
        QCOMPARE(a.at(1).message, QString("custom message"));

        // Plain `git stash list` lines (no date column).
        const QList<GitStash> b = GitStashService::parseList(
            "stash@{0}: WIP on main: c3b84cb first work\n"
            "stash@{1}: On main: custom message\n");
        QCOMPARE(b.size(), 2);
        QCOMPARE(b.at(0).message, QString("first work"));
        QCOMPARE(b.at(1).branch, QString("main"));
        QCOMPARE(b.at(1).message, QString("custom message"));
        QVERIFY(b.at(1).date.isEmpty());
    }

    void applyStash() {
        const QString repo = mkRepo("stash_apply");
        writeFile(repo, "a.txt", "1\n");
        commitAll(repo, "first");
        writeFile(repo, "a.txt", "1\n2\n");

        GitStashService service;
        service.setWorkingDirectory(repo);
        awaitOperation(
            &service,
            [&service](GitStashService::OperationCompletion cb) {
                service.createStashAsync(QString(), cb);
            });
        QVERIFY(workingTreeStatus(repo).isEmpty());

        const GitStashService::OperationResult apply = awaitOperation(
            &service,
            [&service](GitStashService::OperationCompletion cb) {
                service.applyStashAsync(QStringLiteral("stash@{0}"), cb);
            });
        QVERIFY2(apply.success, qPrintable(apply.error));

        // Changes are back in the working tree; the stash is KEPT.
        QVERIFY(!workingTreeStatus(repo).isEmpty());
        const GitStashService::ListResult list = service.listStashes();
        QVERIFY(list.success);
        QCOMPARE(list.stashes.size(), 1);
    }

    void popStash() {
        const QString repo = mkRepo("stash_pop");
        writeFile(repo, "a.txt", "1\n");
        commitAll(repo, "first");
        writeFile(repo, "a.txt", "1\n2\n");

        GitStashService service;
        service.setWorkingDirectory(repo);
        awaitOperation(
            &service,
            [&service](GitStashService::OperationCompletion cb) {
                service.createStashAsync(QString(), cb);
            });
        QVERIFY(workingTreeStatus(repo).isEmpty());

        const GitStashService::OperationResult pop = awaitOperation(
            &service,
            [&service](GitStashService::OperationCompletion cb) {
                service.popStashAsync(QStringLiteral("stash@{0}"), cb);
            });
        QVERIFY2(pop.success, qPrintable(pop.error));

        // Changes restored AND the stash is gone.
        QVERIFY(!workingTreeStatus(repo).isEmpty());
        const GitStashService::ListResult list = service.listStashes();
        QVERIFY(list.success);
        QVERIFY(list.stashes.isEmpty());
    }

    void dropStash() {
        const QString repo = mkRepo("stash_drop");
        writeFile(repo, "a.txt", "1\n");
        commitAll(repo, "first");
        writeFile(repo, "a.txt", "1\n2\n");

        GitStashService service;
        service.setWorkingDirectory(repo);
        awaitOperation(
            &service,
            [&service](GitStashService::OperationCompletion cb) {
                service.createStashAsync(QStringLiteral("tmp"), cb);
            });
        QCOMPARE(service.listStashes().stashes.size(), 1);

        const GitStashService::OperationResult drop = awaitOperation(
            &service,
            [&service](GitStashService::OperationCompletion cb) {
                service.dropStashAsync(QStringLiteral("stash@{0}"), cb);
            });
        QVERIFY2(drop.success, qPrintable(drop.error));

        const GitStashService::ListResult list = service.listStashes();
        QVERIFY(list.success);
        QVERIFY(list.stashes.isEmpty());

        // Dropping a missing stash fails cleanly.
        const GitStashService::OperationResult dropMissing = awaitOperation(
            &service,
            [&service](GitStashService::OperationCompletion cb) {
                service.dropStashAsync(QStringLiteral("stash@{0}"), cb);
            });
        QVERIFY(!dropMissing.success);
        QVERIFY(!dropMissing.error.isEmpty());
    }
};

QTEST_MAIN(TestStashManagement)
#include "test_stash_management.moc"
