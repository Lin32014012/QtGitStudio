#pragma once

#include "GitError.h"

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QVector>

// Index conflict states.  The names intentionally describe Git's stage
// semantics rather than a future resolution action.
enum class ConflictType {
    BothModified,
    AddedByUs,
    AddedByThem,
    DeletedByUs,
    DeletedByThem,
    BothAdded,
    BothDeleted,
    RenameDelete,
    RenameRename,
    Unknown,
};

enum class ConflictOperationType {
    Merge,
    Rebase,
    CherryPick,
    Revert,
    Unknown,
};

struct ConflictFile {
    QString path;
    ConflictType type = ConflictType::Unknown;

    bool hasBase = false;
    bool hasOurs = false;
    bool hasTheirs = false;
    QString baseObjectId;
    QString oursObjectId;
    QString theirsObjectId;
    QString baseMode;
    QString oursMode;
    QString theirsMode;

    // Metadata scans intentionally do not load blobs.  This remains false
    // until a ConflictFileContent preview is requested and inspected.
    bool isBinary = false;
    QString error;
};

struct ConflictFileContent {
    QString path;
    QByteArray baseContent;
    QByteArray oursContent;
    QByteArray theirsContent;
    bool baseTruncated = false;
    bool oursTruncated = false;
    bool theirsTruncated = false;
    bool contentTruncated = false;
    bool isBinary = false;
};

struct ConflictSnapshot {
    QString repositoryPath;
    bool hasConflicts = false;
    QVector<ConflictFile> files;
    int conflictCount = 0;
    ConflictOperationType operationType = ConflictOperationType::Unknown;
};

struct ConflictScanResult {
    ConflictSnapshot snapshot;
    GitError error;
    // A stale result deliberately carries no conflict data for a new session.
    bool stale = false;
};

struct ConflictFileContentResult {
    ConflictFileContent content;
    GitError error;
    bool stale = false;
};

QString conflictTypeName(ConflictType type);
QString conflictOperationTypeName(ConflictOperationType type);

Q_DECLARE_METATYPE(ConflictType)
Q_DECLARE_METATYPE(ConflictOperationType)
