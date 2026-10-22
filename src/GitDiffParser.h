#pragma once
#include "GitSnapshot.h"
#include <QByteArray>

class GitDiffParser {
public:
    static GitSnapshot parse(const QByteArray &output);
};
