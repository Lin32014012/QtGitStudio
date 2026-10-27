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
// v4-P6-26: styling moved to resources/theme.qss (role="action"), so the
// hover state comes from the stylesheet instead of a per-widget rewrite.
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

// v4-P6-26: split workbench welcome page - project rail on the left,
// quick-start tiles + status strip on the right. The public API (signals,
// status setters, recent-project feed) is unchanged, so MainWindow and
// the welcome regression tests keep working.
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
    // v4-P6-15: responsive layout. Wide -> split workbench; narrow -> the
    // project rail shrinks and the action grid stacks into one column.
    void resizeEvent(QResizeEvent *event) override;

private:
    void buildSidebar();
    void buildMainArea();
    QWidget *buildGuidePanel();
    QFrame *makeRecentRow(const QString &path);
    void refreshStrip();

    QLabel *m_envStatusLabel = nullptr;
    QLabel *m_githubStatusLabel = nullptr;
    QLabel *m_envDot = nullptr;
    QLabel *m_githubDot = nullptr;
    QLabel *m_sideCountLabel = nullptr;
    QLabel *m_stripGitLabel = nullptr;
    QLabel *m_stripProjectLabel = nullptr;
    QVBoxLayout *m_recentLayout = nullptr;
    QFrame *m_recentContainer = nullptr;
    QFrame *m_sidePanel = nullptr;
    QWidget *m_mainArea = nullptr;
    QHBoxLayout *m_columnsLayout = nullptr;
    QGridLayout *m_actionGrid = nullptr;
    QStringList m_projects;
    int m_shownCount = 0;
    bool m_envAvailable = false;
    QString m_envVersion;
    bool m_githubConnected = false;
    QString m_githubUser;
};
