#pragma once

#include <QDialog>
#include <QStringList>

class BranchSwitchErrorDialog : public QDialog {
    Q_OBJECT

public:
    explicit BranchSwitchErrorDialog(
        const QString &targetBranch,
        const QString &gitError,
        QWidget *parent = nullptr);

    static bool isOverwriteProtectionError(const QString &gitError);
    static QStringList conflictingFiles(const QString &gitError);
    static QString diagnosticText(const QString &targetBranch, const QString &gitError);
};
