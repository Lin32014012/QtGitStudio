// v4-P0-R1.3: minimal diff viewer. Shows the raw git diff text; when git
// reports a binary diff, shows the EncodingDiagnostic explanation instead
// (read-only - the file itself is never touched).
#ifndef DIFFVIEWERWIDGET_H
#define DIFFVIEWERWIDGET_H

#include <QWidget>

class QLabel;
class QTextEdit;

struct EncodingDiagnostic;

class DiffViewerWidget : public QWidget {
    Q_OBJECT
public:
    explicit DiffViewerWidget(QWidget *parent = nullptr);

    void showTextDiff(const QString &text);
    void showBinaryDiagnosis(const QString &fileName, const EncodingDiagnostic &diag);
    void showMessage(const QString &message);
    void clearView();

    QString currentText() const;

private:
    QLabel *m_statusLabel;
    QTextEdit *m_textEdit;
};

#endif // DIFFVIEWERWIDGET_H
