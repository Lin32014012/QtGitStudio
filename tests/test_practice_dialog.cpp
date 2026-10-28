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
#include <QStandardPaths>
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
    void passingOnePracticeMovesToTheNext();
    void chapterHeaderShowsProgress();
    void sandboxRootFallsBackToTheDefault();
    void storedSandboxRootIsHonouredAndOverridable();
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
    // Verdicts are written to the practice log under AppLocalDataLocation.
    QStandardPaths::setTestModeEnabled(true);
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
    // Headers carry a tally ("基础（0/4）"), so match the chapter name by
    // prefix rather than equality.
    bool hasBasic = false;
    bool hasConflict = false;
    for (int i = 0; i < list->topLevelItemCount(); ++i) {
        const QString text = list->topLevelItem(i)->text(0);
        if (text.startsWith(QStringLiteral("基础"))) hasBasic = true;
        if (text.startsWith(QStringLiteral("冲突"))) hasConflict = true;
    }
    QVERIFY(hasBasic);
    QVERIFY(hasConflict);

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
    QVERIFY2(result->text().contains(QStringLiteral("已通过")),
             qPrintable(result->text()));
}


void TestPracticeDialog::passingOnePracticeMovesToTheNext() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PracticeDialog dialog;
    dialog.setSandboxRoot(dir.path());
    QSignalSpy spy(&dialog, &PracticeDialog::practiceStarted);

    QPushButton *start = nullptr;
    QPushButton *check = nullptr;
    for (QPushButton *button : dialog.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("开始练习")) start = button;
        if (button->text() == QStringLiteral("检查完成")) check = button;
    }
    QVERIFY(start && check);

    QVERIFY(dialog.selectPractice(QStringLiteral("first-commit")));
    start->click();
    QCOMPARE(spy.count(), 1);
    const QString repoPath = spy.first().at(1).toString();

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

    // Selecting the next exercise is how the tool keeps momentum going.
    QCOMPARE(dialog.selectedPracticeId(), QStringLiteral("branch-and-merge"));
    auto *result = dialog.findChild<QLabel *>(QStringLiteral("practiceResult"));
    QVERIFY(result != nullptr);
    QVERIFY2(result->text().contains(QStringLiteral("已通过")),
             qPrintable(result->text()));
    QVERIFY2(result->text().contains(QStringLiteral("下一关")),
             qPrintable(result->text()));
}

void TestPracticeDialog::chapterHeaderShowsProgress() {
    // Earlier cases earn real passes in this same process, so reset the
    // store before asserting on a fresh tally.
    QSettings().clear();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PracticeDialog dialog;
    dialog.setSandboxRoot(dir.path());

    auto *list = dialog.findChild<QTreeWidget *>(QStringLiteral("practiceList"));
    QVERIFY(list != nullptr);

    auto headerText = [list](const QString &chapter) {
        for (int i = 0; i < list->topLevelItemCount(); ++i) {
            if (list->topLevelItem(i)->text(0).startsWith(chapter)) {
                return list->topLevelItem(i)->text(0);
            }
        }
        return QString();
    };

    // Fresh settings: every chapter starts at 0 passed.
    QVERIFY2(headerText(QStringLiteral("基础")).contains(QStringLiteral("（0/4）")),
             qPrintable(headerText(QStringLiteral("基础"))));
    QVERIFY2(headerText(QStringLiteral("冲突")).contains(QStringLiteral("（0/6）")),
             qPrintable(headerText(QStringLiteral("冲突"))));

    // Pass one exercise end to end, then the tally must follow.
    QSignalSpy spy(&dialog, &PracticeDialog::practiceStarted);
    QVERIFY(dialog.selectPractice(QStringLiteral("first-commit")));
    QPushButton *start = nullptr;
    QPushButton *check = nullptr;
    for (QPushButton *button : dialog.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("开始练习")) start = button;
        if (button->text() == QStringLiteral("检查完成")) check = button;
    }
    QVERIFY(start && check);
    start->click();
    const QString repoPath = spy.first().at(1).toString();

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
    QVERIFY2(headerText(QStringLiteral("基础")).contains(QStringLiteral("（1/4）")),
             qPrintable(headerText(QStringLiteral("基础"))));
}


void TestPracticeDialog::sandboxRootFallsBackToTheDefault() {
    QSettings().clear();
    PracticeDialog dialog;
    QCOMPARE(QDir::cleanPath(dialog.sandboxRoot()),
             QDir::cleanPath(PracticeDialog::defaultSandboxRoot()));
    QVERIFY(dialog.sandboxRoot().contains(QStringLiteral("practices")));

    // The location is surfaced in the UI, not just held in a member.
    auto *location = dialog.findChild<QLabel *>(QStringLiteral("practiceLocation"));
    QVERIFY(location != nullptr);
    QCOMPARE(QDir::cleanPath(location->text()), QDir::cleanPath(dialog.sandboxRoot()));
}

void TestPracticeDialog::storedSandboxRootIsHonouredAndOverridable() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // A stored preference wins over the default...
    QSettings().setValue(QStringLiteral("practices/sandboxRoot"), dir.path());
    {
        PracticeDialog dialog;
        QCOMPARE(QDir::cleanPath(dialog.sandboxRoot()), QDir::cleanPath(dir.path()));
    }

    // ...and setSandboxRoot overrides it in memory without rewriting the
    // preference, which is what tests and screenshots rely on.
    QTemporaryDir other;
    QVERIFY(other.isValid());
    {
        PracticeDialog dialog;
        dialog.setSandboxRoot(other.path());
        QCOMPARE(QDir::cleanPath(dialog.sandboxRoot()), QDir::cleanPath(other.path()));
        auto *location = dialog.findChild<QLabel *>(QStringLiteral("practiceLocation"));
        QVERIFY(location != nullptr);
        QCOMPARE(QDir::cleanPath(location->text()), QDir::cleanPath(other.path()));
    }
    QCOMPARE(QSettings().value(QStringLiteral("practices/sandboxRoot")).toString(),
             dir.path());
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
