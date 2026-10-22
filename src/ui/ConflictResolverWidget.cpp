#include "ConflictResolverWidget.h"
#include "TextEncoding.h"

#include "GitConflictService.h"
#include "GitConflictResolutionService.h"

#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QResizeEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QTextBlock>
#include <QVBoxLayout>

namespace {

class NumberedPlainTextEdit;

class LineNumberArea final : public QWidget {
public:
    explicit LineNumberArea(NumberedPlainTextEdit *editor);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    NumberedPlainTextEdit *m_editor;
};

class NumberedPlainTextEdit final : public QPlainTextEdit {
public:
    explicit NumberedPlainTextEdit(QWidget *parent = nullptr)
        : QPlainTextEdit(parent)
        , m_lineNumberArea(new LineNumberArea(this)) {
        setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        setLineWrapMode(QPlainTextEdit::NoWrap);
        connect(this, &QPlainTextEdit::blockCountChanged, this,
                [this](int) { updateLineNumberAreaWidth(); });
        connect(this, &QPlainTextEdit::updateRequest, this,
                [this](const QRect &rect, int dy) { updateLineNumberArea(rect, dy); });
        updateLineNumberAreaWidth();
    }

    int lineNumberAreaWidth() const {
        int digits = 1;
        int maximum = qMax(1, blockCount());
        while (maximum >= 10) {
            maximum /= 10;
            ++digits;
        }
        return 8 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    }

    void lineNumberAreaPaintEvent(QPaintEvent *event) {
        QPainter painter(m_lineNumberArea);
        painter.fillRect(event->rect(), QColor(QStringLiteral("#F3F4F6")));
        QTextBlock block = firstVisibleBlock();
        int number = block.blockNumber();
        int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        while (block.isValid() && top <= event->rect().bottom()) {
            const int bottom = top + qRound(blockBoundingRect(block).height());
            if (block.isVisible() && bottom >= event->rect().top()) {
                painter.setPen(QColor(QStringLiteral("#6B7280")));
                painter.drawText(0, top, m_lineNumberArea->width() - 4,
                                 fontMetrics().height(), Qt::AlignRight,
                                 QString::number(number + 1));
            }
            block = block.next();
            ++number;
            top = bottom;
        }
    }

protected:
    void resizeEvent(QResizeEvent *event) override {
        QPlainTextEdit::resizeEvent(event);
        const QRect rect = contentsRect();
        m_lineNumberArea->setGeometry(QRect(rect.left(), rect.top(),
                                            lineNumberAreaWidth(), rect.height()));
    }

private:
    void updateLineNumberAreaWidth() {
        setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
    }

    void updateLineNumberArea(const QRect &rect, int dy) {
        if (dy != 0) {
            m_lineNumberArea->scroll(0, dy);
        } else {
            m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
        }
        if (rect.contains(viewport()->rect())) {
            updateLineNumberAreaWidth();
        }
    }

    LineNumberArea *m_lineNumberArea;
};

LineNumberArea::LineNumberArea(NumberedPlainTextEdit *editor)
    : QWidget(editor)
    , m_editor(editor) {
}

QSize LineNumberArea::sizeHint() const {
    return QSize(m_editor->lineNumberAreaWidth(), 0);
}

void LineNumberArea::paintEvent(QPaintEvent *event) {
    m_editor->lineNumberAreaPaintEvent(event);
}

QString friendlyConflictType(ConflictType type) {
    switch (type) {
    case ConflictType::BothModified: return QStringLiteral("Both Modified");
    case ConflictType::AddedByUs: return QStringLiteral("Added By Us");
    case ConflictType::AddedByThem: return QStringLiteral("Added By Them");
    case ConflictType::DeletedByUs: return QStringLiteral("Deleted By Us");
    case ConflictType::DeletedByThem: return QStringLiteral("Deleted By Them");
    case ConflictType::BothAdded: return QStringLiteral("Both Added");
    case ConflictType::BothDeleted: return QStringLiteral("Both Deleted");
    case ConflictType::RenameDelete: return QStringLiteral("Rename/Delete");
    case ConflictType::RenameRename: return QStringLiteral("Rename/Rename");
    case ConflictType::Unknown: return QStringLiteral("Unknown Conflict");
    }
    return QStringLiteral("Unknown Conflict");
}

QString operationTitle(ConflictOperationType type) {
    switch (type) {
    case ConflictOperationType::Merge: return QStringLiteral("Merge Conflict");
    case ConflictOperationType::Rebase: return QStringLiteral("Rebase Conflict");
    case ConflictOperationType::CherryPick: return QStringLiteral("Cherry-pick Conflict");
    case ConflictOperationType::Revert: return QStringLiteral("Revert Conflict");
    case ConflictOperationType::Unknown: return QStringLiteral("Unknown Conflict");
    }
    return QStringLiteral("Unknown Conflict");
}

QString operationHint(ConflictOperationType type) {
    if (type == ConflictOperationType::Merge) {
        return QStringLiteral("Merge 中：Ours 是当前分支，Theirs 是合并进来的分支。");
    }
    if (type == ConflictOperationType::Rebase) {
        return QStringLiteral("Rebase 中请按 Git Stage 2 / Stage 3 标签审阅；不要按普通合并的直觉判断。");
    }
    return QStringLiteral("版本内容以 Git Stage 1 / 2 / 3 标签为准。Accept 仅更新当前解决方案；点击 Apply 后才会写入并暂存。");
}

QString noBaseText() {
    return QStringLiteral("[No Base Version]");
}

QString deletedText() {
    return QStringLiteral("[Deleted]");
}

bool isDeletionConflict(ConflictType type) {
    return type == ConflictType::DeletedByUs || type == ConflictType::DeletedByThem
        || type == ConflictType::BothDeleted || type == ConflictType::RenameDelete;
}

} // namespace

ConflictResolverWidget::ConflictResolverWidget(GitConflictService *service,
                                               QWidget *parent)
    : QWidget(parent)
    , m_service(service) {
    Q_ASSERT(m_service);
    buildUi();
}

void ConflictResolverWidget::buildUi() {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    m_operationBanner = new QLabel(this);
    m_operationBanner->setObjectName(QStringLiteral("conflictOperationBanner"));
    m_operationBanner->setStyleSheet(
        QStringLiteral("font-size: 18px; font-weight: 700; color: #991B1B;"));
    layout->addWidget(m_operationBanner);
    m_operationHint = new QLabel(this);
    m_operationHint->setObjectName(QStringLiteral("conflictOperationHint"));
    m_operationHint->setWordWrap(true);
    m_operationHint->setStyleSheet(QStringLiteral("color: #4B5563;"));
    layout->addWidget(m_operationHint);

    m_emptyState = new QLabel(QStringLiteral("✓ 当前仓库没有未解决冲突"), this);
    m_emptyState->setObjectName(QStringLiteral("conflictResolverEmptyState"));
    m_emptyState->setStyleSheet(QStringLiteral("color: #15803D; padding: 16px;"));
    m_emptyState->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_emptyState);

    m_resolverContent = new QSplitter(Qt::Horizontal, this);
    m_resolverContent->setObjectName(QStringLiteral("conflictResolverRootSplitter"));
    m_fileList = new QListWidget(m_resolverContent);
    m_fileList->setObjectName(QStringLiteral("conflictFileList"));
    m_fileList->setMinimumWidth(220);
    m_fileList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_resolverContent->addWidget(m_fileList);

    auto *contentSplitter = new QSplitter(Qt::Vertical, m_resolverContent);
    contentSplitter->setObjectName(QStringLiteral("conflictResolverContentSplitter"));
    auto *sources = new QSplitter(Qt::Horizontal, contentSplitter);
    sources->setObjectName(QStringLiteral("conflictSourcesSplitter"));
    const auto makeSource = [sources](const QString &title, const QString &name) {
        auto *panel = new QWidget(sources);
        auto *panelLayout = new QVBoxLayout(panel);
        panelLayout->setContentsMargins(0, 0, 0, 0);
        auto *label = new QLabel(title, panel);
        label->setStyleSheet(QStringLiteral("font-weight: 700; color: #374151;"));
        panelLayout->addWidget(label);
        auto *editor = new NumberedPlainTextEdit(panel);
        editor->setObjectName(name);
        editor->setReadOnly(true);
        editor->setMinimumHeight(150);
        panelLayout->addWidget(editor, 1);
        sources->addWidget(panel);
        return editor;
    };
    m_oursEditor = makeSource(QStringLiteral("Ours (Stage 2)"), QStringLiteral("conflictOursEditor"));
    m_baseEditor = makeSource(QStringLiteral("Base (Stage 1)"), QStringLiteral("conflictBaseEditor"));
    m_theirsEditor = makeSource(QStringLiteral("Theirs (Stage 3)"), QStringLiteral("conflictTheirsEditor"));
    contentSplitter->addWidget(sources);

    auto *resultPanel = new QWidget(contentSplitter);
    auto *resultLayout = new QVBoxLayout(resultPanel);
    resultLayout->setContentsMargins(0, 0, 0, 0);
    auto *resultHeader = new QHBoxLayout();
    auto *resultTitle = new QLabel(QStringLiteral("Result (应用前预览)"), resultPanel);
    resultTitle->setStyleSheet(QStringLiteral("font-weight: 700; color: #1D4ED8;"));
    resultHeader->addWidget(resultTitle);
    resultHeader->addStretch();
    m_hunkSelector = new QComboBox(resultPanel);
    m_hunkSelector->setObjectName(QStringLiteral("conflictHunkSelector"));
    resultHeader->addWidget(m_hunkSelector);
    resultLayout->addLayout(resultHeader);
    auto *planHint = new QLabel(
        QStringLiteral("Accept 操作仅更新当前解决方案；点击 Apply 后才会写入并暂存。"),
        resultPanel);
    planHint->setObjectName(QStringLiteral("conflictResolutionPlanHint"));
    planHint->setWordWrap(true);
    planHint->setStyleSheet(QStringLiteral("color: #4B5563;"));
    resultLayout->addWidget(planHint);
    m_resultEditor = new NumberedPlainTextEdit(resultPanel);
    m_resultEditor->setObjectName(QStringLiteral("conflictResultEditor"));
    m_resultEditor->setMinimumHeight(180);
    resultLayout->addWidget(m_resultEditor, 1);
    auto *actions = new QHBoxLayout();
    m_acceptOursButton = new QPushButton(QStringLiteral("Accept Ours"), resultPanel);
    m_acceptOursButton->setObjectName(QStringLiteral("acceptOursButton"));
    m_acceptTheirsButton = new QPushButton(QStringLiteral("Accept Theirs"), resultPanel);
    m_acceptTheirsButton->setObjectName(QStringLiteral("acceptTheirsButton"));
    m_acceptBothButton = new QPushButton(QStringLiteral("Accept Both"), resultPanel);
    m_acceptBothButton->setObjectName(QStringLiteral("acceptBothButton"));
    m_deleteButton = new QPushButton(QStringLiteral("计划删除文件"), resultPanel);
    m_deleteButton->setObjectName(QStringLiteral("planDeleteConflictFileButton"));
    actions->addWidget(m_acceptOursButton);
    actions->addWidget(m_acceptTheirsButton);
    actions->addWidget(m_acceptBothButton);
    actions->addWidget(m_deleteButton);
    actions->addStretch();
    resultLayout->addLayout(actions);
    contentSplitter->addWidget(resultPanel);
    m_resolverContent->addWidget(contentSplitter);
    m_resolverContent->setStretchFactor(1, 1);
    contentSplitter->setStretchFactor(0, 1);
    contentSplitter->setStretchFactor(1, 1);
    layout->addWidget(m_resolverContent, 1);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("conflictResolverStatus"));
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto *workflow = new QHBoxLayout();
    m_workflowStatus = new QLabel(this);
    m_workflowStatus->setObjectName(QStringLiteral("conflictWorkflowStatus"));
    m_workflowStatus->setWordWrap(true);
    workflow->addWidget(m_workflowStatus, 1);
    m_applyCurrentButton = new QPushButton(QStringLiteral("应用当前方案"), this);
    m_applyCurrentButton->setObjectName(QStringLiteral("applyCurrentConflictButton"));
    m_applyAllButton = new QPushButton(QStringLiteral("应用全部已准备方案"), this);
    m_applyAllButton->setObjectName(QStringLiteral("applyAllReadyConflictButton"));
    m_continueButton = new QPushButton(this);
    m_continueButton->setObjectName(QStringLiteral("continueConflictOperationButton"));
    m_abortButton = new QPushButton(this);
    m_abortButton->setObjectName(QStringLiteral("abortConflictOperationButton"));
    workflow->addWidget(m_applyCurrentButton);
    workflow->addWidget(m_applyAllButton);
    workflow->addWidget(m_continueButton);
    workflow->addWidget(m_abortButton);
    layout->addLayout(workflow);

    connect(m_fileList, &QListWidget::itemSelectionChanged, this,
            &ConflictResolverWidget::onFileSelectionChanged);
    connect(m_resultEditor, &QPlainTextEdit::textChanged, this,
            &ConflictResolverWidget::onResultChanged);
    connect(m_acceptOursButton, &QPushButton::clicked, this,
            &ConflictResolverWidget::acceptOurs);
    connect(m_acceptTheirsButton, &QPushButton::clicked, this,
            &ConflictResolverWidget::acceptTheirs);
    connect(m_acceptBothButton, &QPushButton::clicked, this,
            &ConflictResolverWidget::acceptBoth);
    connect(m_deleteButton, &QPushButton::clicked, this,
            &ConflictResolverWidget::planDelete);
    connect(m_applyCurrentButton, &QPushButton::clicked, this,
            &ConflictResolverWidget::applyCurrentPlan);
    connect(m_applyAllButton, &QPushButton::clicked, this,
            &ConflictResolverWidget::applyAllReadyPlans);
    connect(m_continueButton, &QPushButton::clicked, this,
            &ConflictResolverWidget::continueOperation);
    connect(m_abortButton, &QPushButton::clicked, this,
            &ConflictResolverWidget::abortOperation);
    updateWorkflowControls();
}

void ConflictResolverWidget::setResolutionService(GitConflictResolutionService *service) {
    m_resolutionService = service;
    updateWorkflowControls();
}

void ConflictResolverWidget::setSnapshot(const ConflictSnapshot &snapshot) {
    m_completedOperation = ConflictOperationType::Unknown;
    applySnapshot(snapshot);
}

void ConflictResolverWidget::setCompletedSnapshot(const ConflictSnapshot &snapshot,
                                                   ConflictOperationType operation) {
    m_completedOperation = operation;
    applySnapshot(snapshot);
}

void ConflictResolverWidget::applySnapshot(const ConflictSnapshot &snapshot) {
    ++m_loadRequest;
    m_snapshot = snapshot;
    m_serviceGeneration = m_service->repositoryGeneration();
    m_currentPath.clear();
    m_states.clear();
    m_fileList->clear();
    m_operationBanner->setText(operationTitle(snapshot.operationType));
    m_operationHint->setText(operationHint(snapshot.operationType));
    const bool empty = !snapshot.hasConflicts || snapshot.files.isEmpty();
    m_emptyState->setVisible(empty);
    m_resolverContent->setVisible(!empty);
    m_resultEditor->setEnabled(!empty);
    m_hunkSelector->setEnabled(!empty);
    if (empty) {
        setResultText({});
        m_status->clear();
        updateHunkControls(nullptr);
        updateWorkflowControls();
        emit conflictSnapshotChanged(m_snapshot);
        return;
    }

    for (const ConflictFile &file : snapshot.files) {
        FileState state;
        state.file = file;
        state.plan.repositoryPath = snapshot.repositoryPath;
        state.plan.filePath = file.path;
        state.plan.conflictType = file.type;
        state.plan.sessionGeneration = m_serviceGeneration;
        state.plan.expectedBaseObjectId = file.baseObjectId;
        state.plan.expectedOursObjectId = file.oursObjectId;
        state.plan.expectedTheirsObjectId = file.theirsObjectId;
        m_states.insert(file.path, state);
        auto *item = new QListWidgetItem(m_fileList);
        item->setData(Qt::UserRole, file.path);
        updateFileItem(file.path);
    }
    if (m_fileList->count() > 0) {
        m_fileList->setCurrentRow(0);
    }
    updateWorkflowControls();
    emit conflictSnapshotChanged(m_snapshot);
}

ConflictResolutionPlan ConflictResolverWidget::resolutionPlanForFile(const QString &path) const {
    const FileState *state = stateForPath(path);
    return state ? state->plan : ConflictResolutionPlan{};
}

QList<ConflictResolutionPlan> ConflictResolverWidget::resolutionPlans() const {
    QList<ConflictResolutionPlan> result;
    for (const ConflictFile &file : m_snapshot.files) {
        if (const FileState *state = stateForPath(file.path)) {
            result.append(state->plan);
        }
    }
    return result;
}

int ConflictResolverWidget::unresolvedHunksForTesting(const QString &path) const {
    const FileState *state = stateForPath(path);
    return state ? state->plan.unresolvedHunks : 0;
}

bool ConflictResolverWidget::isFileReadyForTesting(const QString &path) const {
    const FileState *state = stateForPath(path);
    return state && state->plan.ready;
}

bool ConflictResolverWidget::isEmptyStateForTesting() const {
    return m_emptyState->isVisible();
}

void ConflictResolverWidget::onFileSelectionChanged() {
    const QListWidgetItem *item = m_fileList->currentItem();
    if (!item) {
        return;
    }
    selectFile(item->data(Qt::UserRole).toString());
}

void ConflictResolverWidget::selectFile(const QString &path) {
    if (path.isEmpty() || !m_states.contains(path) || path == m_currentPath) {
        return;
    }
    ++m_loadRequest;
    m_currentPath = path;
    FileState *state = currentState();
    if (state->loaded || state->unavailable) {
        showFileState();
        return;
    }
    // A previous selection may still have an in-flight read. Its callback is
    // request-fenced below; reselecting the file starts a fresh read rather
    // than leaving the newly selected item permanently "loading".
    state->loading = false;
    loadCurrentFile();
}

void ConflictResolverWidget::loadCurrentFile() {
    FileState *state = currentState();
    if (!state || state->loading || !isResolverSessionCurrent()) {
        return;
    }
    state->loading = true;
    const QString path = m_currentPath;
    const quint64 request = ++m_loadRequest;
    m_status->setText(QStringLiteral("正在按需加载 %1 的冲突内容…").arg(path));
    QPointer<ConflictResolverWidget> guard(this);
    m_service->loadConflictContentAsync(
        state->file, [guard, path, request](ConflictFileContentResult result) {
            if (!guard) {
                return;
            }
            ConflictResolverWidget *self = guard.data();
            if (request != self->m_loadRequest || path != self->m_currentPath
                || !self->isResolverSessionCurrent() || result.stale) {
                return;
            }
            FileState *current = self->currentState();
            if (!current) {
                return;
            }
            current->loading = false;
            if (result.error.hasError()) {
                current->unavailable = true;
                self->m_status->setText(QStringLiteral("无法加载冲突内容：%1").arg(result.error.message));
                self->showFileState();
                return;
            }
            current->content = std::move(result.content);
            if (current->content.contentTruncated) {
                current->unavailable = true;
                self->m_status->setText(QStringLiteral("文件过大，无法在内置 Resolver 中完整编辑。"));
                self->showFileState();
                return;
            }
            const QString absolutePath = QDir(self->m_snapshot.repositoryPath).filePath(path);
            QFile worktree(absolutePath);
            QByteArray workingContent;
            const bool worktreeExists = worktree.exists();
            if (worktreeExists && worktree.open(QIODevice::ReadOnly)) {
                workingContent = worktree.readAll();
            }
            current->plan.expectedWorkingTreeExists = worktreeExists;
            current->plan.expectedWorkingTreeSha256 = worktreeExists
                ? QString::fromLatin1(QCryptographicHash::hash(
                    workingContent, QCryptographicHash::Sha256).toHex())
                : QString();
            current->binary = current->content.isBinary || workingContent.contains('\0');
            current->plan.binary = current->binary;
            current->loaded = true;
            const bool deletion = isDeletionConflict(current->file.type)
                && (!current->file.hasOurs || !current->file.hasTheirs);
            if (!current->binary && !deletion) {
                // Decode in the file's own encoding (UTF-8 / BOM / GB18030...);
                // write-back re-encodes with the same encoding.
                current->resultContent = TextEncoding::decode(workingContent).text;
                current->parseResult = ConflictMarkerParser::parse(current->resultContent);
                current->initialHunkCount = current->parseResult.hunks.size();
                self->updateParseAndPlan(current, ResolutionAction::None);
            } else {
                current->plan.action = ResolutionAction::None;
                current->plan.ready = false;
                current->plan.unresolvedHunks = 0;
            }
            self->showFileState();
        });
}

void ConflictResolverWidget::showFileState() {
    FileState *state = currentState();
    if (!state) {
        return;
    }
    if (state->binary) {
        m_baseEditor->setPlainText(state->file.hasBase
            ? QStringLiteral("[Binary content — object %1]").arg(state->file.baseObjectId)
            : noBaseText());
        m_oursEditor->setPlainText(state->file.hasOurs
            ? QStringLiteral("[Binary content — object %1]").arg(state->file.oursObjectId)
            : deletedText());
        m_theirsEditor->setPlainText(state->file.hasTheirs
            ? QStringLiteral("[Binary content — object %1]").arg(state->file.theirsObjectId)
            : deletedText());
    } else {
        setSourceText(m_baseEditor, state->content.baseContent, state->file.hasBase, noBaseText());
        setSourceText(m_oursEditor, state->content.oursContent, state->file.hasOurs, deletedText());
        setSourceText(m_theirsEditor, state->content.theirsContent, state->file.hasTheirs, deletedText());
    }
    if (state->unavailable) {
        setResultText({});
        m_resultEditor->setEnabled(false);
        updateHunkControls(state);
        return;
    }
    if (state->binary) {
        setResultText(QStringLiteral("Binary conflict\n文本编辑不可用。可仅生成 Use Ours 或 Use Theirs 的内存方案。"));
        m_resultEditor->setEnabled(false);
        m_status->setText(QStringLiteral("Binary conflict：不会打开文本编辑器。"));
        updateHunkControls(state);
        return;
    }
    if (isDeletionConflict(state->file.type)
        && (!state->file.hasOurs || !state->file.hasTheirs)) {
        m_resultEditor->setEnabled(false);
        setResultText({});
        m_status->setText(QStringLiteral("此冲突包含已删除的一侧；请选择保留一侧或仅生成删除意图。"));
        updateHunkControls(state);
        return;
    }
    m_resultEditor->setEnabled(true);
    setResultText(state->resultContent);
    if (!state->parseResult.isValid()) {
        m_status->setText(QStringLiteral("冲突标记格式错误：%1").arg(state->parseResult.error));
    }
    updateHunkControls(state);
}

void ConflictResolverWidget::setResultText(const QString &text) {
    m_settingResult = true;
    m_resultEditor->setPlainText(text);
    m_settingResult = false;
}

void ConflictResolverWidget::onResultChanged() {
    if (m_settingResult) {
        return;
    }
    FileState *state = currentState();
    if (!state || !state->loaded || state->binary || state->unavailable) {
        return;
    }
    state->resultContent = m_resultEditor->toPlainText();
    state->dirty = true;
    state->parseResult = ConflictMarkerParser::parse(state->resultContent);
    updateParseAndPlan(state, ResolutionAction::Manual);
    updateHunkControls(state);
}

void ConflictResolverWidget::acceptOurs() {
    applyCurrentHunk(ResolutionAction::Ours);
}

void ConflictResolverWidget::acceptTheirs() {
    applyCurrentHunk(ResolutionAction::Theirs);
}

void ConflictResolverWidget::acceptBoth() {
    applyCurrentHunk(ResolutionAction::Both);
}

void ConflictResolverWidget::planDelete() {
    FileState *state = currentState();
    if (!state || !isDeletionConflict(state->file.type)) {
        return;
    }
    state->plan.action = ResolutionAction::Delete;
    state->plan.deleteFile = true;
    state->plan.ready = true;
    state->plan.unresolvedHunks = 0;
    state->plan.resultContent.clear();
    updateFileItem(m_currentPath);
    m_status->setText(QStringLiteral("删除意图已准备，仅保存在内存中。"));
    emit resolutionPlanChanged(state->plan);
}

void ConflictResolverWidget::applyCurrentHunk(ResolutionAction action) {
    FileState *state = currentState();
    if (!state || state->unavailable) {
        return;
    }
    if (state->binary || (isDeletionConflict(state->file.type)
                          && (!state->file.hasOurs || !state->file.hasTheirs))) {
        prepareSidePlan(action);
        return;
    }
    if (!state->parseResult.isValid() || state->parseResult.hunks.isEmpty()) {
        return;
    }
    const int index = m_hunkSelector->currentIndex();
    if (index < 0 || index >= state->parseResult.hunks.size()) {
        return;
    }
    const ConflictMarkerHunk &hunk = state->parseResult.hunks[index];
    QString replacement;
    if (action == ResolutionAction::Ours) {
        replacement = hunk.ours;
    } else if (action == ResolutionAction::Theirs) {
        replacement = hunk.theirs;
    } else {
        replacement = hunk.ours + hunk.theirs;
    }
    state->resultContent.replace(hunk.startOffset, hunk.endOffset - hunk.startOffset,
                                 replacement);
    state->dirty = true;
    state->parseResult = ConflictMarkerParser::parse(state->resultContent);
    updateParseAndPlan(state, action);
    setResultText(state->resultContent);
    updateHunkControls(state);
}

void ConflictResolverWidget::prepareSidePlan(ResolutionAction action) {
    FileState *state = currentState();
    if (!state || (action != ResolutionAction::Ours && action != ResolutionAction::Theirs)) {
        return;
    }
    const bool ours = action == ResolutionAction::Ours;
    const bool exists = ours ? state->file.hasOurs : state->file.hasTheirs;
    state->plan.action = exists ? action : ResolutionAction::Delete;
    state->plan.deleteFile = !exists;
    state->plan.sourceObjectId = ours ? state->file.oursObjectId : state->file.theirsObjectId;
    state->plan.resultContent.clear();
    state->plan.ready = true;
    state->plan.unresolvedHunks = 0;
    updateFileItem(m_currentPath);
    m_status->setText(QStringLiteral("解决方案已准备，仅保存在内存中。"));
    emit resolutionPlanChanged(state->plan);
}

void ConflictResolverWidget::updateParseAndPlan(FileState *state,
                                                 ResolutionAction action) {
    state->plan.resultContent = state->resultContent;
    state->plan.deleteFile = false;
    state->plan.sourceObjectId.clear();
    state->plan.unresolvedHunks = state->parseResult.isValid()
        ? state->parseResult.hunks.size() : -1;
    state->plan.ready = state->parseResult.isValid()
        && state->parseResult.hunks.isEmpty();
    if (action != ResolutionAction::None) {
        state->plan.action = action;
    }
    if (state->plan.ready && state->plan.action == ResolutionAction::None) {
        state->plan.action = ResolutionAction::Manual;
    }
    updateFileItem(state->file.path);
    if (state->plan.ready) {
        m_status->setText(QStringLiteral("解决方案已准备，仅保存在内存中。"));
        emit resolutionPlanChanged(state->plan);
    } else if (state->parseResult.isValid()) {
        const int remaining = state->parseResult.hunks.size();
        m_status->setText(QStringLiteral("%1 个冲突块，已解决 %2，剩余 %3。")
                              .arg(state->initialHunkCount)
                              .arg(qMax(0, state->initialHunkCount - remaining))
                              .arg(remaining));
    }
}

void ConflictResolverWidget::updateFileItem(const QString &path) {
    for (int i = 0; i < m_fileList->count(); ++i) {
        QListWidgetItem *item = m_fileList->item(i);
        if (item->data(Qt::UserRole).toString() != path) {
            continue;
        }
        const FileState *state = stateForPath(path);
        if (!state) {
            return;
        }
        const QString fileName = QFileInfo(path).fileName();
        QString text = QStringLiteral("%1\n%2\n%3")
                           .arg(fileName, friendlyConflictType(state->file.type), path);
        if (state->plan.ready) {
            text += QStringLiteral("\n解决方案已准备");
        } else if (state->loaded && state->parseResult.isValid()) {
            text += QStringLiteral("\n剩余 %1 个冲突块").arg(state->plan.unresolvedHunks);
        }
        item->setText(text);
        item->setToolTip(path);
        return;
    }
}

void ConflictResolverWidget::updateHunkControls(FileState *state) {
    m_hunkSelector->blockSignals(true);
    m_hunkSelector->clear();
    const bool validText = state && state->loaded && !state->binary
        && !state->unavailable && state->parseResult.isValid();
    if (validText) {
        for (int i = 0; i < state->parseResult.hunks.size(); ++i) {
            m_hunkSelector->addItem(QStringLiteral("冲突块 %1").arg(i + 1));
        }
    }
    m_hunkSelector->blockSignals(false);
    const bool hasHunk = validText && !state->parseResult.hunks.isEmpty();
    const bool deleted = state && isDeletionConflict(state->file.type)
        && (!state->file.hasOurs || !state->file.hasTheirs);
    const bool binary = state && state->binary;
    m_hunkSelector->setEnabled(!m_workflowBusy && hasHunk);
    m_acceptOursButton->setEnabled(!m_workflowBusy && (hasHunk || binary || deleted));
    m_acceptTheirsButton->setEnabled(!m_workflowBusy && (hasHunk || binary || deleted));
    m_acceptBothButton->setEnabled(!m_workflowBusy && hasHunk && !binary && !deleted);
    m_deleteButton->setVisible(deleted);
    m_deleteButton->setEnabled(!m_workflowBusy && deleted);
    updateWorkflowControls();
}

void ConflictResolverWidget::setWorkflowBusy(bool busy) {
    m_workflowBusy = busy;
    if (busy) {
        m_status->setText(QStringLiteral("正在写入并验证 Git index…"));
    }
    updateHunkControls(currentState());
    updateWorkflowControls();
}

ConflictResolverWidget::WorkflowPresentationState
ConflictResolverWidget::workflowPresentationState() const {
    if (m_completedOperation != ConflictOperationType::Unknown) {
        return WorkflowPresentationState::Completed;
    }
    const bool noConflicts = !m_snapshot.hasConflicts || m_snapshot.conflictCount == 0;
    if (noConflicts && m_snapshot.operationType != ConflictOperationType::Unknown) {
        return WorkflowPresentationState::ReadyToContinue;
    }
    return WorkflowPresentationState::Resolving;
}

void ConflictResolverWidget::updateWorkflowControls() {
    if (!m_applyCurrentButton || !m_applyAllButton || !m_continueButton || !m_abortButton) {
        return;
    }
    const bool sessionCurrent = m_resolutionService && m_service
        && m_resolutionService->workingDirectory() == m_snapshot.repositoryPath
        && m_resolutionService->repositoryGeneration() == m_serviceGeneration;
    const FileState *state = currentState();
    const bool currentReady = state && state->plan.ready && state->plan.unresolvedHunks == 0;
    bool anyReady = false;
    for (const ConflictFile &file : m_snapshot.files) {
        if (const FileState *candidate = stateForPath(file.path)) {
            anyReady = anyReady || (candidate->plan.ready
                                    && candidate->plan.unresolvedHunks == 0);
        }
    }
    const bool operationKnown = m_snapshot.operationType != ConflictOperationType::Unknown;
    const bool noConflicts = !m_snapshot.hasConflicts || m_snapshot.conflictCount == 0;
    const WorkflowPresentationState presentation = workflowPresentationState();
    m_applyCurrentButton->setEnabled(sessionCurrent && !m_workflowBusy && currentReady);
    m_applyAllButton->setEnabled(sessionCurrent && !m_workflowBusy && anyReady);

    if (presentation == WorkflowPresentationState::Completed) {
        const QString operation = conflictOperationTypeName(m_completedOperation);
        m_operationBanner->setText(QStringLiteral("%1 已完成").arg(operation));
        m_operationHint->setText(QStringLiteral("当前 Git 操作已成功完成。"));
        m_emptyState->setText(QStringLiteral("✓ %1 已完成").arg(operation));
        m_continueButton->setText(QStringLiteral("%1 已完成").arg(operation));
        m_continueButton->setEnabled(false);
        m_abortButton->setText(QStringLiteral("当前无操作可中止"));
        m_abortButton->setEnabled(false);
        m_workflowStatus->setText(QStringLiteral("%1 已完成。").arg(operation));
        return;
    }

    if (presentation == WorkflowPresentationState::ReadyToContinue) {
        m_operationBanner->setText(QStringLiteral("所有冲突已解决"));
        m_operationHint->setText(
            QStringLiteral("所有冲突文件均已处理，可以继续当前 Git 操作。"));
        m_emptyState->setText(QStringLiteral("✓ 所有冲突已解决"));
    } else if (noConflicts) {
        m_operationBanner->setText(QStringLiteral("当前没有冲突"));
        m_operationHint->setText(QStringLiteral("当前仓库没有未解决冲突。"));
        m_emptyState->setText(QStringLiteral("✓ 当前仓库没有未解决冲突"));
    } else {
        m_operationBanner->setText(operationTitle(m_snapshot.operationType));
        m_operationHint->setText(operationHint(m_snapshot.operationType));
        m_emptyState->setText(QStringLiteral("✓ 当前仓库没有未解决冲突"));
    }

    m_continueButton->setText(operationKnown
        ? QStringLiteral("继续 %1").arg(conflictOperationTypeName(m_snapshot.operationType))
        : QStringLiteral("当前无可继续操作"));
    m_continueButton->setEnabled(sessionCurrent && !m_workflowBusy && operationKnown && noConflicts);
    m_abortButton->setText(operationKnown
        ? QStringLiteral("中止 %1").arg(conflictOperationTypeName(m_snapshot.operationType))
        : QStringLiteral("当前无可中止操作"));
    m_abortButton->setEnabled(sessionCurrent && !m_workflowBusy && operationKnown);
    if (!sessionCurrent) {
        m_workflowStatus->setText(QStringLiteral("此 Resolver 属于旧仓库会话，不能写入。"));
    } else if (presentation == WorkflowPresentationState::ReadyToContinue) {
        m_workflowStatus->setText(
            QStringLiteral("所有冲突已解决并暂存；请在确认后继续或中止当前 Git 操作。"));
    } else if (noConflicts) {
        m_workflowStatus->setText(QStringLiteral("当前仓库没有未解决冲突。"));
    } else {
        m_workflowStatus->setText(QStringLiteral("剩余冲突：%1").arg(m_snapshot.conflictCount));
    }
}

void ConflictResolverWidget::applyCurrentPlan() {
    if (!m_resolutionService || m_workflowBusy) {
        return;
    }
    const FileState *state = currentState();
    if (!state || !state->plan.ready) {
        return;
    }
    setWorkflowBusy(true);
    QPointer<ConflictResolverWidget> guard(this);
    m_resolutionService->applyPlanAsync(state->plan,
        [guard](ConflictApplyResult result) {
            if (!guard) return;
            guard->setWorkflowBusy(false);
            if (!result.success()) {
                const QString prefix = result.workingTreeUpdated
                        && result.error == ConflictApplyError::StageFailed
                    ? QStringLiteral("文件已写回，但暂存失败")
                    : result.workingTreeUpdated
                        && result.error == ConflictApplyError::StageVerificationFailed
                    ? QStringLiteral("文件已写回，但暂存验证失败")
                    : QStringLiteral("应用未完成");
                guard->m_workflowStatus->setText(QStringLiteral("%1：%2")
                    .arg(prefix, result.message));
                return;
            }
            ConflictSnapshot refreshed = result.snapshot;
            if (!refreshed.hasConflicts) {
                // A conflict-free scan intentionally avoids operation probing;
                // retain the operation captured at resolver open so the user
                // can make the explicit Continue/Abort decision.
                refreshed.operationType = guard->m_snapshot.operationType;
            }
            guard->setSnapshot(refreshed);
            guard->m_workflowStatus->setText(QStringLiteral("已解决并暂存：%1")
                .arg(result.resolvedPath));
        });
}

void ConflictResolverWidget::applyAllReadyPlans() {
    if (!m_resolutionService || m_workflowBusy) {
        return;
    }
    setWorkflowBusy(true);
    QPointer<ConflictResolverWidget> guard(this);
    m_resolutionService->applyAllReadyAsync(resolutionPlans(),
        [guard](ConflictApplyResult result) {
            if (!guard) return;
            guard->setWorkflowBusy(false);
            if (!result.success()) {
                const QString prefix = result.error == ConflictApplyError::PartialSuccess
                    ? QStringLiteral("部分成功") : QStringLiteral("应用未完成");
                guard->m_workflowStatus->setText(QStringLiteral("%1：%2；失败文件：%3")
                    .arg(prefix, result.message, result.failedPath));
                return;
            }
            ConflictSnapshot refreshed = result.snapshot;
            if (!refreshed.hasConflicts) {
                refreshed.operationType = guard->m_snapshot.operationType;
            }
            guard->setSnapshot(refreshed);
            guard->m_workflowStatus->setText(QStringLiteral("已解决并暂存 %1 个文件。")
                .arg(result.successFiles.size()));
        });
}

void ConflictResolverWidget::continueOperation() {
    if (!m_resolutionService || m_workflowBusy || m_snapshot.hasConflicts
        || m_snapshot.operationType == ConflictOperationType::Unknown) {
        return;
    }
    setWorkflowBusy(true);
    QPointer<ConflictResolverWidget> guard(this);
    m_resolutionService->continueOperationAsync(m_snapshot.operationType,
        [guard](ConflictApplyResult result) {
            if (!guard) return;
            guard->setWorkflowBusy(false);
            if (!result.success()) {
                guard->m_workflowStatus->setText(
                    QStringLiteral("继续失败：%1").arg(result.message));
                return;
            }
            const ConflictOperationType continuedOperation = guard->m_snapshot.operationType;
            if (result.continuedButMoreConflicts) {
                guard->setSnapshot(result.snapshot);
                guard->m_workflowStatus->setText(
                    QStringLiteral("Git 已继续，出现下一批冲突。"));
            } else {
                guard->setCompletedSnapshot(result.snapshot, continuedOperation);
                guard->m_workflowStatus->setText(QStringLiteral("Git 操作已继续完成。"));
            }
        });
}

void ConflictResolverWidget::abortOperation() {
    if (!m_resolutionService || m_workflowBusy
        || m_snapshot.operationType == ConflictOperationType::Unknown) {
        return;
    }
    const QString operation = conflictOperationTypeName(m_snapshot.operationType);
    if (QMessageBox::warning(this, QStringLiteral("确认中止"),
                             QStringLiteral("中止 %1 将撤销当前 Git 操作产生的状态，可能改变工作区。是否继续？")
                                 .arg(operation),
                             QMessageBox::Yes | QMessageBox::No,
                             QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    setWorkflowBusy(true);
    QPointer<ConflictResolverWidget> guard(this);
    m_resolutionService->abortOperationAsync(m_snapshot.operationType,
        [guard](ConflictApplyResult result) {
            if (!guard) return;
            guard->setWorkflowBusy(false);
            if (!result.success()) {
                guard->m_workflowStatus->setText(
                    QStringLiteral("中止失败：%1").arg(result.message));
                return;
            }
            guard->setSnapshot(result.snapshot);
            guard->m_workflowStatus->setText(QStringLiteral("Git 操作已中止。"));
        });
}

void ConflictResolverWidget::setSourceText(QPlainTextEdit *editor,
                                           const QByteArray &content,
                                           bool present,
                                           const QString &missingText) {
    editor->setPlainText(present ? TextEncoding::decode(content).text : missingText);
}

bool ConflictResolverWidget::isResolverSessionCurrent() const {
    return m_service && m_snapshot.repositoryPath == m_service->workingDirectory()
        && m_serviceGeneration == m_service->repositoryGeneration();
}

ConflictResolverWidget::FileState *ConflictResolverWidget::currentState() {
    auto it = m_states.find(m_currentPath);
    return it == m_states.end() ? nullptr : &it.value();
}

const ConflictResolverWidget::FileState *ConflictResolverWidget::stateForPath(
    const QString &path) const {
    auto it = m_states.constFind(path);
    return it == m_states.cend() ? nullptr : &it.value();
}
