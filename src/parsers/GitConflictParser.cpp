#include "GitConflictParser.h"

#include <algorithm>

namespace {

int pathStartForUnmergedStatusRecord(const QByteArray &record) {
    int spaces = 0;
    for (int index = 0; index < record.size(); ++index) {
        if (record.at(index) != ' ') {
            continue;
        }
        ++spaces;
        if (spaces == 9) {
            return index + 1;
        }
    }
    return -1;
}

void setStage(ConflictFile *file, int stage, const QString &mode,
              const QString &objectId) {
    switch (stage) {
    case 1:
        file->hasBase = true;
        file->baseMode = mode;
        file->baseObjectId = objectId;
        break;
    case 2:
        file->hasOurs = true;
        file->oursMode = mode;
        file->oursObjectId = objectId;
        break;
    case 3:
        file->hasTheirs = true;
        file->theirsMode = mode;
        file->theirsObjectId = objectId;
        break;
    default:
        break;
    }
}

ConflictType fallbackType(const ConflictFile &file) {
    if (file.hasBase && file.hasOurs && file.hasTheirs) {
        return ConflictType::BothModified;
    }
    if (!file.hasBase && file.hasOurs && file.hasTheirs) {
        return ConflictType::BothAdded;
    }
    if (file.hasBase && !file.hasOurs && file.hasTheirs) {
        return ConflictType::DeletedByUs;
    }
    if (file.hasBase && file.hasOurs && !file.hasTheirs) {
        return ConflictType::DeletedByThem;
    }
    if (file.hasBase && !file.hasOurs && !file.hasTheirs) {
        return ConflictType::BothDeleted;
    }
    return ConflictType::Unknown;
}

} // namespace

QHash<QString, ConflictType> GitConflictParser::parsePorcelainV2Unmerged(
    const QByteArray &output) {
    QHash<QString, ConflictType> typesByPath;
    for (const QByteArray &record : output.split('\0')) {
        if (!record.startsWith("u ")) {
            continue;
        }
        const int pathStart = pathStartForUnmergedStatusRecord(record);
        if (pathStart < 0 || pathStart >= record.size()) {
            continue;
        }
        const QList<QByteArray> fields = record.left(pathStart - 1).split(' ');
        if (fields.size() < 2) {
            continue;
        }
        const QString path = QString::fromUtf8(record.mid(pathStart));
        if (!path.isEmpty()) {
            typesByPath.insert(path, conflictTypeForPorcelainCode(fields.at(1)));
        }
    }
    return typesByPath;
}

QVector<ConflictFile> GitConflictParser::parseUnmergedIndex(
    const QByteArray &output, const QHash<QString, ConflictType> &typesByPath) {
    QVector<ConflictFile> files;
    QHash<QString, int> indexByPath;
    for (const QByteArray &record : output.split('\0')) {
        const int tab = record.indexOf('\t');
        if (tab < 0 || tab + 1 >= record.size()) {
            continue;
        }
        const QList<QByteArray> fields = record.left(tab).split(' ');
        if (fields.size() != 3) {
            continue;
        }
        bool validStage = false;
        const int stage = fields.at(2).toInt(&validStage);
        const QString path = QString::fromUtf8(record.mid(tab + 1));
        if (!validStage || path.isEmpty() || stage < 1 || stage > 3) {
            continue;
        }
        int index = indexByPath.value(path, -1);
        if (index < 0) {
            ConflictFile file;
            file.path = path;
            file.type = typesByPath.value(path, ConflictType::Unknown);
            files.append(file);
            index = files.size() - 1;
            indexByPath.insert(path, index);
        }
        setStage(&files[index], stage, QString::fromLatin1(fields.at(0)),
                 QString::fromLatin1(fields.at(1)));
    }
    for (ConflictFile &file : files) {
        if (file.type == ConflictType::Unknown) {
            file.type = fallbackType(file);
        }
    }
    std::sort(files.begin(), files.end(), [](const ConflictFile &left,
                                             const ConflictFile &right) {
        return left.path < right.path;
    });
    return files;
}

ConflictType GitConflictParser::conflictTypeForPorcelainCode(const QByteArray &code) {
    if (code == "UU") return ConflictType::BothModified;
    if (code == "AA") return ConflictType::BothAdded;
    if (code == "DD") return ConflictType::BothDeleted;
    if (code == "AU") return ConflictType::AddedByUs;
    if (code == "UA") return ConflictType::AddedByThem;
    if (code == "DU") return ConflictType::DeletedByUs;
    if (code == "UD") return ConflictType::DeletedByThem;
    return ConflictType::Unknown;
}
