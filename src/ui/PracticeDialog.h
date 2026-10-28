#pragma once
#include <QDialog>
#include <QString>

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

// The practice chapter of the learning mode: pick an exercise, get a
// disposable sandbox, run it in the normal workspace, then come back and
// have the app verify the result.
class PracticeDialog : public QDialog {
    Q_OBJECT
public:
    explicit PracticeDialog(QWidget *parent = nullptr);

    // Where sandboxes are created. The user can point this at any folder
    // (remembered in QSettings); setSandboxRoot overrides it in memory only,
    // so tests and demos never touch the real preference.
    void setSandboxRoot(const QString &root);
    QString sandboxRoot() const;
    // Default location used when no preference has been stored.
    static QString defaultSandboxRoot();

    // Current selection (used by tests).
    QString selectedPracticeId() const { return m_selectedId; }
    bool selectPractice(const QString &id);

signals:
    // Emitted after a sandbox is created: the main window opens the
    // repository and binds its practice banner to the same exercise.
    void practiceStarted(const QString &practiceId, const QString &repoPath);

private:
    void buildList();
    void updateSelectionUi();
    void startPractice();
    void checkPractice();
    void revealHint();
    void chooseSandboxRoot();
    void exportLog();
    void reloadItemStates();
    void showResult(const QString &text, bool ok);
    QString nextPracticeId(const QString &id) const;
    QTreeWidgetItem *itemFor(const QString &id) const;
    bool isPassed(const QString &id) const;
    void setPassed(const QString &id, bool passed);

    QTreeWidget *m_list = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_goal = nullptr;
    QLabel *m_hint = nullptr;
    QLabel *m_result = nullptr;
    QLabel *m_locationLabel = nullptr;
    QPushButton *m_hintButton = nullptr;
    QPushButton *m_startButton = nullptr;
    QPushButton *m_checkButton = nullptr;
    QString m_selectedId;
    QString m_activeRepoPath;
    QString m_sandboxRoot;
    bool m_hintVisible = false;
};
