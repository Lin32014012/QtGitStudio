#include <QtTest>
#include <QTemporaryDir>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QSettings>
#include <QProcess>
#include <QProcessEnvironment>
#include <QFileInfo>

#include "test_settings_isolation.h"
#include "PracticeBanner.h"
#include "PracticeService.h"

class TestPracticeBanner : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void startsInactive();
    void setPracticeShowsGoalAndBecomesActive();
    void checkReportsIncompleteThenPasses();
    void hintToggles();
    void clearResetsEverything();
};

void TestPracticeBanner::initTestCase() {
    isolateTestSettings();
    QSettings().clear();
}

void TestPracticeBanner::startsInactive() {
    PracticeBanner banner;
    QVERIFY(!banner.isActive());
    QVERIFY(!banner.isVisible());
    QVERIFY(banner.practiceId().isEmpty());
}

void TestPracticeBanner::setPracticeShowsGoalAndBecomesActive() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString repo, error;
    QVERIFY2(PracticeService::generate(QStringLiteral("conflict-keep-one-side"),
                                       dir.path(), &repo, &error), qPrintable(error));

    PracticeBanner banner;
    banner.show();
    banner.setPractice(QStringLiteral("conflict-keep-one-side"), repo);
    QVERIFY(banner.isActive());
    QCOMPARE(banner.practiceId(), QStringLiteral("conflict-keep-one-side"));
    QCOMPARE(banner.repositoryPath(), repo);

    const auto definition =
        PracticeService::definition(QStringLiteral("conflict-keep-one-side"));
    auto *title = banner.findChild<QLabel *>(QStringLiteral("practiceBannerTitle"));
    QVERIFY(title != nullptr);
    QVERIFY2(title->text().contains(definition.goal), qPrintable(title->text()));
}

void TestPracticeBanner::checkReportsIncompleteThenPasses() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString repo, error;
    QVERIFY2(PracticeService::generate(QStringLiteral("first-commit"),
                                       dir.path(), &repo, &error), qPrintable(error));

    PracticeBanner banner;
    banner.show();
    banner.setPractice(QStringLiteral("first-commit"), repo);
    QSignalSpy passedSpy(&banner, &PracticeBanner::practicePassed);

    QPushButton *check = nullptr;
    for (QPushButton *button : banner.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("检查完成")) {
            check = button;
        }
    }
    QVERIFY(check != nullptr);

    check->click();
    QVERIFY2(banner.resultText().contains(QStringLiteral("还没有提交")),
             qPrintable(banner.resultText()));
    QCOMPARE(passedSpy.count(), 0);

    // Do the exercise.
    QProcess git;
    git.setWorkingDirectory(repo);
    git.setProcessEnvironment([repo] {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("GIT_CEILING_DIRECTORIES"),
                   QFileInfo(repo).absolutePath());
        return env;
    }());
    git.start(QStringLiteral("git"), {QStringLiteral("commit"), QStringLiteral("-m"),
                                     QStringLiteral("练习：第一次提交")});
    QVERIFY(git.waitForFinished(20000));
    QCOMPARE(git.exitCode(), 0);

    check->click();
    QVERIFY2(banner.resultText().contains(QStringLiteral("完成")),
             qPrintable(banner.resultText()));
    QCOMPARE(passedSpy.count(), 1);
    QCOMPARE(passedSpy.first().first().toString(), QStringLiteral("first-commit"));

    // The pass is remembered for the dialog to show.
    QVERIFY(QSettings().value(QStringLiteral("practices/passed/first-commit"), false).toBool());
}

void TestPracticeBanner::hintToggles() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString repo, error;
    QVERIFY2(PracticeService::generate(QStringLiteral("partial-staging"),
                                       dir.path(), &repo, &error), qPrintable(error));

    PracticeBanner banner;
    banner.show();
    banner.setPractice(QStringLiteral("partial-staging"), repo);
    QVERIFY(banner.resultText().isEmpty());

    QPushButton *hint = nullptr;
    for (QPushButton *button : banner.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("显示提示")) {
            hint = button;
        }
    }
    QVERIFY(hint != nullptr);
    hint->click();
    QVERIFY2(banner.resultText().contains(QStringLiteral("提示")),
             qPrintable(banner.resultText()));
    QCOMPARE(hint->text(), QStringLiteral("隐藏提示"));

    hint->click();
    QCOMPARE(hint->text(), QStringLiteral("显示提示"));
}

void TestPracticeBanner::clearResetsEverything() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString repo, error;
    QVERIFY2(PracticeService::generate(QStringLiteral("first-commit"),
                                       dir.path(), &repo, &error), qPrintable(error));

    PracticeBanner banner;
    banner.show();
    banner.setPractice(QStringLiteral("first-commit"), repo);
    QVERIFY(banner.isActive());

    banner.clear();
    QVERIFY(!banner.isActive());
    QVERIFY(!banner.isVisible());
    QVERIFY(banner.practiceId().isEmpty());
    QVERIFY(banner.repositoryPath().isEmpty());
    QVERIFY(banner.resultText().isEmpty());
}

QTEST_MAIN(TestPracticeBanner)
#include "test_practice_banner.moc"
