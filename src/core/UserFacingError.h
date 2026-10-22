// v4-P6-1 UI/UX Polish: UserFacingError translates raw Git stderr into
// a user-friendly title + reason + suggestion. Pure static mapping so
// it is fully unit-testable.
#ifndef USERFACINGERROR_H
#define USERFACINGERROR_H

#include <QString>

struct UserFacingError {
    QString title;
    QString reason;
    QString suggestion;

    // Map combined git output (stderr + stdout) to a user-facing error
    // for the given operation name (e.g. "Fetch", "合并", "推送").
    static UserFacingError fromGit(const QString &gitOutput,
                                   const QString &operationName);

    // "title\n\nreason\n\n建议：suggestion"
    QString toString() const;
};

#endif // USERFACINGERROR_H
