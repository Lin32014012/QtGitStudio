#pragma once
#include <QString>
#include <QVector>

enum class FileStatus { Modified, Added, Deleted, Renamed, Conflict, Untracked };

struct FileEntry {
    FileStatus status;
    QString path;
    QString oldPath;
    char xy[2] = {0, 0};
};

struct DiffEntry {
    enum class Type { Add, Delete, Rename, Modify };
    Type type;
    QString oldPath;
    QString newPath;
    int additions = 0;
    int deletions = 0;
};

struct BranchEntry {
    QString name;
    QString upstream;
    int ahead = 0;
    int behind = 0;
    bool isCurrent = false;
    bool isDetached = false;
};

struct GitSnapshot {
    QVector<FileEntry> files;
    QVector<DiffEntry> diffs;
    QVector<BranchEntry> branches;
    QString head;
    QString headRef;
};
