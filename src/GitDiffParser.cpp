#include "GitDiffParser.h"
#include <QByteArrayView>
#include <QStringList>

GitSnapshot GitDiffParser::parse(const QByteArray &output) {
    GitSnapshot snap;
    QByteArray data = output;
    while (data.endsWith('\n')) data.chop(1);
    QStringList lines = QString::fromUtf8(data).split('\n');
    for (const QString &line : lines) {
        if (line.isEmpty()) continue;
        QStringList parts = line.split('\t');
        if (parts.size() < 2) continue;
        DiffEntry entry;
        QString addStr = parts[0];
        QString delStr = parts[1];
        entry.additions = (addStr == "-") ? 0 : addStr.toInt();
        entry.deletions = (delStr == "-") ? 0 : delStr.toInt();
        // Parse paths - may be tab-separated or NUL-separated
        if (parts.size() >= 4) {
            // Tab-separated: add del old new
            entry.oldPath = parts[2];
            entry.newPath = parts[3];
            entry.type = DiffEntry::Type::Rename;
        } else if (parts.size() == 3) {
            // Could be NUL-separated in single field or tab-separated
            QStringList subParts = parts[2].split('\0');
            if (subParts.size() >= 2) {
                entry.oldPath = subParts[0];
                entry.newPath = subParts[1];
                entry.type = DiffEntry::Type::Rename;
            } else {
                // Single path - determine type
                if (entry.deletions > 0 && entry.additions == 0) {
                    entry.oldPath = subParts[0];
                    entry.type = DiffEntry::Type::Delete;
                } else {
                    entry.newPath = subParts[0];
                    entry.type = DiffEntry::Type::Add;
                }
            }
        }
        if (!entry.newPath.isEmpty() || !entry.oldPath.isEmpty()) {
            snap.diffs.append(entry);
        }
    }
    return snap;
}
