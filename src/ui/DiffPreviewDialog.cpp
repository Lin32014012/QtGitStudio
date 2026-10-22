#include "DiffPreviewDialog.h"

#include <QPlainTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFontDatabase>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QApplication>
#include <QClipboard>
#include <QShortcut>
#include <QSplitter>
#include <QKeySequence>
#include <QTextBlock>

DiffPreviewDialog::DiffPreviewDialog(const QString &diffText,
                                     const QString &commitHash,
                                     const QString &fileName,
                                     QWidget *parent)
    : QDialog(parent) {
    const QString shortHash = commitHash.left(7);
    setWindowTitle(QStringLiteral("Diff 预览 — %1 — %2")
                       .arg(shortHash, fileName));
    resize(1100, 750);
    setMinimumSize(640, 420);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // Toolbar.
    auto *toolRow = new QHBoxLayout();
    auto *zoomInBtn = new QPushButton(QStringLiteral("放大"), this);
    auto *zoomOutBtn = new QPushButton(QStringLiteral("缩小"), this);
    auto *resetBtn = new QPushButton(QStringLiteral("重置字号"), this);
    m_findEdit = new QLineEdit(this);
    m_findEdit->setPlaceholderText(QStringLiteral("查找…"));
    m_findEdit->setFixedWidth(180);
    auto *findBtn = new QPushButton(QStringLiteral("查找"), this);
    auto *wrapCheck = new QCheckBox(QStringLiteral("自动换行"), this);
    wrapCheck->setChecked(true);
    auto *copyBtn = new QPushButton(QStringLiteral("复制全部"), this);
    const QString btnStyle =
        "QPushButton { background: #FFFFFF; color: #1F2937;"
        " border: 1px solid #D1D5DB; border-radius: 6px;"
        " padding: 4px 10px; font-size: 12px; }"
        "QPushButton:hover { background: #F3F4F6; }";
    for (QPushButton *b :
         {zoomInBtn, zoomOutBtn, resetBtn, findBtn, copyBtn}) {
        b->setStyleSheet(btnStyle);
    }
    toolRow->addWidget(zoomInBtn);
    toolRow->addWidget(zoomOutBtn);
    toolRow->addWidget(resetBtn);
    toolRow->addSpacing(10);
    toolRow->addWidget(m_findEdit);
    toolRow->addWidget(findBtn);
    toolRow->addSpacing(10);
    toolRow->addWidget(wrapCheck);
    toolRow->addStretch();
    toolRow->addWidget(copyBtn);
    layout->addLayout(toolRow);

    // Side-by-side panes.
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    m_leftEdit = new QPlainTextEdit(splitter);
    m_rightEdit = new QPlainTextEdit(splitter);
    const QString paneStyle =
        "QPlainTextEdit { border: 1px solid #E5E7EB;"
        " border-radius: 6px; padding: 4px; }";
    m_leftEdit->setStyleSheet(paneStyle);
    m_rightEdit->setStyleSheet(paneStyle);
    m_leftEdit->setReadOnly(true);
    m_rightEdit->setReadOnly(true);
    m_leftEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_rightEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    splitter->addWidget(m_leftEdit);
    splitter->addWidget(m_rightEdit);
    splitter->setSizes({550, 550});
    layout->addWidget(splitter, 1);

    // v4-P6-17: parse the unified diff into old (red) / new (green)
    // columns. Context lines appear in both; +/- lines in their own
    // column with a soft background highlight; @@ separators in both.
    // v4-P6-21 ISSUE-11: rows are ALIGNED - every logical visual row
    // occupies one line in BOTH columns (deleted lines pair with the
    // following added lines; leftovers flush as empty on the other
    // side), so context after unequal edits stays at the same index.
    struct AlignedRow {
        QString left;
        QString right;
    };
    QList<AlignedRow> rows;
    QStringList pendingDeletes;
    const QStringList lines = diffText.split('\n');
    for (const QString &line : lines) {
        if (line.startsWith(QStringLiteral("@@"))
            || line.startsWith(QStringLiteral("diff --git"))) {
            while (!pendingDeletes.isEmpty()) {
                rows.append({pendingDeletes.takeFirst(), QString()});
            }
            rows.append({line, line});
        } else if (line.startsWith(QLatin1Char('-'))) {
            pendingDeletes.append(line);
        } else if (line.startsWith(QLatin1Char('+'))) {
            if (!pendingDeletes.isEmpty()) {
                rows.append({pendingDeletes.takeFirst(), line});
            } else {
                rows.append({QString(), line});
            }
        } else {
            while (!pendingDeletes.isEmpty()) {
                rows.append({pendingDeletes.takeFirst(), QString()});
            }
            rows.append({line, line});
        }
    }
    while (!pendingDeletes.isEmpty()) {
        rows.append({pendingDeletes.takeFirst(), QString()});
    }
    for (const AlignedRow &row : rows) {
        m_leftEdit->appendPlainText(row.left);
        m_rightEdit->appendPlainText(row.right);
    }

    // Highlight +/- lines.
    auto highlight = [](QPlainTextEdit *edit, bool added) {
        QTextDocument *doc = edit->document();
        for (QTextBlock block = doc->begin(); block.isValid();
             block = block.next()) {
            const QString text = block.text();
            if (text.isEmpty()) {
                continue;
            }
            QTextCursor cursor(block);
            QTextCharFormat fmt;
            if (text.startsWith(QStringLiteral("@@"))) {
                fmt.setBackground(QColor("#F1F5F9"));
                fmt.setForeground(QColor("#475569"));
            } else if (added && text.startsWith(QLatin1Char('+'))) {
                fmt.setBackground(QColor("#DCFCE7"));
                fmt.setForeground(QColor("#14532D"));
            } else if (!added && text.startsWith(QLatin1Char('-'))) {
                fmt.setBackground(QColor("#FEE2E2"));
                fmt.setForeground(QColor("#7F1D1D"));
            } else {
                continue;
            }
            cursor.select(QTextCursor::LineUnderCursor);
            cursor.mergeCharFormat(fmt);
        }
    };
    highlight(m_leftEdit, false);
    highlight(m_rightEdit, true);

    // Fonts: monospace 12pt default.
    applyFontSize();

    // Zoom.
    connect(zoomInBtn, &QPushButton::clicked, this, &DiffPreviewDialog::zoomIn);
    connect(zoomOutBtn, &QPushButton::clicked, this,
            &DiffPreviewDialog::zoomOut);
    connect(resetBtn, &QPushButton::clicked, this,
            &DiffPreviewDialog::resetZoom);
    auto *plusShortcut =
        new QShortcut(QKeySequence(QStringLiteral("Ctrl++")), this);
    connect(plusShortcut, &QShortcut::activated, this,
            &DiffPreviewDialog::zoomIn);
    auto *minusShortcut =
        new QShortcut(QKeySequence(QStringLiteral("Ctrl+-")), this);
    connect(minusShortcut, &QShortcut::activated, this,
            &DiffPreviewDialog::zoomOut);
    auto *zeroShortcut =
        new QShortcut(QKeySequence(QStringLiteral("Ctrl+0")), this);
    connect(zeroShortcut, &QShortcut::activated, this,
            &DiffPreviewDialog::resetZoom);

    // Find (both panes).
    connect(findBtn, &QPushButton::clicked, this,
            &DiffPreviewDialog::findNext);
    connect(m_findEdit, &QLineEdit::returnPressed, this,
            &DiffPreviewDialog::findNext);

    // Wrap.
    connect(wrapCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_leftEdit->setLineWrapMode(
            on ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
        m_rightEdit->setLineWrapMode(
            on ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    });

    // Copy all (current pane or both).
    connect(copyBtn, &QPushButton::clicked, this,
            &DiffPreviewDialog::copyAll);
}

void DiffPreviewDialog::applyFontSize() {
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    for (QPlainTextEdit *edit : {m_leftEdit, m_rightEdit}) {
        QFont f = mono;
        f.setPointSize(m_fontSize);
        edit->setFont(f);
    }
}

void DiffPreviewDialog::zoomIn() {
    if (m_fontSize < 28) {
        ++m_fontSize;
        applyFontSize();
    }
}

void DiffPreviewDialog::zoomOut() {
    if (m_fontSize > 8) {
        --m_fontSize;
        applyFontSize();
    }
}

void DiffPreviewDialog::resetZoom() {
    m_fontSize = 12;
    applyFontSize();
}

void DiffPreviewDialog::findNext() {
    const QString needle = m_findEdit->text();
    if (needle.isEmpty()) {
        return;
    }
    for (QPlainTextEdit *edit : {m_rightEdit, m_leftEdit}) {
        QTextDocument *doc = edit->document();
        QTextCursor cursor = edit->textCursor();
        QTextCursor hit =
            doc->find(needle, cursor.isNull() ? QTextCursor(doc)
                                              : cursor);
        if (hit.isNull()) {
            // wrap around
            hit = doc->find(needle, 0);
        }
        if (!hit.isNull()) {
            edit->setTextCursor(hit);
            edit->ensureCursorVisible();
            return;
        }
    }
}

void DiffPreviewDialog::toggleWrap(bool on) {
    m_leftEdit->setLineWrapMode(
        on ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    m_rightEdit->setLineWrapMode(
        on ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
}

void DiffPreviewDialog::copyAll() {
    const QString combined =
        QStringLiteral("%1\n%2").arg(m_leftEdit->toPlainText(),
                                     m_rightEdit->toPlainText());
    QApplication::clipboard()->setText(combined);
}
