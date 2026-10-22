#pragma once
#include <QDialog>

class BranchRelationWidget;
class QPushButton;
class QLineEdit;
class QShowEvent;
class QResizeEvent;

// v4-P6-18: standalone branch-graph viewer window - opens from the
// branch list page so the embedded main window stays compact while
// large graphs get zoom/pan/fit/search in their own space.
class BranchGraphViewerDialog : public QDialog {
    Q_OBJECT
public:
    explicit BranchGraphViewerDialog(const QString &repositoryPath,
                                     QWidget *parent = nullptr);
    BranchRelationWidget *relationWidget() const { return m_relation; }

protected:
    // v4-P6-18: wheel zoom on the embedded view.
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

signals:
    void switchBranchRequested(const QString &branch);
    void deleteBranchRequested(const QString &branch);
    void mergeBranchRequested(const QString &branch);
    void rebaseBranchRequested(const QString &branch);

private:
    void fitGraphToView(bool resetVerticalPosition = false);
    BranchRelationWidget *m_relation = nullptr;
    bool m_userTransformed = false;
    bool m_lastGraphWasLinear = false;
};
