#pragma once

#include <QStringList>

class QPushButton;

class ActionButtonFeedback {
public:
    struct Palette {
        QString normal;
        QString hover;
        QString pressed;
    };

    static void configure(
        QPushButton *button,
        const Palette &palette,
        const QStringList &feedbackTexts = {});
    static void setReady(QPushButton *button, bool ready);
    static void showBusy(QPushButton *button, const QString &text);
    static void showSuccess(QPushButton *button, const QString &text, int durationMs = 1200);
    static void showError(QPushButton *button, const QString &text, int durationMs = 1500);
    static void reset(QPushButton *button);
    static bool isActive(const QPushButton *button);

private:
    static void showResult(QPushButton *button, const QString &text, const QString &result, int durationMs);
    static void refreshStyle(QPushButton *button);
};
