// v4-P4-4 GitHub Release management widget: list / create / delete
// releases for an owner/repo. The token comes from the EXISTING
// GitHubAccountService session (never hardcoded); the tag picker lists
// local tags via GitTagService when available.
#ifndef RELEASEWIDGET_H
#define RELEASEWIDGET_H

#include <QWidget>
#include "GitHubReleaseService.h"
#include "GitHubReleaseAssetService.h"
#include "ReleaseNotesService.h"

class QTreeWidget;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QPushButton;
class QProgressBar;
class QPlainTextEdit;
class GitHubAccountService;
class GitTagService;

class ReleaseWidget : public QWidget {
    Q_OBJECT
public:
    explicit ReleaseWidget(GitHubAccountService *accountService,
                           GitTagService *tagService,
                           QWidget *parent = nullptr);

    // Reload the release list for the entered owner/repo.
    void reload();

private slots:
    void onLoadReleases();
    void onCreateRelease();
    void onDeleteRelease();
    void onOpenWebPage();
    void onSelectionChanged();
    void onUploadAsset();
    void onDeleteAsset();
    void onGenerateNotes();
    void onCopyNotes();
    void onUseNotesInRelease();

private:
    void refreshList(const QList<GitHubRelease> &releases);
    void loadAssetsForSelection();
    void refreshAssetList(const QList<GitHubReleaseAsset> &assets);
    void populateTagCombos();
    QString token() const;
    GitHubRelease selectedRelease() const;
    GitHubReleaseAsset selectedAsset() const;

    GitHubAccountService *m_accountService;
    GitTagService *m_tagService;
    GitHubReleaseService *m_releaseService;      // async completions must
    GitHubReleaseAssetService *m_assetService;   // outlive the caller
    ReleaseNotesService *m_notesService;
    QTreeWidget *m_releaseTree;
    QTreeWidget *m_assetTree;
    QLineEdit *m_ownerEdit;
    QLineEdit *m_repoEdit;
    QComboBox *m_tagCombo;
    QLineEdit *m_nameEdit;
    QLineEdit *m_bodyEdit;
    QCheckBox *m_draftCheck;
    QCheckBox *m_prereleaseCheck;
    QPushButton *m_loadButton;
    QPushButton *m_createButton;
    QPushButton *m_deleteButton;
    QPushButton *m_openButton;
    QPushButton *m_uploadButton;
    QPushButton *m_deleteAssetButton;
    QProgressBar *m_progressBar;
    QComboBox *m_oldTagCombo;
    QComboBox *m_newTagCombo;
    QPushButton *m_generateButton;
    QPlainTextEdit *m_previewEdit;
    QPushButton *m_copyButton;
    QPushButton *m_useButton;
    QString m_generatedMarkdown;
};

#endif // RELEASEWIDGET_H
