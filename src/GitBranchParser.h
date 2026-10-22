#pragma once
#include "GitSnapshot.h"
#include <QByteArray>

class GitBranchParser {
public:
    static GitSnapshot parse(const QByteArray &output);
};
