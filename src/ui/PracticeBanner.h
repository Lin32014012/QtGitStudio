#pragma once
#include <QFrame>
#include <QString>

class QLabel;
class QPushButton;

// A thin strip above the workspace that carries the active practice: its
// goal, a progressive hint, and the same verdict the practice dialog uses.
// It exists so a student never has to leave the repository to find out
// whether the exercise is finished.
class PracticeBanner : public QFrame {
    Q_OBJECT
public:
    explicit PracticeBanner(QWidget *parent = nullptr);

    // Binds the banner to the sandbox the main window has open.
    void setPractice(const QString &practiceId, const QString &repoPath);
    void clear();

    bool isActive() const { return !m_practiceId.isEmpty(); }
    QString practiceId() const { return m_practiceId; }
    QString repositoryPath() const { return m_repoPath; }
    // Visible feedback for tests: last verdict text.
    QString resultText() const;

signals:
    // Emitted when a check passes, so the window can celebrate / move on.
    void practicePassed(const QString &practiceId);

private:
    void check();
    void toggleHint();

    QLabel *m_title = nullptr;
    QLabel *m_result = nullptr;
    QPushButton *m_hintButton = nullptr;
    QPushButton *m_checkButton = nullptr;
    QString m_practiceId;
    QString m_repoPath;
    bool m_hintVisible = false;
};
