#include "GitBranchParser.h"
#include <QByteArrayView>
#include <QStringList>

GitSnapshot GitBranchParser::parse(const QByteArray &output) {
    GitSnapshot snap;
    QByteArray data = output;
    while (data.endsWith('\n')) data.chop(1);
    QStringList lines = QString::fromUtf8(data).split('\n');
    for (const QString &line : lines) {
        if (line.isEmpty()) continue;
        QStringList parts = line.split('\t');
        if (parts.size() < 5) continue;
        BranchEntry entry;
        entry.name = parts[0];
        // parts[1] = objectname
        // parts[2] = refname
        entry.upstream = parts[3];
        QString track = parts[4];
        entry.isCurrent = (parts.size() > 5 && parts[5] == "*");
        // Parse ahead/behind from track
        if (track.contains("ahead")) {
            QStringList trackParts = track.split(",");
            for (const auto &p : trackParts) {
                QString trimmed = p.trimmed();
                if (trimmed.startsWith("ahead")) {
                    entry.ahead = trimmed.section(" ", 1).toInt();
                } else if (trimmed.startsWith("behind")) {
                    entry.behind = trimmed.section(" ", 1).toInt();
                }
            }
        }
        snap.branches.append(entry);
    }
    return snap;
}
