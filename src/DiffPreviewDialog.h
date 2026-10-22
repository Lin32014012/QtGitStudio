#pragma once
#include <QDialog>

class QPlainTextEdit;
class QLineEdit;
class QLabel;

// v4-P6-17: IDE-style side-by-side diff viewer - old version (left,
// red) / new version (right, green), parsed from the unified diff
// text. Keeps zoom (Ctrl +/-/0), find, copy and wrap.
class DiffPreviewDialog : public QDialog {
    Q_OBJECT
public:
    explicit DiffPreviewDialog(const QString &diffText,
                               const QString &commitHash,
                               const QString &fileName,
                               QWidget *parent = nullptr);

    // test hooks
    QPlainTextEdit *leftEdit() const { return m_leftEdit; }
    QPlainTextEdit *rightEdit() const { return m_rightEdit; }
    int currentFontSize() const { return m_fontSize; }
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void findNext();
    void toggleWrap(bool on);
    void copyAll();

private:
    void applyFontSize();
    QPlainTextEdit *m_leftEdit = nullptr;
    QPlainTextEdit *m_rightEdit = nullptr;
    QLineEdit *m_findEdit = nullptr;
    int m_fontSize = 12;
};
