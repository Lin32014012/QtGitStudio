#include "ReleaseWidget.h"
#include "Theme.h"
#include "GitHubAccountService.h"
#include "GitTagService.h"
#include "ActionButtonFeedback.h"
#include "NotificationManager.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QFileDialog>
#include <QClipboard>
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>

ReleaseWidget::ReleaseWidget(GitHubAccountService *accountService,
                             GitTagService *tagService, QWidget *parent)
    : QWidget(parent)
    , m_accountService(accountService)
    , m_tagService(tagService)
    , m_releaseService(new GitHubReleaseService(this))
    , m_assetService(new GitHubReleaseAssetService(this))
    , m_notesService(new ReleaseNotesService(this))
    , m_releaseTree(new QTreeWidget(this))
    , m_assetTree(new QTreeWidget(this))
    , m_ownerEdit(new QLineEdit(this))
    , m_repoEdit(new QLineEdit(this))
    , m_tagCombo(new QComboBox(this))
    , m_nameEdit(new QLineEdit(this))
    , m_bodyEdit(new QLineEdit(this))
    , m_draftCheck(new QCheckBox(QStringLiteral("草稿 (Draft)"), this))
    , m_prereleaseCheck(new QCheckBox(QStringLiteral("预发布 (Prerelease)"), this))
    , m_loadButton(new QPushButton(QStringLiteral("加载 Release"), this))
    , m_createButton(new QPushButton(QStringLiteral("创建 Release"), this))
    , m_deleteButton(new QPushButton(QStringLiteral("删除 Release"), this))
    , m_openButton(new QPushButton(QStringLiteral("打开网页"), this))
    , m_uploadButton(new QPushButton(QStringLiteral("上传 Asset"), this))
    , m_deleteAssetButton(new QPushButton(QStringLiteral("删除 Asset"), this))
    , m_progressBar(new QProgressBar(this))
    , m_oldTagCombo(new QComboBox(this))
    , m_newTagCombo(new QComboBox(this))
    , m_generateButton(new QPushButton(QStringLiteral("生成 Release Notes"), this))
    , m_previewEdit(new QPlainTextEdit(this))
    , m_copyButton(new QPushButton(QStringLiteral("复制 Markdown"), this))
    , m_useButton(new QPushButton(QStringLiteral("填入 Release 描述"), this)) {
    // Repository selection.
    auto *repoForm = new QGroupBox(QStringLiteral("仓库"), this);
    auto *repoLayout = new QFormLayout(repoForm);
    m_ownerEdit->setPlaceholderText(QStringLiteral("如 octocat"));
    m_repoEdit->setPlaceholderText(QStringLiteral("如 Hello-World"));
    repoLayout->addRow(QStringLiteral("Owner"), m_ownerEdit);
    repoLayout->addRow(QStringLiteral("Repo"), m_repoEdit);
    auto *loadRow = new QHBoxLayout();
    loadRow->addWidget(m_loadButton);
    loadRow->addStretch();
    repoLayout->addRow(loadRow);

    // Create form.
    auto *createForm = new QGroupBox(QStringLiteral("创建 Release"), this);
    auto *createLayout = new QFormLayout(createForm);
    m_tagCombo->setEditable(true);
    m_tagCombo->setInsertPolicy(QComboBox::NoInsert);
    m_nameEdit->setPlaceholderText(QStringLiteral("留空 = 标签名"));
    m_bodyEdit->setPlaceholderText(QStringLiteral("Release 描述"));
    createLayout->addRow(QStringLiteral("标签"), m_tagCombo);
    createLayout->addRow(QStringLiteral("名称"), m_nameEdit);
    createLayout->addRow(QStringLiteral("描述"), m_bodyEdit);
    auto *flagRow = new QHBoxLayout();
    flagRow->addWidget(m_draftCheck);
    flagRow->addWidget(m_prereleaseCheck);
    flagRow->addStretch();
    createLayout->addRow(flagRow);
    auto *createRow = new QHBoxLayout();
    createRow->addWidget(m_createButton);
    createRow->addStretch();
    createLayout->addRow(createRow);

    // Release list.
    m_releaseTree->setColumnCount(4);
    m_releaseTree->setHeaderLabels(
        {QStringLiteral("标签"), QStringLiteral("名称"),
         QStringLiteral("类型"), QStringLiteral("网页地址")});
    m_releaseTree->setRootIsDecorated(false);
    m_releaseTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_releaseTree->setSelectionBehavior(QAbstractItemView::SelectRows);

    auto *listLabel = new QLabel(QStringLiteral("Release 列表"), this);
    listLabel->setProperty("role", QStringLiteral("cardTitle"));


    auto *buttonRow = new QHBoxLayout();
    buttonRow->addWidget(m_openButton);
    buttonRow->addWidget(m_deleteButton);
    buttonRow->addStretch();

    // v4-P5-1: assets of the selected release.
    m_assetTree->setColumnCount(4);
    m_assetTree->setHeaderLabels(
        {QStringLiteral("文件名"), QStringLiteral("大小"),
         QStringLiteral("类型"), QStringLiteral("下载地址")});
    m_assetTree->setRootIsDecorated(false);
    m_assetTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_assetTree->setSelectionBehavior(QAbstractItemView::SelectRows);

    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(false);

    auto *assetRow = new QHBoxLayout();
    assetRow->addWidget(m_uploadButton);
    assetRow->addWidget(m_deleteAssetButton);
    assetRow->addWidget(m_progressBar, 1);
    assetRow->addStretch();

    // v4-P5-2: release notes generator (old tag .. new tag).
    auto *notesForm = new QGroupBox(QStringLiteral("Release Notes 生成器"), this);
    auto *notesLayout = new QFormLayout(notesForm);
    m_oldTagCombo->setEditable(true);
    m_oldTagCombo->setInsertPolicy(QComboBox::NoInsert);
    m_newTagCombo->setEditable(true);
    m_newTagCombo->setInsertPolicy(QComboBox::NoInsert);
    notesLayout->addRow(QStringLiteral("旧 Tag"), m_oldTagCombo);
    notesLayout->addRow(QStringLiteral("新 Tag"), m_newTagCombo);
    auto *notesRow = new QHBoxLayout();
    notesRow->addWidget(m_generateButton);
    notesRow->addWidget(m_copyButton);
    notesRow->addWidget(m_useButton);
    notesRow->addStretch();
    notesLayout->addRow(notesRow);
    m_previewEdit->setReadOnly(true);
    m_previewEdit->setPlaceholderText(
        QStringLiteral("生成的 Markdown 预览（git log <旧Tag>..<新Tag>）"));
    m_previewEdit->setMinimumHeight(160);
    notesLayout->addRow(m_previewEdit);

    // v4-P6-4: group the asset area under "Release Assets" so the
    // dialog reads as 仓库信息 / 创建 Release / Release Notes /
    // Release Assets (visual grouping only - no API logic change).
    auto *assetGroup = new QGroupBox(QStringLiteral("Release Assets"), this);
    auto *assetGroupLayout = new QVBoxLayout(assetGroup);
    assetGroupLayout->setContentsMargins(8, 6, 8, 8);
    assetGroupLayout->setSpacing(6);
    assetGroupLayout->addWidget(m_assetTree, 1);
    assetGroupLayout->addLayout(assetRow);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(repoForm);
    layout->addWidget(createForm);
    layout->addWidget(notesForm);
    layout->addWidget(listLabel);
    layout->addWidget(m_releaseTree, 1);
    layout->addLayout(buttonRow);
    layout->addWidget(assetGroup, 1);

    ActionButtonFeedback::configure(
        m_createButton,
        {"#16A34A", "#15803D", "#166534"},
        {QStringLiteral("正在创建…"), QStringLiteral("创建成功"),
         QStringLiteral("创建失败")});
    ActionButtonFeedback::configure(
        m_deleteButton,
        {"#DC2626", "#B91C1C", "#991B1B"},
        {QStringLiteral("正在删除…"), QStringLiteral("删除成功"),
         QStringLiteral("删除失败")});
    ActionButtonFeedback::configure(
        m_uploadButton,
        {"#2563EB", "#1D4ED8", "#1E40AF"},
        {QStringLiteral("正在上传…"), QStringLiteral("上传成功"),
         QStringLiteral("上传失败")});
    ActionButtonFeedback::configure(
        m_deleteAssetButton,
        {"#DC2626", "#B91C1C", "#991B1B"},
        {QStringLiteral("正在删除…"), QStringLiteral("删除成功"),
         QStringLiteral("删除失败")});

    connect(m_loadButton, &QPushButton::clicked,
            this, &ReleaseWidget::onLoadReleases);
    connect(m_createButton, &QPushButton::clicked,
            this, &ReleaseWidget::onCreateRelease);
    connect(m_deleteButton, &QPushButton::clicked,
            this, &ReleaseWidget::onDeleteRelease);
    connect(m_openButton, &QPushButton::clicked,
            this, &ReleaseWidget::onOpenWebPage);
    connect(m_releaseTree, &QTreeWidget::itemSelectionChanged,
            this, &ReleaseWidget::onSelectionChanged);
    connect(m_uploadButton, &QPushButton::clicked,
            this, &ReleaseWidget::onUploadAsset);
    connect(m_deleteAssetButton, &QPushButton::clicked,
            this, &ReleaseWidget::onDeleteAsset);
    connect(m_assetTree, &QTreeWidget::itemSelectionChanged,
            this, &ReleaseWidget::onSelectionChanged);
    connect(m_generateButton, &QPushButton::clicked,
            this, &ReleaseWidget::onGenerateNotes);
    connect(m_copyButton, &QPushButton::clicked,
            this, &ReleaseWidget::onCopyNotes);
    connect(m_useButton, &QPushButton::clicked,
            this, &ReleaseWidget::onUseNotesInRelease);

    populateTagCombos();

    onSelectionChanged();
    reload();
}

void ReleaseWidget::populateTagCombos() {
    // Local tags as create/range choices (best effort; never required).
    m_tagCombo->clear();
    m_oldTagCombo->clear();
    m_newTagCombo->clear();
    if (!m_tagService) return;
    const GitTagService::ListResult tags = m_tagService->listTags();
    if (!tags.success || tags.tags.isEmpty()) return;
    QStringList names;
    for (const GitTag &tag : tags.tags) {
        names.append(tag.name);
    }
    std::sort(names.begin(), names.end());
    for (const QString &name : names) {
        m_tagCombo->addItem(name);
        m_oldTagCombo->addItem(name);
        m_newTagCombo->addItem(name);
    }
    // New tag defaults to the newest tag, old tag to the previous one.
    if (names.size() >= 2) {
        m_oldTagCombo->setCurrentIndex(names.size() - 2);
        m_newTagCombo->setCurrentIndex(names.size() - 1);
    }
}

QString ReleaseWidget::token() const {
    return m_accountService ? m_accountService->token() : QString();
}

void ReleaseWidget::reload() {
    // Reload the list for the current owner/repo when already loaded.
    if (!m_ownerEdit->text().trimmed().isEmpty()
        && !m_repoEdit->text().trimmed().isEmpty()
        && m_releaseTree->topLevelItemCount() == 0) {
        onLoadReleases();
    }
}

void ReleaseWidget::onLoadReleases() {
    const QString owner = m_ownerEdit->text().trimmed();
    const QString repo = m_repoEdit->text().trimmed();
    if (owner.isEmpty() || repo.isEmpty()) {
        QMessageBox::information(
            this, QStringLiteral("缺少仓库信息"),
            QStringLiteral("请填写 Owner 和 Repo。"));
        return;
    }
    if (token().trimmed().isEmpty()) {
        QMessageBox::warning(
            this, QStringLiteral("未登录"),
            QStringLiteral("请先在“GitHub 账号”中登录并保存访问令牌。"));
        return;
    }
    GitHubReleaseService *service = m_releaseService;
    service->fetchReleases(
        token(), owner, repo,
        [this](const GitHubReleaseService::ListResult &result) {
            if (!result.success) {
                QMessageBox::warning(this, QStringLiteral("加载失败"),
                                     result.error);
                return;
            }
            refreshList(result.releases);
        });
}

void ReleaseWidget::refreshList(const QList<GitHubRelease> &releases) {
    m_releaseTree->clear();
    if (releases.isEmpty()) {
        // v4-P6-1: empty state with reason + next step.
        auto *emptyItem = new QTreeWidgetItem(m_releaseTree);
        emptyItem->setText(
            0, NotificationManager::emptyStateText(
                   QStringLiteral("该仓库还没有 Release"),
                   QStringLiteral("先创建 Tag，再使用上方“创建 Release”。")));
        emptyItem->setFlags(Qt::NoItemFlags);
        m_releaseTree->setRootIsDecorated(false);
        return;
    }
    for (const GitHubRelease &release : releases) {
        auto *item = new QTreeWidgetItem(m_releaseTree);
        item->setText(0, release.tagName);
        item->setText(1, release.name);
        item->setText(2, release.draft
                             ? QStringLiteral("草稿")
                             : (release.prerelease
                                    ? QStringLiteral("预发布")
                                    : QStringLiteral("正式")));
        item->setText(3, release.htmlUrl);
        item->setData(0, Qt::UserRole, QVariant::fromValue(release.id));
        item->setData(1, Qt::UserRole, release.htmlUrl);
    }
    for (int i = 0; i < 4; ++i) {
        m_releaseTree->resizeColumnToContents(i);
    }
}

GitHubRelease ReleaseWidget::selectedRelease() const {
    GitHubRelease release;
    QTreeWidgetItem *item = m_releaseTree->currentItem();
    if (!item) return release;
    release.id = item->data(0, Qt::UserRole).toLongLong();
    release.htmlUrl = item->data(1, Qt::UserRole).toString();
    release.tagName = item->text(0);
    return release;
}

void ReleaseWidget::onSelectionChanged() {
    const bool hasSelection =
        m_releaseTree->currentItem() != nullptr;
    m_deleteButton->setEnabled(hasSelection);
    m_openButton->setEnabled(hasSelection);
    m_uploadButton->setEnabled(hasSelection);
    m_assetTree->setEnabled(hasSelection);
    const bool hasAssetSelection =
        m_assetTree->currentItem() != nullptr;
    m_deleteAssetButton->setEnabled(hasAssetSelection);
    loadAssetsForSelection();
}

void ReleaseWidget::loadAssetsForSelection() {
    const GitHubRelease release = selectedRelease();
    const QString owner = m_ownerEdit->text().trimmed();
    const QString repo = m_repoEdit->text().trimmed();
    const QString tk = token().trimmed();
    m_assetTree->clear();
    if (release.id <= 0 || owner.isEmpty() || repo.isEmpty() || tk.isEmpty()) {
        return;
    }
    GitHubReleaseAssetService *service = m_assetService;
    service->fetchAssets(
        tk, owner, repo, release.id,
        [this](const GitHubReleaseAssetService::ListResult &result) {
            if (result.success) {
                refreshAssetList(result.assets);
            }
        });
}

void ReleaseWidget::refreshAssetList(
    const QList<GitHubReleaseAsset> &assets) {
    m_assetTree->clear();
    for (const GitHubReleaseAsset &asset : assets) {
        auto *item = new QTreeWidgetItem(m_assetTree);
        item->setText(0, asset.name);
        item->setText(1, QStringLiteral("%1 KB")
                             .arg(asset.size / 1024.0, 0, 'f', 1));
        item->setText(2, asset.contentType);
        item->setText(3, asset.browserDownloadUrl);
        item->setData(0, Qt::UserRole, QVariant::fromValue(asset.id));
        item->setData(1, Qt::UserRole, asset.browserDownloadUrl);
    }
    for (int i = 0; i < 4; ++i) {
        m_assetTree->resizeColumnToContents(i);
    }
}

GitHubReleaseAsset ReleaseWidget::selectedAsset() const {
    GitHubReleaseAsset asset;
    QTreeWidgetItem *item = m_assetTree->currentItem();
    if (!item) return asset;
    asset.id = item->data(0, Qt::UserRole).toLongLong();
    asset.name = item->text(0);
    asset.browserDownloadUrl = item->data(1, Qt::UserRole).toString();
    return asset;
}

void ReleaseWidget::onUploadAsset() {
    const GitHubRelease release = selectedRelease();
    const QString owner = m_ownerEdit->text().trimmed();
    const QString repo = m_repoEdit->text().trimmed();
    const QString tk = token().trimmed();
    if (release.id <= 0) {
        QMessageBox::information(
            this, QStringLiteral("未选择 Release"),
            QStringLiteral("请先在列表中选择要上传资产的 Release。"));
        return;
    }
    if (tk.isEmpty()) {
        QMessageBox::warning(
            this, QStringLiteral("未登录"),
            QStringLiteral("请先在“GitHub 账号”中登录并保存访问令牌。"));
        return;
    }
    const QString filePath = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择要上传的文件"));
    if (filePath.isEmpty()) return;
    ActionButtonFeedback::showBusy(m_uploadButton, QStringLiteral("正在上传…"));
    m_progressBar->setValue(0);
    m_progressBar->setVisible(true);
    GitHubReleaseAssetService *service = m_assetService;
    service->uploadAsset(
        tk, owner, repo, release.id, filePath,
        [this](qint64 sent, qint64 total) {
            if (total > 0) {
                m_progressBar->setValue(
                    static_cast<int>(sent * 100 / total));
            }
        },
        [this](const GitHubReleaseAssetService::UploadResult &result) {
            m_progressBar->setVisible(false);
            if (result.success) {
                ActionButtonFeedback::showSuccess(
                    m_uploadButton, QStringLiteral("上传成功"));
                loadAssetsForSelection();
            } else {
                ActionButtonFeedback::showError(
                    m_uploadButton, QStringLiteral("上传失败"));
                QMessageBox::warning(this, QStringLiteral("上传失败"),
                                     result.error);
            }
        });
}

void ReleaseWidget::onDeleteAsset() {
    const GitHubReleaseAsset asset = selectedAsset();
    const QString owner = m_ownerEdit->text().trimmed();
    const QString repo = m_repoEdit->text().trimmed();
    const QString tk = token().trimmed();
    if (asset.id <= 0) {
        QMessageBox::information(
            this, QStringLiteral("未选择 Asset"),
            QStringLiteral("请先在列表中选择要删除的 Asset。"));
        return;
    }
    if (tk.isEmpty()) {
        QMessageBox::warning(
            this, QStringLiteral("未登录"),
            QStringLiteral("请先在“GitHub 账号”中登录并保存访问令牌。"));
        return;
    }
    const QMessageBox::StandardButton choice = QMessageBox::question(
        this, QStringLiteral("确认删除"),
        QStringLiteral("确定删除 Asset %1 吗？").arg(asset.name));
    if (choice != QMessageBox::Yes) return;
    ActionButtonFeedback::showBusy(m_deleteAssetButton,
                                   QStringLiteral("正在删除…"));
    GitHubReleaseAssetService *service = m_assetService;
    service->deleteAsset(
        tk, owner, repo, asset.id,
        [this](const GitHubReleaseAssetService::DeleteResult &result) {
            if (result.success) {
                ActionButtonFeedback::showSuccess(
                    m_deleteAssetButton, QStringLiteral("删除成功"));
                loadAssetsForSelection();
            } else {
                ActionButtonFeedback::showError(
                    m_deleteAssetButton, QStringLiteral("删除失败"));
                QMessageBox::warning(this, QStringLiteral("删除失败"),
                                     result.error);
            }
        });
}

void ReleaseWidget::onCreateRelease() {
    const QString owner = m_ownerEdit->text().trimmed();
    const QString repo = m_repoEdit->text().trimmed();
    const QString tag = m_tagCombo->currentText().trimmed();
    if (owner.isEmpty() || repo.isEmpty()) {
        QMessageBox::information(
            this, QStringLiteral("缺少仓库信息"),
            QStringLiteral("请填写 Owner 和 Repo。"));
        return;
    }
    if (tag.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("缺少标签"),
                             QStringLiteral("请选择或输入标签名称。"));
        return;
    }
    const QString tk = token().trimmed();
    if (tk.isEmpty()) {
        QMessageBox::warning(
            this, QStringLiteral("未登录"),
            QStringLiteral("请先在“GitHub 账号”中登录并保存访问令牌。"));
        return;
    }
    ActionButtonFeedback::showBusy(m_createButton, QStringLiteral("正在创建…"));
    GitHubReleaseService *service = m_releaseService;
    service->createRelease(
        tk, owner, repo, tag, m_nameEdit->text().trimmed(),
        m_bodyEdit->text().trimmed(), m_draftCheck->isChecked(),
        m_prereleaseCheck->isChecked(),
        [this](const GitHubReleaseService::CreateResult &result) {
            if (result.success) {
                ActionButtonFeedback::showSuccess(
                    m_createButton, QStringLiteral("创建成功"));
                m_bodyEdit->clear();
                onLoadReleases();
            } else {
                ActionButtonFeedback::showError(
                    m_createButton, QStringLiteral("创建失败"));
                QMessageBox::warning(this, QStringLiteral("创建失败"),
                                     result.error);
            }
        });
}

void ReleaseWidget::onDeleteRelease() {
    const GitHubRelease release = selectedRelease();
    if (release.id <= 0) {
        QMessageBox::information(
            this, QStringLiteral("未选择 Release"),
            QStringLiteral("请先在列表中选择要删除的 Release。"));
        return;
    }
    const QString owner = m_ownerEdit->text().trimmed();
    const QString repo = m_repoEdit->text().trimmed();
    const QString tk = token().trimmed();
    if (tk.isEmpty()) {
        QMessageBox::warning(
            this, QStringLiteral("未登录"),
            QStringLiteral("请先在“GitHub 账号”中登录并保存访问令牌。"));
        return;
    }
    const QMessageBox::StandardButton choice = QMessageBox::question(
        this, QStringLiteral("确认删除"),
        QStringLiteral("确定删除 Release %1（%2）吗？")
            .arg(release.tagName, release.htmlUrl));
    if (choice != QMessageBox::Yes) return;
    ActionButtonFeedback::showBusy(m_deleteButton, QStringLiteral("正在删除…"));
    GitHubReleaseService *service = m_releaseService;
    service->deleteRelease(
        tk, owner, repo, release.id,
        [this](const GitHubReleaseService::DeleteResult &result) {
            if (result.success) {
                ActionButtonFeedback::showSuccess(
                    m_deleteButton, QStringLiteral("删除成功"));
                onLoadReleases();
            } else {
                ActionButtonFeedback::showError(
                    m_deleteButton, QStringLiteral("删除失败"));
                QMessageBox::warning(this, QStringLiteral("删除失败"),
                                     result.error);
            }
        });
}

void ReleaseWidget::onOpenWebPage() {
    const GitHubRelease release = selectedRelease();
    if (release.htmlUrl.isEmpty()) return;
    QDesktopServices::openUrl(QUrl(release.htmlUrl));
}

void ReleaseWidget::onGenerateNotes() {
    // v4-P5-2: git log <old>..<new> -> categorized Markdown preview.
    const QString oldTag = m_oldTagCombo->currentText().trimmed();
    const QString newTag = m_newTagCombo->currentText().trimmed();
    if (oldTag.isEmpty() || newTag.isEmpty()) {
        QMessageBox::information(
            this, QStringLiteral("缺少 Tag"),
            QStringLiteral("请选择旧 Tag 和新 Tag。"));
        return;
    }
    ReleaseNotesService *service = m_notesService;
    service->generate(
        oldTag, newTag,
        [this](const ReleaseNotesResult &result) {
            if (!result.success) {
                QMessageBox::warning(
                    this, QStringLiteral("生成失败"),
                    result.errorMessage.isEmpty()
                        ? QStringLiteral("未知错误")
                        : result.errorMessage);
                return;
            }
            m_generatedMarkdown = result.markdown;
            m_previewEdit->setPlainText(result.markdown);
            m_copyButton->setEnabled(true);
            m_useButton->setEnabled(true);
        });
}

void ReleaseWidget::onCopyNotes() {
    // v4-P5-2: copy the generated Markdown to the clipboard.
    if (m_generatedMarkdown.isEmpty()) return;
    QApplication::clipboard()->setText(m_generatedMarkdown);
    ActionButtonFeedback::showSuccess(m_generateButton,
                                      QStringLiteral("已复制到剪贴板"));
}

void ReleaseWidget::onUseNotesInRelease() {
    // v4-P5-2: fill the create-release description with the notes.
    if (m_generatedMarkdown.isEmpty()) return;
    m_bodyEdit->setText(m_generatedMarkdown);
    ActionButtonFeedback::showSuccess(m_generateButton,
                                      QStringLiteral("已填入 Release 描述"));
}
