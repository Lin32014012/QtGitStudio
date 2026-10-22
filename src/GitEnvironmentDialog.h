// v4-P2-1 Git Environment management dialog: shows the detected git
// path, version and status; allows choosing a custom git executable and
// re-detecting.
#ifndef GITENVIRONMENTDIALOG_H
#define GITENVIRONMENTDIALOG_H

#include <QDialog>
#include "GitEnvironmentService.h"

class QLabel;
class QPushButton;

class GitEnvironmentDialog : public QDialog {
    Q_OBJECT
public:
    explicit GitEnvironmentDialog(GitEnvironmentService *service,
                                  QWidget *parent = nullptr);

    // Re-run detection (used by MainWindow after the dialog returns).
    void refresh();

    // v4-P6-20 P1-3: test hook - set the custom git path without the
    // file dialog so tests can drive re-detection (valid/invalid/restore).
    void setCustomPath(const QString &path) { m_customPath = path; }
    // v4-P6-21R6 P1-02: only a path that PASSED real detection is a
    // validated selection; the raw candidate must never be committed.
    QString validatedPath() const { return m_validatedPath; }

    // v4-P6-21 ISSUE-04: the path chosen in this dialog (empty = auto)
    // is propagated app-wide by MainWindow.
    QString selectedPath() const { return m_customPath; }
    // Apply exactly the same accepted/rejected + validated gate used by
    // MainWindow. Tests drive this with real dialog result state.
    void commitSelection(const QString &previousGitPath) const;

public slots:
    void accept() override;

private slots:
    void browseGitPath();
    void reDetect();

private:
    void applyResult(const GitEnvironmentService::GitEnvironment &env);

    GitEnvironmentService *m_service;
    QString m_customPath;
    // v4-P6-21R6 P1-02: set ONLY when detection succeeds; empty for an
    // invalid candidate or before any detection.
    QString m_validatedPath;
    QLabel *m_pathLabel;
    QLabel *m_versionLabel;
    QLabel *m_statusLabel;
    QPushButton *m_browseButton;
    QPushButton *m_redetectButton;
    QPushButton *m_closeButton;
    bool m_detectionInProgress = false;
};

#endif // GITENVIRONMENTDIALOG_H
