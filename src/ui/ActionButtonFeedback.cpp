#include "ActionButtonFeedback.h"

#include <QFontMetrics>
#include <QPointer>
#include <QPushButton>
#include <QSizePolicy>
#include <QStyle>
#include <QTimer>

namespace {
QString buttonStyle(
    const QString &objectName,
    const ActionButtonFeedback::Palette &palette) {
    return QString(
               "QPushButton#%1 { background: %2; color: white; padding: 9px 14px; border: 1px solid transparent; border-radius: 6px; font-size: 14px; font-weight: 600; }"
               "QPushButton#%1:hover { background: %3; }"
               "QPushButton#%1:pressed { background: %4; }"
               "QPushButton#%1:disabled { background: #E5E7EB; color: #9CA3AF; }"
               "QPushButton#%1[busy=\"true\"] { background: #475569; color: white; }"
               "QPushButton#%1[result=\"success\"] { background: #16A34A; color: white; }"
               "QPushButton#%1[result=\"error\"] { background: #DC2626; color: white; }"
               "QPushButton#%1:focus { border-color: #2563EB; outline: none; }")
        .arg(objectName, palette.normal, palette.hover, palette.pressed);
}
}

void ActionButtonFeedback::configure(
    QPushButton *button,
    const Palette &palette,
    const QStringList &feedbackTexts) {
    button->setProperty("normalText", button->text());
    button->setProperty("feedbackToken", 0);
    button->setProperty("feedbackActive", false);
    button->setProperty("busy", false);
    button->setProperty("result", QString());
    button->setProperty("readyWhenIdle", button->isEnabled());
    button->setStyleSheet(buttonStyle(button->objectName(), palette));
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    int widestText = button->fontMetrics().horizontalAdvance(button->text());
    for (const QString &text : feedbackTexts) {
        widestText = qMax(widestText, button->fontMetrics().horizontalAdvance(text));
    }
    button->setMinimumWidth(widestText + 34);
    button->setMinimumHeight(qMax(button->minimumHeight(), 40));
    setReady(button, button->isEnabled());
}

void ActionButtonFeedback::setReady(QPushButton *button, bool ready) {
    if (!button) { return; }  // v4-P6-18 P0-1 null guard
    button->setProperty("readyWhenIdle", ready);
    if (!isActive(button)) {
        button->setEnabled(ready);
        button->setCursor(ready ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
}

void ActionButtonFeedback::showBusy(QPushButton *button, const QString &text) {
    if (!button) { return; }  // v4-P6-18 P0-1 null guard
    const int token = button->property("feedbackToken").toInt() + 1;
    button->setProperty("feedbackToken", token);
    button->setProperty("feedbackActive", true);
    button->setProperty("busy", true);
    button->setProperty("result", QString());
    button->setText(text);
    button->setEnabled(false);
    button->setCursor(Qt::ArrowCursor);
    refreshStyle(button);
}

void ActionButtonFeedback::showSuccess(QPushButton *button, const QString &text, int durationMs) {
    if (!button) { return; }  // v4-P6-18 P0-1 null guard
    showResult(button, text, "success", durationMs);
}

void ActionButtonFeedback::showError(QPushButton *button, const QString &text, int durationMs) {
    if (!button) { return; }  // v4-P6-18 P0-1 null guard
    showResult(button, text, "error", durationMs);
}

void ActionButtonFeedback::reset(QPushButton *button) {
    if (!button) { return; }  // v4-P6-18 P0-1 null guard
    button->setProperty("feedbackToken", button->property("feedbackToken").toInt() + 1);
    button->setProperty("feedbackActive", false);
    button->setProperty("busy", false);
    button->setProperty("result", QString());
    const QVariant normalText = button->property("normalText");
    if (normalText.isValid()) {
        button->setText(normalText.toString());
    }
    const QVariant readyProperty = button->property("readyWhenIdle");
    // NotificationManager also supports ordinary QPushButton instances that
    // have not opted into feedback configuration.  Preserve the historical
    // busy lifecycle for those buttons while configured actions continue to
    // use their derived readyWhenIdle state.
    const bool ready = readyProperty.isValid() ? readyProperty.toBool() : true;
    button->setEnabled(ready);
    button->setCursor(ready ? Qt::PointingHandCursor : Qt::ArrowCursor);
    refreshStyle(button);
}

bool ActionButtonFeedback::isActive(const QPushButton *button) {
    if (!button) { return false; }  // v4-P6-18 P0-1 null guard
    return button->property("feedbackActive").toBool();
}

void ActionButtonFeedback::showResult(
    QPushButton *button,
    const QString &text,
    const QString &result,
    int durationMs) {
    const int token = button->property("feedbackToken").toInt() + 1;
    button->setProperty("feedbackToken", token);
    button->setProperty("feedbackActive", true);
    button->setProperty("busy", false);
    button->setProperty("result", result);
    button->setText(text);
    button->setEnabled(false);
    button->setCursor(Qt::ArrowCursor);
    refreshStyle(button);

    const QPointer<QPushButton> guardedButton(button);
    QTimer::singleShot(durationMs, button, [guardedButton, token]() {
        if (!guardedButton || guardedButton->property("feedbackToken").toInt() != token) {
            return;
        }
        guardedButton->setProperty("feedbackActive", false);
        guardedButton->setProperty("busy", false);
        guardedButton->setProperty("result", QString());
        guardedButton->setText(guardedButton->property("normalText").toString());
        const bool ready = guardedButton->property("readyWhenIdle").toBool();
        guardedButton->setEnabled(ready);
        guardedButton->setCursor(ready ? Qt::PointingHandCursor : Qt::ArrowCursor);
        refreshStyle(guardedButton);
    });
}

void ActionButtonFeedback::refreshStyle(QPushButton *button) {
    button->style()->unpolish(button);
    button->style()->polish(button);
    button->update();
}
