#include "WelcomeWidget.h"
#include "Theme.h"

#include <QMouseEvent>
#include <QEnterEvent>
#include <QStyle>
#include <QFileInfo>
#include <QDateTime>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QResizeEvent>
#include <functional>

namespace {

// Rounded status dot as a plain QLabel so the row stays free of nested
// QFrames (the recent-projects regression counts QFrame children).
// Presence dots are six pixels square, so the sheet's 3px radius fits;
// state (ok / idle / danger) selects the colour in theme.qss.
QLabel *makeDot(QWidget *parent, const QString &state) {
    auto *dot = new QLabel(parent);
    dot->setFixedSize(6, 6);
    dot->setProperty("role", QStringLiteral("dot"));
    dot->setProperty("state", state);
    return dot;
}

void setDotState(QLabel *dot, const QString &state) {
    if (!dot) return;
    dot->setProperty("state", state);
    Theme::restyle(dot);
}

QLabel *makeLabel(const QString &text, const char *role, QWidget *parent) {
    auto *label = new QLabel(text, parent);
    label->setProperty("role", QString::fromLatin1(role));
    return label;
}

// v4-P6-6: relative "最近打开" time (e.g. "10分钟前"). The recent-projects
// data structure stores paths only (never modified), so the directory
// modification time is used as the approximation.
QString lastOpenedText(const QString &projectPath) {
    const QFileInfo info(projectPath);
    if (!info.exists()) return QString();
    const qint64 seconds =
        info.lastModified().secsTo(QDateTime::currentDateTime());
    if (seconds < 60) return QStringLiteral("刚刚");
    if (seconds < 3600) {
        return QStringLiteral("%1分钟前").arg(seconds / 60);
    }
    if (seconds < 86400) {
        return QStringLiteral("%1小时前").arg(seconds / 3600);
    }
    if (seconds < 7LL * 86400) {
        return QStringLiteral("%1天前").arg(seconds / 86400);
    }
    return info.lastModified().toString(QStringLiteral("yyyy-MM-dd"));
}

// v4-P6-6: read the checked-out branch from .git/HEAD without running
// git (pure file read - no Git logic touched).
QString branchOf(const QString &projectPath) {
    QFile headFile(projectPath + "/.git/HEAD");
    if (!headFile.open(QIODevice::ReadOnly)) {
        return QString();
    }
    const QString head = QString::fromUtf8(headFile.readAll()).trimmed();
    headFile.close();
    if (head.startsWith(QStringLiteral("ref: refs/heads/"))) {
        return head.mid(QStringLiteral("ref: refs/heads/").size());
    }
    if (head.size() >= 7 && head.size() <= 40 && !head.contains(' ')) {
        return QStringLiteral("(detached %1)").arg(head.left(7));
    }
    return QString();
}

// Whole-block click without a nested QPushButton.
class ClickFilter : public QObject {
public:
    ClickFilter(std::function<void()> onRelease, QObject *parent)
        : QObject(parent), m_onRelease(std::move(onRelease)) {}

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override {
        if (ev->type() == QEvent::MouseButtonPress) {
            m_pressed = true;
        } else if (ev->type() == QEvent::MouseButtonRelease) {
            if (m_pressed && m_onRelease) {
                m_onRelease();
            }
            m_pressed = false;
        }
        return QObject::eventFilter(obj, ev);
    }

private:
    std::function<void()> m_onRelease;
    bool m_pressed = false;
};

}  // namespace

// --- ActionBlock --------------------------------------------------------

ActionBlock::ActionBlock(const QIcon &icon, const QString &title,
                         const QString &hint, const QString &accent,
                         QWidget *parent)
    : QFrame(parent)
    , m_accent(accent) {
    setObjectName("actionBlock");
    setCursor(Qt::PointingHandCursor);
    // role="action" in theme.qss owns background/border/hover; the widget
    // only needs styled-background + hover tracking for the pseudo-state.
    setProperty("role", QStringLiteral("action"));
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_Hover, true);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 15, 16, 15);
    layout->setSpacing(14);

    // Icon sits in a soft rounded tile so the block reads as a function
    // card rather than stacked text.
    auto *iconTile = new QFrame(this);
    iconTile->setObjectName("iconTile");
    iconTile->setFixedSize(34, 34);
    iconTile->setAttribute(Qt::WA_StyledBackground, true);
    iconTile->setProperty("role", QStringLiteral("iconTile"));
    iconTile->setProperty(
        "tint", accent == QStringLiteral("#059669") ? QStringLiteral("green")
                : accent == QStringLiteral("#7C3AED") ? QStringLiteral("violet")
                                                      : QStringLiteral("blue"));
    auto *tileLayout = new QVBoxLayout(iconTile);
    tileLayout->setContentsMargins(0, 0, 0, 0);
    auto *iconLabel = new QLabel(iconTile);
    iconLabel->setPixmap(icon.pixmap(18, 18));
    iconLabel->setAlignment(Qt::AlignCenter);
    tileLayout->addWidget(iconLabel);
    layout->addWidget(iconTile, 0, Qt::AlignVCenter);

    auto *textLayout = new QVBoxLayout();
    textLayout->setSpacing(3);
    textLayout->setContentsMargins(0, 0, 0, 0);
    m_titleLabel = new QLabel(title, this);
    m_titleLabel->setProperty("role", QStringLiteral("title"));
    textLayout->addWidget(m_titleLabel);

    auto *hintLabel = new QLabel(hint, this);
    hintLabel->setProperty("role", QStringLiteral("desc"));
    hintLabel->setWordWrap(true);
    textLayout->addWidget(hintLabel);

    layout->addLayout(textLayout, 1);
}

QString ActionBlock::titleText() const {
    return m_titleLabel->text();
}

void ActionBlock::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        event->accept();
        return;
    }
    QFrame::mousePressEvent(event);
}

void ActionBlock::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && m_pressed) {
        m_pressed = false;
        if (rect().contains(event->pos())) {
            emit clicked();
        }
        event->accept();
        return;
    }
    QFrame::mouseReleaseEvent(event);
}

void ActionBlock::enterEvent(QEnterEvent *event) {
    setCursor(Qt::PointingHandCursor);
    update();
    QFrame::enterEvent(event);
}

void ActionBlock::leaveEvent(QEvent *event) {
    update();
    QFrame::leaveEvent(event);
}

// --- WelcomeWidget ------------------------------------------------------

WelcomeWidget::WelcomeWidget(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("welcomePage"));
    setAttribute(Qt::WA_StyledBackground, true);

    m_columnsLayout = new QHBoxLayout(this);
    m_columnsLayout->setContentsMargins(0, 0, 0, 0);
    m_columnsLayout->setSpacing(0);

    buildSidebar();
    buildMainArea();

    m_columnsLayout->addWidget(m_sidePanel, 0);
    m_columnsLayout->addWidget(m_mainArea, 1);

    setEnvironmentStatus(false, QString());
    setGitHubStatus(false, QString());
    refreshStrip();
}

void WelcomeWidget::buildSidebar() {
    m_sidePanel = new QFrame(this);
    m_sidePanel->setObjectName(QStringLiteral("welcomeSide"));
    m_sidePanel->setProperty("role", QStringLiteral("sidebar"));
    m_sidePanel->setAttribute(Qt::WA_StyledBackground, true);
    m_sidePanel->setFixedWidth(296);

    auto *sideLayout = new QVBoxLayout(m_sidePanel);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(0);

    // Header: section label + project count.
    auto *header = new QWidget(m_sidePanel);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 16, 16, 8);
    headerLayout->setSpacing(8);
    auto *sectionLabel = makeLabel(QStringLiteral("项目"), "section", header);
    m_sideCountLabel = makeLabel(QStringLiteral("0"), "meta", header);
    headerLayout->addWidget(sectionLabel);
    headerLayout->addWidget(m_sideCountLabel, 0, Qt::AlignVCenter);
    headerLayout->addStretch(1);
    sideLayout->addWidget(header);

    // Scrollable recent-project rail.
    auto *scroll = new QScrollArea(m_sidePanel);
    scroll->setObjectName(QStringLiteral("projectScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_recentContainer = new QFrame(scroll);
    m_recentContainer->setObjectName(QStringLiteral("recentContainer"));
    m_recentLayout = new QVBoxLayout(m_recentContainer);
    m_recentLayout->setContentsMargins(8, 0, 8, 0);
    m_recentLayout->setSpacing(2);
    m_recentLayout->addStretch(1);
    scroll->setWidget(m_recentContainer);
    sideLayout->addWidget(scroll, 1);

    // Dashed "add existing repository" entry.
    auto *addRow = new QFrame(m_sidePanel);
    addRow->setObjectName(QStringLiteral("welcomeAddRow"));
    addRow->setProperty("role", QStringLiteral("dashed"));
    addRow->setAttribute(Qt::WA_StyledBackground, true);
    addRow->setCursor(Qt::PointingHandCursor);
    auto *addLayout = new QHBoxLayout(addRow);
    addLayout->setContentsMargins(10, 8, 10, 8);
    auto *addLabel = makeLabel(QStringLiteral("\uFF0B  添加已有仓库\u2026"),
                               "body", addRow);
    addLayout->addWidget(addLabel);
    addLayout->addStretch(1);
    auto *addHolder = new QWidget(m_sidePanel);
    auto *addHolderLayout = new QVBoxLayout(addHolder);
    addHolderLayout->setContentsMargins(16, 8, 16, 10);
    addHolderLayout->addWidget(addRow);
    sideLayout->addWidget(addHolder);
    addRow->installEventFilter(
        new ClickFilter([this]() { emit openProjectRequested(); }, addRow));

    // Footer: environment + GitHub status with their action buttons.
    auto *foot = new QFrame(m_sidePanel);
    foot->setProperty("role", QStringLiteral("sidebarFoot"));
    foot->setAttribute(Qt::WA_StyledBackground, true);
    auto *footLayout = new QVBoxLayout(foot);
    footLayout->setContentsMargins(16, 12, 16, 14);
    footLayout->setSpacing(6);

    auto *envRow = new QHBoxLayout();
    envRow->setSpacing(7);
    m_envDot = makeDot(foot, QStringLiteral("idle"));
    m_envStatusLabel = makeLabel(QStringLiteral("Git 环境：检测中\u2026"),
                                 "body", foot);
    envRow->addWidget(m_envDot);
    envRow->addWidget(m_envStatusLabel, 1);
    footLayout->addLayout(envRow);

    auto *ghRow = new QHBoxLayout();
    ghRow->setSpacing(7);
    m_githubDot = makeDot(foot, QStringLiteral("idle"));
    m_githubStatusLabel = makeLabel(QStringLiteral("GitHub：未连接"),
                                    "body", foot);
    ghRow->addWidget(m_githubDot);
    ghRow->addWidget(m_githubStatusLabel, 1);
    footLayout->addLayout(ghRow);

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(6);
    auto *envButton = new QPushButton(QStringLiteral("Git 环境"), foot);
    envButton->setObjectName("welcomeEnvButton");
    envButton->setCursor(Qt::PointingHandCursor);
    auto *githubButton = new QPushButton(QStringLiteral("账户"), foot);
    githubButton->setObjectName("welcomeGithubButton");
    githubButton->setCursor(Qt::PointingHandCursor);
    btnRow->addWidget(envButton);
    btnRow->addWidget(githubButton);
    btnRow->addStretch(1);
    footLayout->addLayout(btnRow);
    sideLayout->addWidget(foot);

    connect(envButton, &QPushButton::clicked,
            this, &WelcomeWidget::environmentRequested);
    connect(githubButton, &QPushButton::clicked,
            this, &WelcomeWidget::githubAccountRequested);
}

void WelcomeWidget::buildMainArea() {
    m_mainArea = new QWidget(this);
    m_mainArea->setAttribute(Qt::WA_StyledBackground, true);
    m_mainArea->setObjectName(QStringLiteral("welcomeMain"));

    auto *layout = new QVBoxLayout(m_mainArea);
    layout->setContentsMargins(26, 24, 28, 22);
    layout->setSpacing(18);

    // Hero.
    auto *heroBox = new QVBoxLayout();
    heroBox->setSpacing(5);
    heroBox->setContentsMargins(0, 0, 0, 0);
    heroBox->addWidget(makeLabel(QStringLiteral("QtGitStudio"), "hero", m_mainArea));
    heroBox->addWidget(makeLabel(QStringLiteral("让 Git 对新手更加简单"),
                                 "subtitle", m_mainArea));
    layout->addLayout(heroBox);

    // Quick-start tiles: 2 columns, the clone tile spans the full row.
    const QStyle *st = style();
    auto *openBlock = new ActionBlock(
        st->standardIcon(QStyle::SP_DirOpenIcon),
        QStringLiteral("打开仓库"),
        QStringLiteral("打开本机已有的 Git 仓库，自动读取分支与工作区状态"),
        QStringLiteral("#2563EB"), m_mainArea);
    openBlock->setObjectName("openAction");
    auto *initBlock = new ActionBlock(
        st->standardIcon(QStyle::SP_FileDialogNewFolder),
        QStringLiteral("初始化仓库"),
        QStringLiteral("把当前文件夹变成 Git 仓库，并生成第一次提交"),
        QStringLiteral("#059669"), m_mainArea);
    initBlock->setObjectName("initAction");
    auto *cloneBlock = new ActionBlock(
        st->standardIcon(QStyle::SP_ArrowDown),
        QStringLiteral("克隆仓库"),
        QStringLiteral("从远程 GitHub 克隆到本地，自动识别默认分支"),
        QStringLiteral("#7C3AED"), m_mainArea);
    cloneBlock->setObjectName("cloneAction");

    m_actionGrid = new QGridLayout();
    m_actionGrid->setHorizontalSpacing(12);
    m_actionGrid->setVerticalSpacing(12);
    m_actionGrid->setContentsMargins(0, 0, 0, 0);
    m_actionGrid->addWidget(openBlock, 0, 0);
    m_actionGrid->addWidget(initBlock, 0, 1);
    m_actionGrid->addWidget(cloneBlock, 1, 0, 1, 2);
    layout->addLayout(m_actionGrid);

    layout->addWidget(buildGuidePanel());
    layout->addStretch(1);

    // Bottom status strip: real values only (no invented repo state).
    auto *strip = new QFrame(m_mainArea);
    strip->setProperty("role", QStringLiteral("strip"));
    strip->setAttribute(Qt::WA_StyledBackground, true);
    auto *stripLayout = new QHBoxLayout(strip);
    stripLayout->setContentsMargins(14, 11, 14, 11);
    stripLayout->setSpacing(16);

    m_stripGitLabel = makeLabel(QStringLiteral("Git 环境：检测中\u2026"),
                                "body", strip);
    m_stripProjectLabel = makeLabel(QStringLiteral("最近项目 0 个"),
                                    "body", strip);
    stripLayout->addWidget(m_stripGitLabel);
    stripLayout->addWidget(m_stripProjectLabel);
    stripLayout->addStretch(1);

    auto *openButton = new QPushButton(QStringLiteral("打开仓库"), strip);
    openButton->setProperty("role", QStringLiteral("primary"));
    openButton->setCursor(Qt::PointingHandCursor);
    stripLayout->addWidget(openButton);
    layout->addWidget(strip);

    connect(openBlock, &ActionBlock::clicked,
            this, &WelcomeWidget::openProjectRequested);
    connect(initBlock, &ActionBlock::clicked,
            this, &WelcomeWidget::initProjectRequested);
    connect(cloneBlock, &ActionBlock::clicked,
            this, &WelcomeWidget::cloneProjectRequested);
    connect(openButton, &QPushButton::clicked,
            this, &WelcomeWidget::openProjectRequested);
}

QWidget *WelcomeWidget::buildGuidePanel() {
    auto *panel = new QFrame(m_mainArea);
    panel->setObjectName(QStringLiteral("guidePanel"));
    panel->setProperty("role", QStringLiteral("panel"));
    panel->setAttribute(Qt::WA_StyledBackground, true);

    auto *v = new QVBoxLayout(panel);
    v->setContentsMargins(16, 14, 16, 15);
    v->setSpacing(11);
    v->addWidget(makeLabel(QStringLiteral("\u5FEB\u901F\u4E0A\u624B"), "title", panel));

    struct Step {
        const char *badge;
        const char *title;
        const char *desc;
    };
    const Step steps[] = {
        {"1", "\u6253\u5F00\u4ED3\u5E93",
         "\u4ECE\u5DE6\u4FA7\u5217\u8868\u9009\u62E9\uFF0C\u6216\u6253\u5F00\u4E00\u4E2A\u672C\u673A\u4ED3\u5E93"},
        {"2", "\u6682\u5B58\u6539\u52A8",
         "\u5728\u6587\u4EF6\u9762\u677F\u52FE\u9009\u8981\u7EB3\u5165\u672C\u6B21\u63D0\u4EA4\u7684\u6539\u52A8"},
        {"3", "\u63D0\u4EA4\u5386\u53F2",
         "\u5199\u63D0\u4EA4\u4FE1\u606F\uFF0C\u6216\u7528\u63D0\u4EA4\u56FE\u5BF9\u6BD4\u5206\u652F"},
    };

    auto *row = new QHBoxLayout();
    row->setSpacing(10);
    for (const Step &step : steps) {
        auto *card = new QFrame(panel);
        card->setObjectName(QStringLiteral("guideStep"));
        card->setAttribute(Qt::WA_StyledBackground, true);
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(12, 11, 12, 11);
        cardLayout->setSpacing(5);

        auto *topRow = new QHBoxLayout();
        topRow->setSpacing(7);
        auto *badge = makeLabel(QString::fromLatin1(step.badge), "pillMono", card);
        topRow->addWidget(badge, 0, Qt::AlignVCenter);
        topRow->addWidget(makeLabel(QString::fromUtf8(step.title), "title", card), 1);
        cardLayout->addLayout(topRow);

        auto *desc = makeLabel(QString::fromUtf8(step.desc), "desc", card);
        desc->setWordWrap(true);
        cardLayout->addWidget(desc);
        row->addWidget(card, 1);
    }
    v->addLayout(row);
    return panel;
}

QFrame *WelcomeWidget::makeRecentRow(const QString &path) {
    auto *row = new QFrame(m_recentContainer);
    row->setProperty("role", QStringLiteral("row"));
    row->setAttribute(Qt::WA_StyledBackground, true);
    row->setCursor(Qt::PointingHandCursor);
    row->setMinimumHeight(46);

    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(8, 7, 8, 7);
    rowLayout->setSpacing(9);

    auto *iconLabel = new QLabel(row);
    iconLabel->setFixedSize(24, 24);
    iconLabel->setPixmap(style()->standardIcon(QStyle::SP_DirIcon)
                             .pixmap(16, 16));
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setProperty("role", QStringLiteral("rowIcon"));
    rowLayout->addWidget(iconLabel);

    auto *textLayout = new QVBoxLayout();
    textLayout->setSpacing(1);
    textLayout->setContentsMargins(0, 0, 0, 0);
    auto *nameLabel = new QLabel(QFileInfo(path).fileName(), row);
    nameLabel->setProperty("role", QStringLiteral("rowName"));
    textLayout->addWidget(nameLabel);

    // Secondary line: branch + elided path (monospace, muted).
    const QString branch = branchOf(path);
    QString secondary = branch.isEmpty()
        ? QFileInfo(path).absolutePath()
        : QStringLiteral("%1 \u00B7 %2").arg(
              branch, QFileInfo(path).absolutePath());
    auto *secondaryLabel = new QLabel(
        QFontMetrics(nameLabel->font())
            .elidedText(secondary, Qt::ElideMiddle, 170),
        row);
    secondaryLabel->setToolTip(path);
    secondaryLabel->setProperty("role", QStringLiteral("mono"));
    textLayout->addWidget(secondaryLabel);
    rowLayout->addLayout(textLayout, 1);

    // Timestamp and presence dot share one baseline at the row tail.
    const QString opened = lastOpenedText(path);
    auto *tail = new QHBoxLayout();
    tail->setSpacing(8);
    tail->setContentsMargins(0, 0, 0, 0);
    if (!opened.isEmpty()) {
        auto *timeLabel = makeLabel(opened, "meta", row);
        timeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        tail->addWidget(timeLabel, 0, Qt::AlignVCenter);
    }
    // Rows only exist for paths that are still directories, so the dot is
    // green by construction - it marks "present on disk", nothing more.
    tail->addWidget(makeDot(row, QStringLiteral("ok")), 0, Qt::AlignVCenter);
    rowLayout->addLayout(tail);

    row->installEventFilter(
        new ClickFilter([this, path]() { emit projectSelected(path); }, row));
    return row;
}

void WelcomeWidget::setEnvironmentStatus(bool available,
                                         const QString &version) {
    m_envAvailable = available;
    m_envVersion = version;
    const QString text = available
        ? QStringLiteral("Git %1 \u00B7 可用").arg(
              version.isEmpty() ? QStringLiteral("已安装") : version)
        : QStringLiteral("Git 环境不可用");
    if (m_envStatusLabel) m_envStatusLabel->setText(text);
    if (m_stripGitLabel) m_stripGitLabel->setText(text);
    setDotState(m_envDot, available ? QStringLiteral("ok")
                                    : QStringLiteral("danger"));
}

void WelcomeWidget::setGitHubStatus(bool loggedIn, const QString &username) {
    m_githubConnected = loggedIn;
    m_githubUser = username;
    const QString text = loggedIn
        ? QStringLiteral("GitHub \u00B7 %1").arg(
              username.isEmpty() ? QStringLiteral("已连接") : username)
        : QStringLiteral("GitHub 未连接");
    if (m_githubStatusLabel) m_githubStatusLabel->setText(text);
    setDotState(m_githubDot, loggedIn ? QStringLiteral("ok")
                                      : QStringLiteral("idle"));
}

void WelcomeWidget::refreshStrip() {
    // Count what the rail actually shows: stale paths are filtered out, so
    // the raw feed length would over-report.
    if (m_sideCountLabel) {
        m_sideCountLabel->setText(QString::number(m_shownCount));
    }
    if (m_stripProjectLabel) {
        m_stripProjectLabel->setText(
            QStringLiteral("最近项目 %1 个").arg(m_shownCount));
    }
}

void WelcomeWidget::updateRecentProjects(const QStringList &projects) {
    m_projects = projects;
    // Clear existing rows (synchronous delete so a second update call
    // never leaves stale rows behind).
    while (QLayoutItem *item = m_recentLayout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->deleteLater();
            w->setParent(nullptr);
            delete w;
        }
        delete item;
    }
    const QStringList shown = projects.mid(0, 8);
    int added = 0;
    for (const QString &path : shown) {
        if (!QFileInfo(path).isDir()) {
            continue;  // filter invalid / removed paths
        }
        m_recentLayout->addWidget(makeRecentRow(path));
        ++added;
    }
    if (added == 0) {
        auto *emptyLabel = makeLabel(QStringLiteral("还没有打开过项目"),
                                     "meta", m_recentContainer);
        emptyLabel->setContentsMargins(8, 12, 8, 12);
        m_recentLayout->addWidget(emptyLabel);
    }
    m_recentLayout->addStretch(1);
    m_shownCount = added;
    refreshStrip();
}

void WelcomeWidget::resizeEvent(QResizeEvent *event) {
    // Responsive: the rail narrows first, then the action grid stacks.
    const int w = width();
    if (m_sidePanel) {
        if (w < 860) {
            m_sidePanel->setFixedWidth(240);
        } else {
            m_sidePanel->setFixedWidth(296);
        }
    }
    if (m_actionGrid) {
        // Re-add the clone tile so its span follows the column count.
        QLayoutItem *cloneItem = m_actionGrid->itemAtPosition(1, 0);
        if (cloneItem && cloneItem->widget()) {
            QWidget *cloneW = cloneItem->widget();
            m_actionGrid->removeWidget(cloneW);
            if (w < 760) {
                m_actionGrid->addWidget(cloneW, 2, 0, 1, 1);
            } else {
                m_actionGrid->addWidget(cloneW, 1, 0, 1, 2);
            }
        }
    }
    QWidget::resizeEvent(event);
}
