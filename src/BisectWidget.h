// v4-P4-3 Bisect management widget: start / mark good / mark bad /
// reset with live status display (active flag, good/bad commits, the
// current test commit and the located first-bad commit). Operations
// refresh through the EXISTING GitController refreshAsync pipeline.
#ifndef BISECTWIDGET_H
#define BISECTWIDGET_H

#include <QWidget>
#include "GitBisectService.h"

class QLabel;
class QLineEdit;
class QPushButton;
class GitController;

class BisectWidget : public QWidget {
    Q_OBJECT
public:
    explicit BisectWidget(GitBisectService *service, GitController *controller,
                          QWidget *parent = nullptr);

    // Reload status (called on open and after each operation).
    void reload();

private slots:
    void onStartBisect();
    void onMarkGood();
    void onMarkBad();
    void onResetBisect();

private:
    void applyStatus(const BisectStatus &status);
    void runOperation(
        const QString &busyText, const QString &successText,
        std::function<void(GitBisectService::BisectCompletion)> launch);

    GitBisectService *m_service;
    GitController *m_controller;
    QLabel *m_statusLabel;
    QLabel *m_currentLabel;
    QLabel *m_goodLabel;
    QLabel *m_badLabel;
    QLineEdit *m_badEdit;
    QLineEdit *m_goodEdit;
    QPushButton *m_startButton;
    QPushButton *m_goodButton;
    QPushButton *m_badButton;
    QPushButton *m_resetButton;
};

#endif // BISECTWIDGET_H
