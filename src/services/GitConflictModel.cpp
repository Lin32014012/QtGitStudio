#include "GitConflictModel.h"

QString conflictTypeName(ConflictType type) {
    switch (type) {
    case ConflictType::BothModified: return QStringLiteral("BothModified");
    case ConflictType::AddedByUs: return QStringLiteral("AddedByUs");
    case ConflictType::AddedByThem: return QStringLiteral("AddedByThem");
    case ConflictType::DeletedByUs: return QStringLiteral("DeletedByUs");
    case ConflictType::DeletedByThem: return QStringLiteral("DeletedByThem");
    case ConflictType::BothAdded: return QStringLiteral("BothAdded");
    case ConflictType::BothDeleted: return QStringLiteral("BothDeleted");
    case ConflictType::RenameDelete: return QStringLiteral("RenameDelete");
    case ConflictType::RenameRename: return QStringLiteral("RenameRename");
    case ConflictType::Unknown: return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}

QString conflictOperationTypeName(ConflictOperationType type) {
    switch (type) {
    case ConflictOperationType::Merge: return QStringLiteral("Merge");
    case ConflictOperationType::Rebase: return QStringLiteral("Rebase");
    case ConflictOperationType::CherryPick: return QStringLiteral("CherryPick");
    case ConflictOperationType::Revert: return QStringLiteral("Revert");
    case ConflictOperationType::Unknown: return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}
