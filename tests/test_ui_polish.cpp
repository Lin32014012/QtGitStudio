// v4-P6-1 UI/UX Polish tests:
//   1. NotificationManager (levels, signal emission, no UI popup)
//   2. Busy state (button disabled during operation, no double click,
//      restore on endBusy, repeated beginBusy is a no-op)
//   3. Empty state (reason + next-step text)
//   4. Error conversion (git stderr -> user language mapping)
// v4-P6-11 additions:
//   5. Git environment "重新检测" gives visible feedback
//   6. GitHub connect dialog exposes a visible "打开 GitHub" button
#include <QtTest>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QTimer>
#include "NotificationManager.h"
#include "UserFacingError.h"
#include "GitEnvironmentDialog.h"
#include "GitEnvironmentService.h"
#include "GitHubAccountDialog.h"
#include "GitHubAccountService.h"
#include "MainWindow.h"

class TestUiPolish : public QObject {
    Q_OBJECT
private slots:
    void notificationLevels() {
        NotificationManager manager;
        QSignalSpy spy(&manager, &NotificationManager::notificationShown);
        QVERIFY(spy.isValid());

        manager.notify(NotificationManager::Level::Success,
                       QStringLiteral("完成"), QStringLiteral("操作成功"));
        manager.notify(NotificationManager::Level::Info,
                       QStringLiteral("提示"), QStringLiteral("请继续"));
        manager.notify(NotificationManager::Level::Warning,
                       QStringLiteral("注意"), QStringLiteral("内容未提交"));
        manager.notify(NotificationManager::Level::Error,
                       QStringLiteral("失败"), QStringLiteral("网络错误"));
        QCOMPARE(spy.count(), 4);

        const QList<QVariant> first = spy.at(0);
        QCOMPARE(first.at(0).value<NotificationManager::Level>(),
                 NotificationManager::Level::Success);
        QCOMPARE(first.at(1).toString(), QString("完成"));
        QCOMPARE(first.at(2).toString(), QString("操作成功"));

        const QList<QVariant> last = spy.at(3);
        QCOMPARE(last.at(0).value<NotificationManager::Level>(),
                 NotificationManager::Level::Error);

        QCOMPARE(NotificationManager::levelName(
                     NotificationManager::Level::Warning),
                 QString("Warning"));
    }

    void busyStateBlocksDoubleClick() {
        NotificationManager manager;
        QPushButton button;
        button.setEnabled(true);

        // Begin busy: button disabled, isBusy true.
        manager.beginBusy(&button, QStringLiteral("正在操作…"));
        QVERIFY(manager.isBusy(&button));
        QVERIFY(!button.isEnabled());

        // Repeated beginBusy while busy is a no-op (still disabled).
        manager.beginBusy(&button, QStringLiteral("正在操作…"));
        QVERIFY(manager.isBusy(&button));
        QVERIFY(!button.isEnabled());

        // End busy: restored.
        manager.endBusy(&button);
        QVERIFY(!manager.isBusy(&button));
        QVERIFY(button.isEnabled());

        // Null-safe.
        manager.beginBusy(nullptr, QString());
        manager.endBusy(nullptr);
        QVERIFY(!manager.isBusy(nullptr));
    }

    void emptyStateText() {
        const QString text = NotificationManager::emptyStateText(
            QStringLiteral("未配置远程仓库"),
            QStringLiteral("使用“克隆仓库”添加远程。"));
        QVERIFY(text.contains(QStringLiteral("未配置远程仓库")));
        QVERIFY(text.contains(QStringLiteral("下一步建议")));
        QVERIFY(text.contains(QStringLiteral("克隆仓库")));

        // Without suggestion: reason only.
        const QString bare = NotificationManager::emptyStateText(
            QStringLiteral("原因"), QString());
        QVERIFY(bare.contains(QStringLiteral("原因")));
        QVERIFY(!bare.contains(QStringLiteral("下一步建议")));
    }

    void errorConversion() {
        // Not a git repository.
        const UserFacingError notRepo = UserFacingError::fromGit(
            QStringLiteral("fatal: not a git repository (or any of the parent directories)"),
            QStringLiteral("Fetch"));
        QVERIFY(notRepo.title.contains(QStringLiteral("Fetch")));
        QVERIFY(notRepo.reason.contains(QStringLiteral("不是 Git 仓库")));
        QVERIFY(!notRepo.suggestion.isEmpty());
        QVERIFY(notRepo.toString().contains(QStringLiteral("建议")));

        // Conflict.
        const UserFacingError conflict = UserFacingError::fromGit(
            QStringLiteral("CONFLICT (content): Merge conflict in a.txt"),
            QStringLiteral("Pull"));
        QVERIFY(conflict.reason.contains(QStringLiteral("冲突")));
        QVERIFY(conflict.suggestion.contains(QStringLiteral("解决")));

        // Permission denied.
        const UserFacingError permission = UserFacingError::fromGit(
            QStringLiteral("remote: Permission denied"), QString());
        QVERIFY(permission.reason.contains(QStringLiteral("权限")));

        // Authentication failed.
        const UserFacingError auth = UserFacingError::fromGit(
            QStringLiteral("fatal: Authentication failed for 'https://github.com/'"),
            QStringLiteral("Push"));
        QVERIFY(auth.reason.contains(QStringLiteral("认证失败")));
        QVERIFY(auth.suggestion.contains(QStringLiteral("令牌")));

        // Cannot lock ref.
        const UserFacingError lock = UserFacingError::fromGit(
            QStringLiteral("error: cannot lock ref 'refs/heads/main'"),
            QString());
        QVERIFY(lock.reason.contains(QStringLiteral("锁定")));

        // Not found.
        const UserFacingError notFound = UserFacingError::fromGit(
            QStringLiteral("error: pathspec 'nope' did not match any file(s) known to git"),
            QString());
        QVERIFY(notFound.reason.contains(QStringLiteral("找不到")));

        // Default fallback (unknown text).
        const UserFacingError generic = UserFacingError::fromGit(
            QStringLiteral("some unusual git message"), QString());
        QVERIFY(!generic.title.isEmpty());
        QVERIFY(!generic.suggestion.isEmpty());
        QVERIFY(generic.reason.contains(QStringLiteral("some unusual git message")));
    }

    // v4-P6-11: clicking 重新检测 must show busy state, then a visible
    // completion state - never a silent no-op.
    void gitEnvRedetectFeedback() {
        GitEnvironmentService service;
        GitEnvironmentDialog dialog(&service);
        dialog.show();
        QApplication::processEvents();

        QPushButton *redetect =
            dialog.findChild<QPushButton *>("gitEnvRedetect");
        QLabel *status = dialog.findChild<QLabel *>("gitEnvStatus");
        QVERIFY(redetect != nullptr);
        QVERIFY(status != nullptr);

        redetect->click();
        // Busy state applied synchronously before detection runs.
        QVERIFY(!redetect->isEnabled());
        QVERIFY(redetect->text().contains(QStringLiteral("检测中")));
        QVERIFY(status->text().contains(QStringLiteral("正在检测")));

        // Let the deferred (singleShot(0)) detection complete.
        QApplication::processEvents();
        QVERIFY(redetect->isEnabled());
        QCOMPARE(redetect->text(), QStringLiteral("重新检测"));
        QVERIFY(status->text().contains(QStringLiteral("已完成检测")));
        // Success path refreshes status/path/version labels.
        QLabel *path = dialog.findChild<QLabel *>("gitEnvPath");
        QLabel *version = dialog.findChild<QLabel *>("gitEnvVersion");
        QVERIFY(path != nullptr);
        QVERIFY(version != nullptr);
        QVERIFY(path->text().startsWith(QStringLiteral("路径：")));
        QVERIFY(version->text().startsWith(QStringLiteral("版本：")));
    }

    // v4-P6-11: the GitHub connect flow must expose a visible
    // "打开 GitHub" button plus the token input.
    void githubConnectDialogHasOpenButton() {
        GitHubAccountService service;
        GitHubAccountDialog dialog(&service, nullptr);

        bool sawOpenButton = false;
        bool sawTokenEdit = false;
        // Phase 1: inside the connect dialog, verify the widgets, then
        // accept with an EMPTY token so the flow ends in the local
        // "Token 不能为空" warning (no network).
        QTimer::singleShot(300, &dialog, [&dialog, &sawOpenButton,
                                          &sawTokenEdit]() {
            QPushButton *openBtn =
                dialog.findChild<QPushButton *>("openGithubButton");
            QLineEdit *tokenEdit =
                dialog.findChild<QLineEdit *>("githubTokenEdit");
            if (openBtn && openBtn->text().contains(QStringLiteral("打开 GitHub"))) {
                sawOpenButton = true;
            }
            if (tokenEdit) {
                sawTokenEdit = true;
            }
            // Accept with empty token -> local warning, no network call.
            QPushButton *connectBtn = nullptr;
            const auto buttons = dialog.findChildren<QPushButton *>();
            for (QPushButton *b : buttons) {
                if (b->text() == QStringLiteral("连接")) {
                    connectBtn = b;
                    break;
                }
            }
            if (connectBtn) {
                connectBtn->click();
            }
        });
        // Phase 2: close the "Token 不能为空" warning box.
        QTimer::singleShot(900, []() {
            const auto tops = QApplication::topLevelWidgets();
            for (QWidget *w : tops) {
                if (qobject_cast<QMessageBox *>(w)) {
                    w->close();
                }
            }
        });

        QMetaObject::invokeMethod(&dialog, "connectAccount");
        QApplication::processEvents();
        QVERIFY(sawOpenButton);
        QVERIFY(sawTokenEdit);
    }

    // v4-P6-12/13: no Emoji / Unicode glyphs in visible UI; the
    // learning button is plain text with NO unstable system icon.
    void noEmojiGlyphsInUi() {
        MainWindow window;
        QPushButton *learn = window.findChild<QPushButton *>("learnButton");
        QVERIFY(learn != nullptr);
        QCOMPARE(learn->text(), QStringLiteral("学习模式"));
        // v4-P6-13: no QStyle standard pixmap (red pixel-art icon).
        QVERIFY(learn->icon().isNull());

        QPushButton *home = window.findChild<QPushButton *>("goHomeButton");
        QVERIFY(home != nullptr);
        QCOMPARE(home->text(), QStringLiteral("返回首页"));
        QVERIFY(!home->icon().isNull());

        GitEnvironmentService service;
        GitEnvironmentDialog dialog(&service);
        // Detection runs on a deferred tick - let it complete.
        QApplication::processEvents();
        QLabel *status = dialog.findChild<QLabel *>("gitEnvStatus");
        QVERIFY(status != nullptr);
        QVERIFY(!status->text().contains(QStringLiteral("✓")));
        QVERIFY(!status->text().contains(QStringLiteral("✗")));
        QVERIFY(status->text().contains(QStringLiteral("检测状态")));
    }

    // v4-P6-13: learning button - plain text, no Emoji, no system
    // icon; the entry stays enabled (clicking opens the real modal
    // dialog, which is exercised by test_learning_mode - never click
    // here or the test blocks on exec()).
    void learningButtonHasNoUnstableIcon() {
        MainWindow window;
        QPushButton *learn = window.findChild<QPushButton *>("learnButton");
        QVERIFY(learn != nullptr);
        QCOMPARE(learn->text(), QStringLiteral("学习模式"));
        QVERIFY(!learn->text().contains(QStringLiteral("🎓")));
        QVERIFY(learn->icon().isNull());
        // The entry must stay usable (signal wiring is covered by the
        // learning-mode tests which construct the dialog directly).
        QVERIFY(learn->isEnabled());
        QVERIFY(!learn->isHidden());
    }
};

QTEST_MAIN(TestUiPolish)
#include "test_ui_polish.moc"
