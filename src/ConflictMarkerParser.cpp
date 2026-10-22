#include "ConflictMarkerParser.h"

namespace {

struct PhysicalLine {
    int start = 0;
    int end = 0;
    QString text;
};

QVector<PhysicalLine> linesFor(const QString &content) {
    QVector<PhysicalLine> lines;
    int start = 0;
    while (start < content.size()) {
        const int newline = content.indexOf(QLatin1Char('\n'), start);
        const int end = newline < 0 ? content.size() : newline + 1;
        int textEnd = newline < 0 ? content.size() : newline;
        if (textEnd > start && content.at(textEnd - 1) == QLatin1Char('\r')) {
            --textEnd;
        }
        lines.append({start, end, content.mid(start, textEnd - start)});
        start = end;
    }
    return lines;
}

enum class ParserState { Normal, Ours, Base, Theirs };

bool startsMarker(const QString &line, const char *marker) {
    return line.startsWith(QLatin1String(marker));
}

QString malformed(const QString &detail) {
    return QStringLiteral("Invalid conflict marker structure: %1").arg(detail);
}

} // namespace

ConflictMarkerParseResult ConflictMarkerParser::parse(const QString &content) {
    ConflictMarkerParseResult result;
    ParserState state = ParserState::Normal;
    ConflictMarkerHunk hunk;
    int oursStart = 0;
    int baseStart = 0;
    int theirsStart = 0;

    for (const PhysicalLine &line : linesFor(content)) {
        const bool open = startsMarker(line.text, "<<<<<<<");
        const bool base = startsMarker(line.text, "|||||||");
        const bool divider = startsMarker(line.text, "=======");
        const bool close = startsMarker(line.text, ">>>>>>>");

        if (state == ParserState::Normal) {
            if (open) {
                state = ParserState::Ours;
                hunk = {};
                hunk.startOffset = line.start;
                oursStart = line.end;
            } else if (base || divider || close) {
                result.error = malformed(QStringLiteral("marker without opening marker"));
                return result;
            }
            continue;
        }

        if (open) {
            result.error = malformed(QStringLiteral("nested opening marker"));
            return result;
        }

        if (state == ParserState::Ours) {
            if (base) {
                hunk.ours = content.mid(oursStart, line.start - oursStart);
                hunk.hasBase = true;
                baseStart = line.end;
                state = ParserState::Base;
            } else if (divider) {
                hunk.ours = content.mid(oursStart, line.start - oursStart);
                theirsStart = line.end;
                state = ParserState::Theirs;
            } else if (close) {
                result.error = malformed(QStringLiteral("missing divider marker"));
                return result;
            }
            continue;
        }

        if (state == ParserState::Base) {
            if (divider) {
                hunk.base = content.mid(baseStart, line.start - baseStart);
                theirsStart = line.end;
                state = ParserState::Theirs;
            } else if (base || close) {
                result.error = malformed(QStringLiteral("invalid diff3 marker order"));
                return result;
            }
            continue;
        }

        if (state == ParserState::Theirs) {
            if (close) {
                hunk.theirs = content.mid(theirsStart, line.start - theirsStart);
                hunk.endOffset = line.end;
                result.hunks.append(hunk);
                state = ParserState::Normal;
            } else if (base || divider) {
                result.error = malformed(QStringLiteral("duplicate separator marker"));
                return result;
            }
        }
    }

    if (state != ParserState::Normal) {
        result.error = malformed(QStringLiteral("unterminated conflict block"));
    }
    return result;
}
