#include "EncodingDiagnostic.h"

namespace {

constexpr int ProbeBytes = 8000;   // git's binary heuristic window

bool hasNullByte(const QByteArray &data) {
    const int probe = qMin(data.size(), ProbeBytes);
    for (int i = 0; i < probe; ++i) {
        if (data.at(i) == '\0') {
            return true;
        }
    }
    return false;
}

} // namespace

EncodingDiagnostic diagnoseEncoding(const QByteArray &data) {
    EncodingDiagnostic diag;
    if (data.size() < 2) {
        return diag;
    }
    const unsigned char b0 = static_cast<unsigned char>(data.at(0));
    const unsigned char b1 = static_cast<unsigned char>(data.at(1));

    if (b0 == 0xFF && b1 == 0xFE) {
        diag.kind = EncodingDiagnostic::Kind::Utf16LeBom;
        diag.encodingName = QStringLiteral("UTF-16 LE");
        diag.hasNullBytes = hasNullByte(data);
        return diag;
    }
    if (b0 == 0xFE && b1 == 0xFF) {
        diag.kind = EncodingDiagnostic::Kind::Utf16BeBom;
        diag.encodingName = QStringLiteral("UTF-16 BE");
        diag.hasNullBytes = hasNullByte(data);
        return diag;
    }
    if (data.size() >= 3) {
        const unsigned char b2 = static_cast<unsigned char>(data.at(2));
        if (b0 == 0xEF && b1 == 0xBB && b2 == 0xBF) {
            diag.kind = EncodingDiagnostic::Kind::Utf8Bom;
            diag.encodingName = QStringLiteral("UTF-8 (BOM)");
            diag.hasNullBytes = hasNullByte(data);
            return diag;
        }
    }
    if (hasNullByte(data)) {
        diag.kind = EncodingDiagnostic::Kind::NullBytes;
        diag.hasNullBytes = true;
    }
    return diag;
}
