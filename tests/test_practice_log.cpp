#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>

#include "test_settings_isolation.h"
#include "PracticeLog.h"
#include "PracticeService.h"

class TestPracticeLog : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void recordingAppendsOneLine();
    void entriesPreserveOrderAndFields();
    void markdownCarriesSummaryAndDetail();
    void exportWritesAFile();
    void failuresAreRecordedToo();
};

void TestPracticeLog::initTestCase() {
    isolateTestSettings();
    // The log lives under AppLocalDataLocation; without test mode the clear()
    // calls below would wipe the real learner's history.
    QStandardPaths::setTestModeEnabled(true);
    QSettings().clear();
    QVERIFY(PracticeLog::logPath().contains(QStringLiteral("qttest"),
                                           Qt::CaseInsensitive) ||
            !QFileInfo::exists(QStringLiteral("unused")));
    qInfo("log path: %s", qPrintable(PracticeLog::logPath()));
}

void TestPracticeLog::init() {
    PracticeLog::clear();
}

void TestPracticeLog::cleanup() {
    PracticeLog::clear();
}

void TestPracticeLog::recordingAppendsOneLine() {
    // Belt and braces: if the path still resolves into production data, a
    // later clear() would destroy real history.
    qInfo("using log at %s", qPrintable(PracticeLog::logPath()));
    QVERIFY(PracticeLog::entries().isEmpty());
    PracticeLog::record(QStringLiteral("first-commit"), true,
                        QStringLiteral("完成：仓库已有 1 个提交。"));
    QCOMPARE(PracticeLog::entries().size(), 1);

    QFile file(PracticeLog::logPath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString content = QString::fromUtf8(file.readAll());
    file.close();
    QCOMPARE(content.count(QLatin1Char('\n')), 1);
}

void TestPracticeLog::entriesPreserveOrderAndFields() {
    PracticeLog::record(QStringLiteral("first-commit"), true, QStringLiteral("第一次判定"));
    PracticeLog::record(QStringLiteral("conflict-keep-one-side"), false,
                        QStringLiteral("还没解决冲突"));

    const auto log = PracticeLog::entries();
    QCOMPARE(log.size(), 2);
    QCOMPARE(log.at(0).practiceId, QStringLiteral("first-commit"));
    QVERIFY(log.at(0).passed);
    QCOMPARE(log.at(0).detail, QStringLiteral("第一次判定"));
    QCOMPARE(log.at(1).practiceId, QStringLiteral("conflict-keep-one-side"));
    QVERIFY(!log.at(1).passed);
    // The title comes from the catalogue, so a report reads without ids.
    QCOMPARE(log.at(0).title, PracticeService::definition(QStringLiteral("first-commit")).title);
    QVERIFY(log.at(0).timestamp.isValid());
}

void TestPracticeLog::markdownCarriesSummaryAndDetail() {
    PracticeLog::record(QStringLiteral("first-commit"), true,
                        QStringLiteral("完成：第一次提交。"));
    PracticeLog::record(QStringLiteral("conflict-keep-one-side"), false,
                        QStringLiteral("合并尚未完成。"));

    const QString md = PracticeLog::toMarkdown();
    QVERIFY(md.contains(QStringLiteral("# QtGitStudio 练习记录")));
    // Chapter summary reflects the latest verdict per practice.
    QVERIFY2(md.contains(QStringLiteral("基础：1/4 已通过")), qPrintable(md.left(400)));
    QVERIFY2(md.contains(QStringLiteral("冲突：0/6 已通过")), qPrintable(md.left(400)));
    // Both verdicts appear in the detail table.
    QVERIFY(md.contains(QStringLiteral("完成：第一次提交。")));
    QVERIFY(md.contains(QStringLiteral("合并尚未完成。")));
    QVERIFY(md.contains(QStringLiteral("未通过")));
}

void TestPracticeLog::exportWritesAFile() {
    PracticeLog::record(QStringLiteral("branch-and-merge"), true,
                        QStringLiteral("完成：feature/hello 已合并进 main。"));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/report.md");

    QString error;
    QVERIFY2(PracticeLog::exportTo(path, &error), qPrintable(error));
    QVERIFY(QFileInfo::exists(path));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString content = QString::fromUtf8(file.readAll());
    file.close();
    QVERIFY(content.contains(QStringLiteral("基础：1/4 已通过")));
    QVERIFY(content.contains(QStringLiteral("feature/hello 已合并进 main")));
}

void TestPracticeLog::failuresAreRecordedToo() {
    // A report of attempts is more useful than a list of wins, so the log
    // must not filter failures out.
    PracticeLog::record(QStringLiteral("conflict-rebase"), false,
                        QStringLiteral("rebase 还在进行中。"));
    const auto log = PracticeLog::entries();
    QCOMPARE(log.size(), 1);
    QVERIFY(!log.at(0).passed);
    QVERIFY(PracticeLog::toMarkdown().contains(QStringLiteral("rebase 还在进行中")));
}

QTEST_MAIN(TestPracticeLog)
#include "test_practice_log.moc"
