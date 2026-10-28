#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QSettings>
#include <QFileInfo>
#include <QDir>

#include "test_settings_isolation.h"
#include "PracticeDialog.h"
#include "PracticeService.h"

class TestPracticeDialog : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void listsBothChaptersAndEveryPractice();
    void selectingPracticeShowsItsGoal();
    void startCreatesSandboxAndAnnouncesIt();
    void checkReportsIncompleteThenComplete();
    void cleanupTestCase();
};

namespace {

int childCount(QTreeWidget *list) {
    int n = 0;
    for (int i = 0; i < list->topLevelItemCount(); ++i) {
        n += list->topLevelItem(i)->childCount();
    }
    return n;
}

}  // namespace

void TestPracticeDialog::initTestCase() {
    isolateTestSettings();
    // A previous run may have left pass marks behind; the assertions below
    // describe behaviour, not history.
    QSettings().clear();
}

void TestPracticeDialog::listsBothChaptersAndEveryPractice() {
    PracticeDialog dialog;
    auto *list = dialog.findChild<QTreeWidget *>(QStringLiteral("practiceList"));
    QVERIFY(list != nullptr);

    // Two chapters, and every catalogue entry appears exactly once.
    QCOMPARE(list->topLevelItemCount(), 2);
    QStringList chapterNames;
    for (int i = 0; i < list->topLevelItemCount(); ++i) {
        chapterNames.append(list->topLevelItem(i)->text(0));
    }
    QVERIFY(chapterNames.contains(QStringLiteral("基础")));
    QVERIFY(chapterNames.contains(QStringLiteral("冲突")));

    QCOMPARE(childCount(list), PracticeService::catalog().size());
}

void TestPracticeDialog::selectingPracticeShowsItsGoal() {
    PracticeDialog dialog;
    const auto target = PracticeService::definition(QStringLiteral("resolve-merge-conflict"));
    QVERIFY(dialog.selectPractice(QStringLiteral("resolve-merge-conflict")));
    QCOMPARE(dialog.selectedPracticeId(), QStringLiteral("resolve-merge-conflict"));

    // The goal text must actually reach the UI, not just the model.
    auto *goalLabel = dialog.findChild<QLabel *>(QStringLiteral("practiceGoal"));
    QVERIFY(goalLabel != nullptr);
    QVERIFY2(goalLabel->text().contains(target.goal),
             qPrintable(goalLabel->text()));
}

void TestPracticeDialog::startCreatesSandboxAndAnnouncesIt() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PracticeDialog dialog;
    dialog.setSandboxRoot(dir.path());
    QSignalSpy spy(&dialog, &PracticeDialog::practiceStarted);

    QVERIFY(dialog.selectPractice(QStringLiteral("conflict-keep-one-side")));
    auto *start = dialog.findChild<QPushButton *>();
    // Click the real button rather than calling the slot, so the wiring is
    // covered too.
    bool clicked = false;
    for (QPushButton *button : dialog.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("开始练习")) {
            button->click();
            clicked = true;
            break;
        }
    }
    QVERIFY2(clicked, "start button not found");
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("conflict-keep-one-side"));

    const QString repoPath = spy.first().at(1).toString();
    QVERIFY(QFileInfo::exists(repoPath + QStringLiteral("/.git")));
    // A conflicted merge is the sandbox's starting state.
    QVERIFY(QFileInfo::exists(repoPath + QStringLiteral("/.git/MERGE_HEAD")));
}

void TestPracticeDialog::checkReportsIncompleteThenComplete() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PracticeDialog dialog;
    dialog.setSandboxRoot(dir.path());
    QSignalSpy spy(&dialog, &PracticeDialog::practiceStarted);

    // Checking before starting must explain what to do, not crash.
    dialog.show();
    QVERIFY(dialog.selectPractice(QStringLiteral("first-commit")));
    QPushButton *start = nullptr;
    QPushButton *check = nullptr;
    for (QPushButton *button : dialog.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("开始练习")) start = button;
        if (button->text() == QStringLiteral("检查完成")) check = button;
    }
    QVERIFY(start && check);
    check->click();  // nothing generated yet
    QVERIFY(spy.isEmpty());

    start->click();
    QCOMPARE(spy.count(), 1);
    const QString repoPath = spy.first().at(1).toString();

    // Prove generation actually succeeded before asserting on the verdict:
    // otherwise a generation failure would look like a missing message.
    auto *result = dialog.findChild<QLabel *>(QStringLiteral("practiceResult"));
    QVERIFY(result != nullptr);
    QVERIFY2(result->text().contains(QStringLiteral("练习仓库已就绪")),
             qPrintable(result->text()));

    // Not done yet. Read the label by name: probing isVisible() is
    // unreliable under the offscreen platform, where showing is async.
    check->click();
    QVERIFY2(result->text().contains(QStringLiteral("还没有提交")),
             qPrintable(result->text()));

    // Do the exercise, then check again.
    QProcess git;
    git.setWorkingDirectory(repoPath);
    git.setProcessEnvironment([repoPath] {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("GIT_CEILING_DIRECTORIES"),
                   QFileInfo(repoPath).absolutePath());
        return env;
    }());
    git.start(QStringLiteral("git"), {QStringLiteral("commit"), QStringLiteral("-m"),
                                     QStringLiteral("练习：第一次提交")});
    QVERIFY(git.waitForFinished(20000));
    QCOMPARE(git.exitCode(), 0);

    check->click();
    QVERIFY2(result->text().contains(QStringLiteral("完成")),
             qPrintable(result->text()));
}

void TestPracticeDialog::cleanupTestCase() {
    // Leave a screenshot for documentation, if a destination was provided.
    const QString out = qEnvironmentVariable("QTGITSTUDIO_PRACTICE_SHOT");
    if (out.isEmpty()) {
        return;
    }
    PracticeDialog dialog;
    dialog.resize(880, 560);
    dialog.show();
    dialog.selectPractice(QStringLiteral("resolve-merge-conflict"));
    QTest::qWait(120);
    QVERIFY(dialog.grab().save(out));
    qInfo("saved practice dialog screenshot to %s", qPrintable(out));
}

QTEST_MAIN(TestPracticeDialog)
#include "test_practice_dialog.moc"
