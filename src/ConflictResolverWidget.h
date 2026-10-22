#pragma once

#include "ConflictMarkerParser.h"
#include "ConflictResolutionPlan.h"
#include "GitConflictModel.h"

#include <QHash>
#include <QWidget>

class QComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class GitConflictService;
class GitConflictResolutionService;

// Accept actions update an in-memory ConflictResolutionPlan. Repository writes,
// staging, and operation control are delegated to GitConflictResolutionService
// only after the user explicitly chooses Apply, Continue, or Abort.
class ConflictResolverWidget : public QWidget {
    Q_OBJECT
public:
    explicit ConflictResolverWidget(GitConflictService *service,
                                   QWidget *parent = nullptr);

    void setSnapshot(const ConflictSnapshot &snapshot);
    ConflictSnapshot snapshot() const { return m_snapshot; }
    ConflictResolutionPlan resolutionPlanForFile(const QString &path) const;
    QList<ConflictResolutionPlan> resolutionPlans() const;
    void setResolutionService(GitConflictResolutionService *service);

    // Narrow hooks keep the test focused on real widget interaction while
    // avoiding dependence on layout position or translated visible strings.
    QListWidget *fileListForTesting() const { return m_fileList; }
    QPlainTextEdit *resultEditorForTesting() const { return m_resultEditor; }
    QComboBox *hunkSelectorForTesting() const { return m_hunkSelector; }
    QString currentPathForTesting() const { return m_currentPath; }
    int unresolvedHunksForTesting(const QString &path) const;
    bool isFileReadyForTesting(const QString &path) const;
    bool isEmptyStateForTesting() const;
    bool workflowBusyForTesting() const { return m_workflowBusy; }

signals:
    void resolutionPlanChanged(const ConflictResolutionPlan &plan);
    void conflictSnapshotChanged(const ConflictSnapshot &snapshot);

private slots:
    void onFileSelectionChanged();
    void onResultChanged();
    void acceptOurs();
    void acceptTheirs();
    void acceptBoth();
    void planDelete();
    void applyCurrentPlan();
    void applyAllReadyPlans();
    void continueOperation();
    void abortOperation();

private:
    enum class WorkflowPresentationState {
        Resolving,
        ReadyToContinue,
        Completed,
    };

    struct FileState {
        ConflictFile file;
        ConflictFileContent content;
        QString resultContent;
        ConflictMarkerParseResult parseResult;
        ConflictResolutionPlan plan;
        bool loaded = false;
        bool loading = false;
        bool unavailable = false;
        bool binary = false;
        bool dirty = false;
        int initialHunkCount = 0;
    };

    void buildUi();
    void applySnapshot(const ConflictSnapshot &snapshot);
    void setCompletedSnapshot(const ConflictSnapshot &snapshot,
                              ConflictOperationType operation);
    WorkflowPresentationState workflowPresentationState() const;
    void selectFile(const QString &path);
    void loadCurrentFile();
    void showFileState();
    void setResultText(const QString &text);
    void updateParseAndPlan(FileState *state, ResolutionAction action);
    void updateFileItem(const QString &path);
    void updateHunkControls(FileState *state);
    void applyCurrentHunk(ResolutionAction action);
    void prepareSidePlan(ResolutionAction action);
    void setSourceText(QPlainTextEdit *editor, const QByteArray &content,
                       bool present, const QString &missingText);
    void setWorkflowBusy(bool busy);
    void updateWorkflowControls();
    bool isResolverSessionCurrent() const;
    FileState *currentState();
    const FileState *stateForPath(const QString &path) const;

    GitConflictService *m_service = nullptr;
    GitConflictResolutionService *m_resolutionService = nullptr;
    ConflictSnapshot m_snapshot;
    quint64 m_serviceGeneration = 0;
    quint64 m_loadRequest = 0;
    QString m_currentPath;
    QHash<QString, FileState> m_states;
    bool m_settingResult = false;
    bool m_workflowBusy = false;
    ConflictOperationType m_completedOperation = ConflictOperationType::Unknown;

    QLabel *m_operationBanner = nullptr;
    QLabel *m_operationHint = nullptr;
    QLabel *m_emptyState = nullptr;
    QLabel *m_status = nullptr;
    QListWidget *m_fileList = nullptr;
    QSplitter *m_resolverContent = nullptr;
    QPlainTextEdit *m_oursEditor = nullptr;
    QPlainTextEdit *m_baseEditor = nullptr;
    QPlainTextEdit *m_theirsEditor = nullptr;
    QPlainTextEdit *m_resultEditor = nullptr;
    QComboBox *m_hunkSelector = nullptr;
    QPushButton *m_acceptOursButton = nullptr;
    QPushButton *m_acceptTheirsButton = nullptr;
    QPushButton *m_acceptBothButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QPushButton *m_applyCurrentButton = nullptr;
    QPushButton *m_applyAllButton = nullptr;
    QPushButton *m_continueButton = nullptr;
    QPushButton *m_abortButton = nullptr;
    QLabel *m_workflowStatus = nullptr;
};
