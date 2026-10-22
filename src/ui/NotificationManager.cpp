#include "NotificationManager.h"
#include "ActionButtonFeedback.h"

#include <QPushButton>

NotificationManager::NotificationManager(QObject *parent)
    : QObject(parent) {
}

void NotificationManager::notify(Level level, const QString &title,
                                 const QString &message) {
    emit notificationShown(level, title, message);
}

void NotificationManager::beginBusy(QPushButton *button,
                                    const QString &busyText) {
    if (!button) return;
    if (m_busyButtons.value(button, false)) {
        // Already busy: keep the disabled state (no double-click).
        return;
    }
    m_busyButtons.insert(button, true);
    if (!busyText.isEmpty()) {
        ActionButtonFeedback::showBusy(button, busyText);
    }
    button->setEnabled(false);
}

void NotificationManager::endBusy(QPushButton *button) {
    if (!button) return;
    m_busyButtons.remove(button);
    ActionButtonFeedback::reset(button);
}

bool NotificationManager::isBusy(QPushButton *button) const {
    return button && m_busyButtons.value(button, false);
}

void NotificationManager::clearAllBusy() {
    // v4-P6-21R6 P1-04: drop the busy map entirely and restore every
    // tracked button to its derived state.
    const auto buttons = m_busyButtons.keys();
    for (QPushButton *b : buttons) {
        ActionButtonFeedback::reset(b);
    }
    m_busyButtons.clear();
}

QString NotificationManager::emptyStateText(const QString &reason,
                                            const QString &suggestion) {
    QString text = reason;
    if (!suggestion.trimmed().isEmpty()) {
        text += QStringLiteral("\n下一步建议：%1").arg(suggestion.trimmed());
    }
    return text;
}

QString NotificationManager::levelName(Level level) {
    switch (level) {
    case Level::Success: return QStringLiteral("Success");
    case Level::Info: return QStringLiteral("Info");
    case Level::Warning: return QStringLiteral("Warning");
    case Level::Error: return QStringLiteral("Error");
    }
    return QStringLiteral("Info");
}
