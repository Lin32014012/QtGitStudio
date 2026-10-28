#include "PracticeBanner.h"

#include "PracticeService.h"
#include "Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace {
const char *kPassedKey = "practices/passed/";
}

PracticeBanner::PracticeBanner(QWidget *parent) : QFrame(parent) {
    setObjectName(QStringLiteral("practiceBanner"));
    setProperty("role", QStringLiteral("strip"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 9, 14, 9);
    outer->setSpacing(6);

    auto *row = new QHBoxLayout();
    row->setSpacing(10);

    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("practiceBannerTitle"));
    m_title->setProperty("role", QStringLiteral("bodyText"));
    m_title->setWordWrap(true);
    row->addWidget(m_title, 1);

    m_hintButton = new QPushButton(QStringLiteral("显示提示"), this);
    m_hintButton->setProperty("role", QStringLiteral("ghost"));
    m_hintButton->setCursor(Qt::PointingHandCursor);
    row->addWidget(m_hintButton, 0, Qt::AlignVCenter);

    m_checkButton = new QPushButton(QStringLiteral("检查完成"), this);
    m_checkButton->setProperty("role", QStringLiteral("primary"));
    m_checkButton->setCursor(Qt::PointingHandCursor);
    row->addWidget(m_checkButton, 0, Qt::AlignVCenter);
    outer->addLayout(row);

    m_result = new QLabel(this);
    m_result->setObjectName(QStringLiteral("practiceBannerResult"));
    m_result->setProperty("role", QStringLiteral("hint"));
    m_result->setWordWrap(true);
    m_result->hide();
    outer->addWidget(m_result);

    connect(m_checkButton, &QPushButton::clicked, this, &PracticeBanner::check);
    connect(m_hintButton, &QPushButton::clicked, this, &PracticeBanner::toggleHint);

    hide();
}

void PracticeBanner::setPractice(const QString &practiceId, const QString &repoPath) {
    if (practiceId.isEmpty() || repoPath.isEmpty()) {
        clear();
        return;
    }
    m_practiceId = practiceId;
    m_repoPath = repoPath;
    m_hintVisible = false;

    const PracticeDefinition definition = PracticeService::definition(practiceId);
    m_title->setText(QStringLiteral("练习 · %1：%2").arg(definition.title,
                                                         definition.goal));
    m_title->setToolTip(definition.hint);
    m_hintButton->setText(QStringLiteral("显示提示"));
    m_result->clear();
    m_result->hide();
    show();
}

void PracticeBanner::clear() {
    m_practiceId.clear();
    m_repoPath.clear();
    m_hintVisible = false;
    m_result->clear();
    m_result->hide();
    hide();
}

QString PracticeBanner::resultText() const {
    return m_result->text();
}

void PracticeBanner::toggleHint() {
    m_hintVisible = !m_hintVisible;
    const PracticeDefinition definition = PracticeService::definition(m_practiceId);
    if (m_hintVisible) {
        m_result->setText(QStringLiteral("提示：%1").arg(definition.hint));
        m_result->setProperty("state", QStringLiteral("info"));
        Theme::restyle(m_result);
        m_result->show();
        m_hintButton->setText(QStringLiteral("隐藏提示"));
    } else {
        m_result->hide();
        m_hintButton->setText(QStringLiteral("显示提示"));
    }
}

void PracticeBanner::check() {
    if (m_practiceId.isEmpty() || m_repoPath.isEmpty()) {
        return;
    }
    const PracticeCheck check = PracticeService::check(m_practiceId, m_repoPath);
    m_result->setText(check.detail);
    m_result->setProperty("state", check.passed ? QStringLiteral("ok")
                                               : QStringLiteral("warn"));
    Theme::restyle(m_result);
    m_result->show();

    if (check.passed) {
        // Remember the pass exactly like the dialog does, so both surfaces
        // agree about progress.
        QSettings settings;
        settings.setValue(QString::fromLatin1(kPassedKey) + m_practiceId, true);
        emit practicePassed(m_practiceId);
    }
}
