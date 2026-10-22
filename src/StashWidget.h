// v4-P3-5 Stash management widget: stash list + create / apply / pop /
// drop. Operations refresh through the EXISTING GitController
// refreshAsync pipeline (no new refresh system).
#ifndef STASHWIDGET_H
#define STASHWIDGET_H

#include <QWidget>
#include "GitStashService.h"

class QTreeWidget;
class QLineEdit;
class QPushButton;
class GitController;

class StashWidget : public QWidget {
    Q_OBJECT
public:
    explicit StashWidget(GitStashService *service, GitController *controller,
                         QWidget *parent = nullptr);

    // Reload the stash list (called on open and after each operation).
    void reload();

private slots:
    void onCreateStash();
    void onApplyStash();
    void onPopStash();
    void onDropStash();

private:
    void refreshList(const QList<GitStash> &stashes);
    QString selectedStashId() const;
    void runOperation(const QString &id,
                      std::function<void(GitStashService::OperationCompletion)> launch,
                      QPushButton *button, const QString &busyText,
                      const QString &successText);

    GitStashService *m_service;
    GitController *m_controller;
    QTreeWidget *m_stashTree;
    QLineEdit *m_messageEdit;
    QPushButton *m_createButton;
    QPushButton *m_applyButton;
    QPushButton *m_popButton;
    QPushButton *m_dropButton;
};

#endif // STASHWIDGET_H
