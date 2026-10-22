#include "GitStatusParser.h"
#include <QByteArrayView>
#include <QStringList>

GitSnapshot GitStatusParser::parse(const QByteArray &output) {
    GitSnapshot snap;
    QByteArray data = output;
    while (data.endsWith('\n')) data.chop(1);
    QStringList lines = QString::fromUtf8(data).split('\n');
    for (const QString &line : lines) {
        if (line.isEmpty()) continue;
        QStringList parts = line.split(' ');
        if (parts.size() < 2) continue;
        // Porcelain v2 format:
        // Tracked files: "1 XY sub ... path"
        // Untracked files: "? path"
        if (parts[0] == "?") {
            // Untracked file: "? path"
            FileEntry entry;
            entry.status = FileStatus::Untracked;
            entry.path = parts.mid(1).join(' ');
            if (!entry.path.isEmpty()) {
                snap.files.append(entry);
            }
            continue;
        }
        if (parts[0] == "!") continue; // ignored files
        QString xy = parts[1]; // XY field is second
        if (xy.startsWith('#')) continue; // header
        FileEntry entry;
        entry.xy[0] = xy[0].toLatin1();
        entry.xy[1] = xy.size() > 1 ? xy[1].toLatin1() : ' ';
        // Determine status from XY
        char x = entry.xy[0];
        char y = entry.xy[1];
        if (x == 'R' || x == 'C') {
            entry.status = (x == 'R') ? FileStatus::Renamed : FileStatus::Modified;
        } else if (x == 'A' || y == 'A') {
            entry.status = FileStatus::Added;
        } else if (x == 'D' || y == 'D') {
            entry.status = FileStatus::Deleted;
        } else if (x == 'U' || y == 'U') {
            entry.status = FileStatus::Conflict;
        } else if (x == '?' && y == '?') {
            entry.status = FileStatus::Untracked;
        } else {
            entry.status = FileStatus::Modified;
        }
        // Path is the last field
        entry.path = parts.last();
        if (!entry.path.isEmpty()) {
            snap.files.append(entry);
        }
    }
    return snap;
}
