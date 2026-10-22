// v4-P3-4 Tag management dialog: list / create (lightweight + annotated)
// / delete git tags for the current repository.
#ifndef TAGMANAGERDIALOG_H
#define TAGMANAGERDIALOG_H

#include <QDialog>
#include "GitTagService.h"

class QTreeWidget;
class QLineEdit;
class QComboBox;
class QPushButton;

class TagManagerDialog : public QDialog {
    Q_OBJECT
public:
    explicit TagManagerDialog(GitTagService *service,
                              QWidget *parent = nullptr);

    // Reload the tag list (called on open and after each operation).
    void reload();

private slots:
    void onCreateTag();
    void onDeleteTag();
    void onTagTypeChanged(int index);

private:
    void refreshList(const QList<GitTag> &tags);

    GitTagService *m_service;
    QTreeWidget *m_tagTree;
    QLineEdit *m_nameEdit;
    QComboBox *m_typeCombo;
    QLineEdit *m_messageEdit;
    QLineEdit *m_targetEdit;
    QPushButton *m_createButton;
    QPushButton *m_deleteButton;
};

#endif // TAGMANAGERDIALOG_H
