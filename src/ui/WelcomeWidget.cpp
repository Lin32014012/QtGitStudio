#include "WelcomeWidget.h"

#include <QMouseEvent>
#include <QEnterEvent>
#include <QStyle>
#include <QFileInfo>
#include <QDateTime>
#include <QScrollArea>

// --- ActionBlock --------------------------------------------------------

ActionBlock::ActionBlock(const QIcon &icon, const QString &title,
                         const QString &hint, const QString &accent,
                         QWidget *parent)
    : QFrame(parent)
    , m_accent(accent) {
    setObjectName("actionBlock");
    setCursor(Qt::PointingHandCursor);
    setStyleSheet(
        "QFrame#actionBlock { background: #FFFFFF;"
        " border: 1.5px solid #E5E7EB; border-radius: 10px; }");

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(12);

    // v4-P6-16: icon sits in a soft rounded tile so the block reads as
    // a proper function card, not plain stacked text.
    auto *iconTile = new QFrame(this);
    iconTile->setStyleSheet(
        QStringLiteral("QFrame { background: %1; border-radius: 8px; }")
            .arg(accent + QStringLiteral("1A")));
    iconTile->setFixedSize(38, 38);
    auto *tileLayout = new QVBoxLayout(iconTile);
    tileLayout->setContentsMargins(0, 0, 0, 0);
    auto *iconLabel = new QLabel(iconTile);
    iconLabel->setPixmap(icon.pixmap(20, 20));
    iconLabel->setAlignment(Qt::AlignCenter);
    tileLayout->addWidget(iconLabel);
    layout->addWidget(iconTile, 0, Qt::AlignVCenter);

    auto *textLayout = new QVBoxLayout();
    textLayout->setSpacing(2);
    m_titleLabel = new QLabel(title, this);
    m_titleLabel->setStyleSheet(
        "font-size: 14px; font-weight: 600; color: #111827;"
        " border: none; background: transparent;");
    textLayout->addWidget(m_titleLabel);

    auto *hintLabel = new QLabel(hint, this);
    hintLabel->setStyleSheet(
        "font-size: 12px; color: #6B7280;"
        " border: none; background: transparent;");
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
    setStyleSheet(
        "QFrame#actionBlock { background: #F8FAFF;"
        " border: 1.5px solid #93C5FD; border-radius: 10px; }");
    QFrame::enterEvent(event);
}

void ActionBlock::leaveEvent(QEvent *event) {
    setStyleSheet(
        "QFrame#actionBlock { background: #FFFFFF;"
        " border: 1.5px solid #E5E7EB; border-radius: 10px; }");
    QFrame::leaveEvent(event);
}

// --- WelcomeWidget ------------------------------------------------------

namespace {
// v4-P6-6: relative "最近打开" time (e.g. "10分钟前"). The
// recent-projects data structure stores paths only (never modified), so
// the directory modification time is used as the approximation.
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

QFrame *sectionCard(const QString &title, QWidget *parent) {
    auto *card = new QFrame(parent);
    card->setStyleSheet(
        "QFrame { background: #FFFFFF; border: 1px solid #E5E7EB;"
        " border-radius: 10px; }");
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(6);
    auto *titleLabel = new QLabel(title, card);
    titleLabel->setStyleSheet(
        "font-size: 14px; font-weight: 700; color: #111827;"
        " border: none; background: transparent;");
    layout->addWidget(titleLabel);
    return card;
}
}  // namespace

WelcomeWidget::WelcomeWidget(QWidget *parent) : QWidget(parent) {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 12, 24, 12);
    mainLayout->setSpacing(6);

    // Title area - centered, compact.
    auto *titleLabel = new QLabel("QtGitStudio", this);
    titleLabel->setStyleSheet(
        "font-size: 26px; font-weight: bold; color: #1a1a2e;"
        " font-family: 'Microsoft YaHei UI';");
    titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(titleLabel);

    auto *subtitleLabel = new QLabel("让 Git 对新手更加简单", this);
    subtitleLabel->setStyleSheet(
        "font-size: 13px; color: #6B7280;"
        " font-family: 'Microsoft YaHei UI';");
    subtitleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(subtitleLabel);
    mainLayout->addSpacing(8);

    // v4-P6-15: two-column dashboard, capped at 1000px, centered.
    m_columnsLayout = new QHBoxLayout();
    m_columnsLayout->setSpacing(14);
    auto *columnsWidget = new QWidget(this);
    columnsWidget->setMaximumWidth(1000);
    columnsWidget->setLayout(m_columnsLayout);
    mainLayout->addWidget(columnsWidget, 0, Qt::AlignHCenter);
    mainLayout->addStretch(1);

    // Left column: recent projects (height shrinks with content).
    auto *recentCard = sectionCard("最近项目", columnsWidget);
    auto *recentCardLayout = qobject_cast<QVBoxLayout *>(
        recentCard->layout());
    m_recentContainer = new QFrame(recentCard);
    m_recentContainer->setObjectName("recentContainer");
    m_recentContainer->setStyleSheet(
        "QFrame { background: transparent; border: none; }");
    m_recentLayout = new QVBoxLayout(m_recentContainer);
    m_recentLayout->setContentsMargins(0, 0, 0, 0);
    m_recentLayout->setSpacing(2);
    recentCardLayout->addWidget(m_recentContainer);
    // No fixed minimum height: with few projects the card shrinks.
    recentCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_columnsLayout->addWidget(recentCard, 3);

    // Right column: quick start + environment status.
    auto *rightColumn = new QVBoxLayout();
    rightColumn->setSpacing(10);

    auto *quickCard = sectionCard("快速开始", columnsWidget);
    auto *quickLayout = qobject_cast<QVBoxLayout *>(quickCard->layout());
    const QStyle *st = style();
    auto *openBlock = new ActionBlock(
        st->standardIcon(QStyle::SP_DirOpenIcon),
        "打开仓库", "打开本机已有的 Git 仓库", "#2563EB", quickCard);
    openBlock->setObjectName("openAction");
    auto *initBlock = new ActionBlock(
        st->standardIcon(QStyle::SP_FileDialogNewFolder),
        "初始化仓库", "把当前文件夹变成 Git 仓库", "#059669", quickCard);
    initBlock->setObjectName("initAction");
    auto *cloneBlock = new ActionBlock(
        st->standardIcon(QStyle::SP_ArrowDown),
        "克隆仓库", "从远程 GitHub 克隆到本地", "#7C3AED", quickCard);
    cloneBlock->setObjectName("cloneAction");
    quickLayout->addWidget(openBlock);
    quickLayout->addWidget(initBlock);
    quickLayout->addWidget(cloneBlock);
    rightColumn->addWidget(quickCard);

    // Environment / GitHub status card.
    auto *statusCard = sectionCard("环境", columnsWidget);
    auto *statusLayout = qobject_cast<QVBoxLayout *>(statusCard->layout());
    m_envStatusLabel = new QLabel("Git 环境：检测中…", statusCard);
    m_envStatusLabel->setStyleSheet(
        "font-size: 12px; color: #374151; border: none;"
        " background: transparent;");
    statusLayout->addWidget(m_envStatusLabel);
    m_githubStatusLabel = new QLabel("GitHub：未连接", statusCard);
    m_githubStatusLabel->setStyleSheet(
        "font-size: 12px; color: #374151; border: none;"
        " background: transparent;");
    statusLayout->addWidget(m_githubStatusLabel);
    auto *statusRow = new QHBoxLayout();
    auto *envButton = new QPushButton("Git 环境", statusCard);
    envButton->setObjectName("welcomeEnvButton");
    auto *githubButton = new QPushButton("GitHub 账户", statusCard);
    githubButton->setObjectName("welcomeGithubButton");
    for (QPushButton *b : {envButton, githubButton}) {
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(
            "QPushButton { background: #F3F4F6; color: #374151;"
            " border: 1px solid #D1D5DB; border-radius: 6px;"
            " padding: 5px 12px; font-size: 12px; }"
            "QPushButton:hover { background: #E5E7EB; }");
    }
    statusRow->addWidget(envButton);
    statusRow->addWidget(githubButton);
    statusRow->addStretch();
    statusLayout->addLayout(statusRow);
    rightColumn->addWidget(statusCard);
    rightColumn->addStretch();

    auto *rightContainer = new QWidget(columnsWidget);
    rightContainer->setLayout(rightColumn);
    m_columnsLayout->addWidget(rightContainer, 2);

    connect(openBlock, &ActionBlock::clicked,
            this, &WelcomeWidget::openProjectRequested);
    connect(initBlock, &ActionBlock::clicked,
            this, &WelcomeWidget::initProjectRequested);
    connect(cloneBlock, &ActionBlock::clicked,
            this, &WelcomeWidget::cloneProjectRequested);
    connect(envButton, &QPushButton::clicked,
            this, &WelcomeWidget::environmentRequested);
    connect(githubButton, &QPushButton::clicked,
            this, &WelcomeWidget::githubAccountRequested);
}

void WelcomeWidget::setEnvironmentStatus(bool available,
                                         const QString &version) {
    m_envStatusLabel->setText(available
        ? QStringLiteral("Git 环境：%1 · 可用").arg(
              version.isEmpty() ? QStringLiteral("已安装") : version)
        : QStringLiteral("Git 环境：不可用"));
    m_envStatusLabel->setStyleSheet(
        QStringLiteral("font-size: 12px; color: %1; border: none;"
                       " background: transparent;")
            .arg(available ? "#16A34A" : "#DC2626"));
}

void WelcomeWidget::setGitHubStatus(bool loggedIn, const QString &username) {
    m_githubStatusLabel->setText(
        loggedIn ? QStringLiteral("GitHub：已连接（%1）").arg(username)
                 : QStringLiteral("GitHub：未连接"));
    m_githubStatusLabel->setStyleSheet(
        QStringLiteral("font-size: 12px; color: %1; border: none;"
                       " background: transparent;")
            .arg(loggedIn ? "#16A34A" : "#6B7280"));
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
    for (const QString &path : shown) {
        if (!QFileInfo(path).isDir()) {
            continue;  // filter invalid / removed paths
        }
        auto *row = new QFrame(m_recentContainer);
        row->setCursor(Qt::PointingHandCursor);
        row->setStyleSheet(
            "QFrame { background: #FAFAFA; border: 1px solid #EEF0F2;"
            " border-radius: 8px; }"
            "QFrame:hover { background: #F0F6FF;"
            " border-color: #BFDBFE; }");
        row->setMinimumHeight(48);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(10, 6, 10, 6);
        rowLayout->setSpacing(8);

        const QString name = QFileInfo(path).fileName();
        auto *primaryLabel = new QLabel(name, row);
        primaryLabel->setStyleSheet(
            "font-size: 13px; font-weight: 600; color: #111827;"
            " border: none; background: transparent;");
        rowLayout->addWidget(primaryLabel);

        const QString branch = branchOf(path);
        if (!branch.isEmpty()) {
            auto *branchLabel = new QLabel(branch, row);
            branchLabel->setStyleSheet(
                "font-size: 11px; color: #1E40AF;"
                " background: #EFF6FF; border: 1px solid #BFDBFE;"
                " border-radius: 8px; padding: 1px 7px;"
                " font-family: Consolas, monospace;");
            rowLayout->addWidget(branchLabel);
        }
        rowLayout->addStretch();

        // v4-P6-17: project status badge - all shown rows exist (missing
        // paths are filtered), so the badge reads 正常.
        auto *statusLabel = new QLabel(QStringLiteral("正常"), row);
        statusLabel->setStyleSheet(
            "font-size: 11px; color: #15803D;"
            " background: #F0FDF4; border: 1px solid #BBF7D0;"
            " border-radius: 8px; padding: 1px 7px;");
        rowLayout->addWidget(statusLabel);

        const QString opened = lastOpenedText(path);
        if (!opened.isEmpty()) {
            auto *timeLabel = new QLabel(opened, row);
            timeLabel->setStyleSheet(
                "font-size: 11px; color: #9CA3AF; border: none;"
                " background: transparent;");
            rowLayout->addWidget(timeLabel);
        }

        const QString pathText =
            QFontMetrics(primaryLabel->font()).elidedText(
                path, Qt::ElideMiddle, 260);
        auto *pathLabel = new QLabel(pathText, row);
        pathLabel->setToolTip(path);
        pathLabel->setStyleSheet(
            "font-size: 11px; color: #9CA3AF; border: none;"
            " background: transparent;");
        rowLayout->addWidget(pathLabel);

        const QString captured = path;
        // Clickable whole row (no nested buttons).
        class RowClickFilter : public QObject {
        public:
            RowClickFilter(QString path, std::function<void()> onRelease)
                : m_path(std::move(path)), m_onRelease(std::move(onRelease)) {}
            QString m_path;
            std::function<void()> m_onRelease;
            bool m_pressed = false;
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
        };
        auto *filter = new RowClickFilter(
            captured, [this, captured]() { emit projectSelected(captured); });
        row->installEventFilter(filter);
        filter->setParent(row);

        m_recentLayout->addWidget(row);
    }
    if (shown.isEmpty()) {
        auto *emptyLabel = new QLabel(
            "还没有打开过项目", m_recentContainer);
        emptyLabel->setStyleSheet(
            "font-size: 12px; color: #9CA3AF; border: none;"
            " background: transparent; padding: 12px;");
        m_recentLayout->addWidget(emptyLabel);
    }
    m_recentLayout->addStretch();
}

void WelcomeWidget::resizeEvent(QResizeEvent *event) {
    // v4-P6-15: wide -> two columns, narrow -> single column. Only the
    // column direction changes; nothing else is rebuilt.
    m_columnsLayout->setDirection(
        width() < 960 ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    QWidget::resizeEvent(event);
}
