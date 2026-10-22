#pragma once

#include "GitConflictModel.h"

#include <QByteArray>
#include <QHash>
#include <QVector>

// Parser for NUL-delimited Git porcelain v2 and unmerged-index records.
// It never interprets a path by splitting on lines or whitespace.
class GitConflictParser {
public:
    static QHash<QString, ConflictType> parsePorcelainV2Unmerged(
        const QByteArray &output);
    static QVector<ConflictFile> parseUnmergedIndex(
        const QByteArray &output,
        const QHash<QString, ConflictType> &typesByPath);
    static ConflictType conflictTypeForPorcelainCode(const QByteArray &code);
};
