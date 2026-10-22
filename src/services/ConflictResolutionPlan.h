#pragma once

#include "GitConflictModel.h"

#include <QString>

// A prepared resolution is intentionally an in-memory instruction only.
// P6-23C will be the first stage allowed to persist or stage a plan.
enum class ResolutionAction {
    None,
    Manual,
    Ours,
    Theirs,
    Both,
    Delete,
    Keep,
};

struct ConflictResolutionPlan {
    QString repositoryPath;
    QString filePath;
    ConflictType conflictType = ConflictType::Unknown;
    ResolutionAction action = ResolutionAction::None;
    QString resultContent;
    QString sourceObjectId;
    // Immutable index facts captured with the read-only snapshot.  P6-23C
    // compares them to a fresh scan before writing, so an old editor can
    // never overwrite a conflict changed by another Git client.
    QString expectedBaseObjectId;
    QString expectedOursObjectId;
    QString expectedTheirsObjectId;
    QString expectedWorkingTreeSha256;
    bool expectedWorkingTreeExists = false;
    bool binary = false;
    bool deleteFile = false;
    bool ready = false;
    int unresolvedHunks = 0;
    quint64 sessionGeneration = 0;
};

QString resolutionActionName(ResolutionAction action);

Q_DECLARE_METATYPE(ResolutionAction)
Q_DECLARE_METATYPE(ConflictResolutionPlan)
