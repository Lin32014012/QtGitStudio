#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class ProjectController;

class CloneDialog : public QDialog {
    Q_OBJECT

public:
    explicit CloneDialog(ProjectController *controller, QWidget *parent = nullptr);
    QString targetPath() const;

private slots:
    void browseTargetParent();
    void onInputChanged();
    void updateState();
    void startClone();

private:
    QString suggestedRepositoryName() const;
    void clearCloneFailure();
    void setCloneInProgress(bool inProgress);

    ProjectController *m_controller;
    QLineEdit *m_sourceEdit;
    QLineEdit *m_targetParentEdit;
    QLineEdit *m_nameEdit;
    QLabel *m_targetPreview;
    QLabel *m_statusLabel;
    QLabel *m_errorLabel;
    QPlainTextEdit *m_errorDetails;
    QPushButton *m_copyErrorButton;
    QPushButton *m_browseButton;
    QPushButton *m_startButton;
    QPushButton *m_cancelButton;
    QString m_lastSuggestedName;
    bool m_cloneInProgress = false;
    bool m_hasCloneFailure = false;
};
