#pragma once
#include "GitSnapshot.h"
#include <QByteArray>

class GitStatusParser {
public:
    static GitSnapshot parse(const QByteArray &output);
};
