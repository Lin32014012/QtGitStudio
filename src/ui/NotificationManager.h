// v4-P6-1 UI/UX Polish: unified notification + busy-state manager.
//   - notify(): single channel for Success / Info / Warning / Error
//     messages. The manager only EMITS a signal - the single
//     presentation point (MainWindow) renders it, so no module keeps
//     its own notification style.
//   - beginBusy()/endBusy(): a uniform busy state for operation buttons
//     that also disables the button, preventing double-clicks while an
//     operation is running.
//   - emptyStateText(): uniform "reason + next step" text for empty
//     states (commit graph, remotes, releases, repositories).
#ifndef NOTIFICATIONMANAGER_H
#define NOTIFICATIONMANAGER_H

#include <QObject>
#include <QString>
#include <QHash>

class QPushButton;

class NotificationManager : public QObject {
    Q_OBJECT
public:
    enum class Level { Success, Info, Warning, Error };

    explicit NotificationManager(QObject *parent = nullptr);

    // Emit a notification. The receiver (MainWindow) decides how to
    // present it (QMessageBox etc.) - this class never pops UI itself.
    void notify(Level level, const QString &title, const QString &message);

    // Mark a button busy: disables it (no double clicks) and shows the
    // busy feedback text. Repeated beginBusy on the same button is a
    // no-op while already busy.
    void beginBusy(QPushButton *button, const QString &busyText = QString());
    void endBusy(QPushButton *button);
    // v4-P6-21R6 P1-04: drop every busy entry owned by the CURRENT
    // session (invoked on repository switch). A stale callback from the
    // old session can then never leave a busy button uncleared, and a
    // callback for the NEW session cannot be cleared by an old token.
    void clearAllBusy();
    bool isBusy(QPushButton *button) const;

    // Uniform empty-state text: "原因" + "下一步建议".
    static QString emptyStateText(const QString &reason,
                                  const QString &suggestion);

    static QString levelName(Level level);

signals:
    void notificationShown(Level level, const QString &title,
                           const QString &message);

private:
    QHash<QPushButton *, bool> m_busyButtons;
};

#endif // NOTIFICATIONMANAGER_H
