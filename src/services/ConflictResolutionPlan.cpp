#include "ConflictResolutionPlan.h"

QString resolutionActionName(ResolutionAction action) {
    switch (action) {
    case ResolutionAction::None: return QStringLiteral("None");
    case ResolutionAction::Manual: return QStringLiteral("Manual");
    case ResolutionAction::Ours: return QStringLiteral("Ours");
    case ResolutionAction::Theirs: return QStringLiteral("Theirs");
    case ResolutionAction::Both: return QStringLiteral("Both");
    case ResolutionAction::Delete: return QStringLiteral("Delete");
    case ResolutionAction::Keep: return QStringLiteral("Keep");
    }
    return QStringLiteral("None");
}
