#include "ActionButtonFeedback.h"

#include <QFontMetrics>
#include <QPointer>
#include <QPushButton>
#include <QSizePolicy>
#include <QStyle>
#include <QTimer>

namespace {

// The palette a caller passes is a family selector, not a styling source:
// the button's normal colour picks one of the theme's variants and
// theme.qss owns every actual colour (normal/hover/pressed/disabled plus
// the busy/success/error feedback states). Callers keep passing a
// Palette so no call site has to change.
QString variantFor(const QString &normalColor) {
    const QString normal = normalColor.toUpper();
    if (normal == QStringLiteral("#2563EB") ||
        normal == QStringLiteral("#0D6EFD")) {
        return QStringLiteral("blue");
    }
    if (normal == QStringLiteral("#16A34A")) {
        return QStringLiteral("green");
    }
    if (normal == QStringLiteral("#DC2626")) {
        return QStringLiteral("red");
    }
    if (normal == QStringLiteral("#7C3AED")) {
        return QStringLiteral("violet");
    }
    if (normal == QStringLiteral("#0F9F9A")) {
        return QStringLiteral("teal");
    }
    if (normal == QStringLiteral("#64748B")) {
        return QStringLiteral("slate");
    }
    if (normal == QStringLiteral("#1D4ED8")) {
        return QStringLiteral("deep");
    }
    return QStringLiteral("blue");
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
    button->setProperty("variant", variantFor(palette.normal));
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    int widestText = button->fontMetrics().horizontalAdvance(button->text());
    for (const QString &text : feedbackTexts) {
        widestText = qMax(widestText, button->fontMetrics().horizontalAdvance(text));
    }
    button->setMinimumWidth(widestText + 34);
    button->setMinimumHeight(qMax(button->minimumHeight(), 40));
    setReady(button, button->isEnabled());
    refreshStyle(button);
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
