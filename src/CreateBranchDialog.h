#pragma once

#include <QDialog>
#include <QStringList>

class BranchTreeWidget;
class GitController;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;

class CreateBranchDialog : public QDialog {
    Q_OBJECT

public:
    explicit CreateBranchDialog(
        GitController *controller,
        QWidget *parent = nullptr,
        const QString &initialStartPoint = QString());

    void reject() override;

signals:
    void creationStarted(const QString &branchName);
    void branchCreated(const QString &branchName, bool switchedToBranch);
    void branchCreationFailed(const QString &error);

private slots:
    void scheduleValidation();
    void validateBranchName();
    void createBranch();

private:
    QString localValidationError() const;
    QString startPoint() const;
    void updatePreview();
    void updateCreateState();
    void setBusy(bool busy);
    void showValidationError(const QString &error);

    GitController *m_controller;
    BranchTreeWidget *m_startTree;
    QLineEdit *m_nameEdit;
    QCheckBox *m_switchCheck;
    QLabel *m_previewLabel;
    QLabel *m_validationLabel;
    QLabel *m_busyLabel;
    QPushButton *m_createButton;
    QPushButton *m_cancelButton;
    QTimer *m_validationTimer;
    QStringList m_existingBranches;
    int m_validationGeneration = 0;
    bool m_nameValid = false;
    bool m_busy = false;
};
