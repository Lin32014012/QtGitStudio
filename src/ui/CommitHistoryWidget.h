#pragma once
#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QLabel;
class QListWidget;
class QSplitter;
class QPushButton;
class DiffViewerWidget;

// v4-P6-15: list-style commit history (replaces the graph as the main
// presentation). Reuses GitCommitGraphService (list data) and
// GitCommitDiffService (files + diff) - no new data pipeline.
class CommitHistoryWidget : public QWidget {
    Q_OBJECT
public:
    explicit CommitHistoryWidget(QWidget *parent = nullptr);

    void setRepositoryPath(const QString &path);
    QString repositoryPath() const { return m_repositoryPath; }
    void refresh();               // reload the commit list

    // test hooks
    int rowCount() const;
    QTreeWidget *commitTree() const { return m_commitTree; }
    QLineEdit *searchEdit() const { return m_searchEdit; }
    QString selectedCommitHash() const;
    QString currentDiffText() const { return m_currentDiffText; }

public slots:
    void selectCommitByHash(const QString &hash);

private:
    void loadCommitList();
    void onCommitSelected();
    void onFileClicked(int row);
    void openFullDiff();

    QString m_repositoryPath;
    QTreeWidget *m_commitTree;
    QLineEdit *m_searchEdit;
    QLabel *m_detailLabel;
    QListWidget *m_fileList;
    DiffViewerWidget *m_diffViewer;
    QPushButton *m_enlargeButton;
    QString m_currentHash;
    QString m_currentDiffText;
    QString m_currentFileName;
};
