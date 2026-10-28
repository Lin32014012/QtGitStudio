#pragma once
#include <QDateTime>
#include <QString>
#include <QVector>

// One verdict, as it was shown to the student.
struct PracticeLogEntry {
    QDateTime timestamp;
    QString practiceId;
    QString title;
    bool passed = false;
    QString detail;
};

// Append-only record of practice verdicts, plus a Markdown rendering for
// export. Stored as JSON Lines so recording is an append and never a
// read-modify-write of the whole file.
class PracticeLog {
public:
    static void record(const QString &practiceId, bool passed, const QString &detail);
    static QVector<PracticeLogEntry> entries();
    // Human-readable report: per-chapter summary followed by the verdicts.
    static QString toMarkdown();
    static bool exportTo(const QString &filePath, QString *error);
    // Test/QA helper: removes the log file.
    static void clear();
    static QString logPath();
};
