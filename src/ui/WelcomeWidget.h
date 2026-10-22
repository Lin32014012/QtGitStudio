#pragma once
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QFrame>
#include <QGridLayout>
#include <QBoxLayout>
#include <QIcon>

// v4-P6-15: fully clickable quick-start action block (QIcon + full
// title + one-line hint). The whole area is clickable - no nested
// button; icons come from QStyle standard pixmaps (no Emoji glyphs).
class ActionBlock : public QFrame {
    Q_OBJECT
public:
    explicit ActionBlock(const QIcon &icon, const QString &title,
                         const QString &hint, const QString &accent,
                         QWidget *parent = nullptr);

    QString titleText() const;

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QLabel *m_titleLabel;
    QString m_accent;
    bool m_pressed = false;
};

class WelcomeWidget : public QWidget {
    Q_OBJECT
public:
    explicit WelcomeWidget(QWidget *parent = nullptr);
    void updateRecentProjects(const QStringList &projects);
    // v4-P6-15: environment / GitHub status injected by MainWindow.
    void setEnvironmentStatus(bool available, const QString &version);
    void setGitHubStatus(bool loggedIn, const QString &username);

signals:
    void openProjectRequested();
    void initProjectRequested();
    void cloneProjectRequested();
    void projectSelected(const QString &path);
    void environmentRequested();
    void githubAccountRequested();

protected:
    // v4-P6-15 responsive layout: wide -> two columns, narrow -> one
    // column. Only the column direction is switched here.
    void resizeEvent(QResizeEvent *event) override;

private:
    QLabel *m_envStatusLabel;
    QLabel *m_githubStatusLabel;
    QVBoxLayout *m_recentLayout;
    QFrame *m_recentContainer;
    QHBoxLayout *m_columnsLayout;
    QStringList m_projects;
};
