#pragma once
#include <QString>
#include <QStringList>
#include <QVector>

// A practice scenario for learning Git inside the app: a sandboxed
// repository the student may break freely, plus a checkable goal.
struct PracticeDefinition {
    QString id;
    QString title;
    QString goal;     // one sentence the student reads
    QString level;    // 入门 / 进阶
    QString hint;     // a single nudge, not a walkthrough
    QString chapter;  // 基础 / 冲突 - groups practices into chapters
};

struct PracticeCheck {
    bool passed = false;
    QString detail;  // why it is not done yet, phrased as what is missing
};

// Generates practice sandboxes and verifies the student's result.
//
// Scope note: this never touches a user's real repository. `generate`
// creates a fresh directory under the parent the caller supplies, and
// `check` only reads history/index state through git plumbing.
class PracticeService {
public:
    static QVector<PracticeDefinition> catalog();
    static PracticeDefinition definition(const QString &id);

    // Creates <parentDir>/<id> as a ready-to-work sandbox. On success
    // *repoPath receives the absolute path. On failure returns false and
    // fills *error.
    static bool generate(const QString &id,
                         const QString &parentDir,
                         QString *repoPath,
                         QString *error);

    // Verifies whether the sandbox now satisfies the practice goal.
    static PracticeCheck check(const QString &id, const QString &repoPath);
};
