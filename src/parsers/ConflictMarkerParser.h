#pragma once

#include <QString>
#include <QVector>

struct ConflictMarkerHunk {
    int startOffset = 0;
    int endOffset = 0;
    QString ours;
    QString base;
    QString theirs;
    bool hasBase = false;
};

struct ConflictMarkerParseResult {
    QVector<ConflictMarkerHunk> hunks;
    QString error;

    bool isValid() const { return error.isEmpty(); }
    bool hasConflicts() const { return !hunks.isEmpty(); }
};

// Stateful parser for working-tree conflict markers. Markers are recognized
// only at the beginning of a physical line; text that merely contains a marker
// sequence is ordinary content.
class ConflictMarkerParser {
public:
    static ConflictMarkerParseResult parse(const QString &content);
};
