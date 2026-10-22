#pragma once
#include <QString>

enum class GitErrorType {
    None,
    ProcessFailed,
    CommandNotFound,
    Timeout,
    InvalidOutput
};

struct GitError {
    GitErrorType type = GitErrorType::None;
    QString message;
    int exitCode = -1;
    QString stderrOutput;

    bool hasError() const { return type != GitErrorType::None; }
};
