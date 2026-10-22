#include "TextEncoding.h"

#include <QStringDecoder>
#include <QStringEncoder>
#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace TextEncoding {

namespace {

constexpr char kGb18030Name[] = "GB18030";
#ifdef Q_OS_WIN
constexpr UINT kGb18030CodePage = 54936;
#endif

bool qtGb18030Available() {
    QStringDecoder decoder(kGb18030Name);
    return decoder.isValid();
}

bool decodeGb18030(const QByteArray &bytes, QString *out) {
    if (bytes.isEmpty()) {
        out->clear();
        return true;
    }
    QStringDecoder decoder(kGb18030Name, QStringConverter::Flag::Stateless);
    if (decoder.isValid()) {
        const QString text = decoder.decode(bytes);
        if (decoder.hasError()) {
            return false;
        }
        *out = text;
        return true;
    }
#ifdef Q_OS_WIN
    const int len = MultiByteToWideChar(kGb18030CodePage, MB_ERR_INVALID_CHARS,
                                        bytes.constData(), int(bytes.size()),
                                        nullptr, 0);
    if (len <= 0) {
        return false;
    }
    QString text(len, Qt::Uninitialized);
    MultiByteToWideChar(kGb18030CodePage, MB_ERR_INVALID_CHARS,
                        bytes.constData(), int(bytes.size()),
                        reinterpret_cast<wchar_t *>(text.data()), len);
    *out = text;
    return true;
#else
    return false;
#endif
}

bool encodeGb18030(const QString &text, QByteArray *out) {
    if (text.isEmpty()) {
        out->clear();
        return true;
    }
    QStringEncoder encoder(kGb18030Name, QStringConverter::Flag::Stateless);
    if (encoder.isValid()) {
        const QByteArray bytes = encoder.encode(text);
        if (encoder.hasError()) {
            return false;
        }
        *out = bytes;
        return true;
    }
#ifdef Q_OS_WIN
    const auto *wide = reinterpret_cast<const wchar_t *>(text.utf16());
    const int len = WideCharToMultiByte(kGb18030CodePage, 0, wide,
                                        int(text.size()), nullptr, 0,
                                        nullptr, nullptr);
    if (len <= 0) {
        return false;
    }
    QByteArray bytes(len, Qt::Uninitialized);
    WideCharToMultiByte(kGb18030CodePage, 0, wide, int(text.size()),
                        bytes.data(), len, nullptr, nullptr);
    *out = bytes;
    return true;
#else
    return false;
#endif
}

} // namespace

QString name(Kind kind) {
    switch (kind) {
    case Kind::Utf8: return QStringLiteral("UTF-8");
    case Kind::Utf8Bom: return QStringLiteral("UTF-8 (BOM)");
    case Kind::Utf16Le: return QStringLiteral("UTF-16 LE");
    case Kind::Utf16Be: return QStringLiteral("UTF-16 BE");
    case Kind::Gb18030: return QStringLiteral("GB18030");
    case Kind::Unknown: break;
    }
    return QStringLiteral("未知编码");
}

bool isValidUtf8(const QByteArray &bytes) {
    QStringDecoder decoder(QStringConverter::Utf8,
                           QStringConverter::Flag::Stateless);
    const QString decoded = decoder.decode(bytes);  // EncodedData is lazy
    Q_UNUSED(decoded);
    return !decoder.hasError();
}

bool isGb18030Available() {
#ifdef Q_OS_WIN
    return qtGb18030Available() || IsValidCodePage(kGb18030CodePage);
#else
    return qtGb18030Available();
#endif
}

QString gb18030Backend() {
    if (qtGb18030Available()) {
        return QStringLiteral("QStringConverter");
    }
#ifdef Q_OS_WIN
    if (IsValidCodePage(kGb18030CodePage)) {
        return QStringLiteral("Win32 CP54936");
    }
#endif
    return QStringLiteral("none");
}

Decoded decode(const QByteArray &bytes) {
    Decoded result;
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        const QByteArray body = bytes.mid(3);
        result.kind = Kind::Utf8Bom;
        result.ok = isValidUtf8(body);
        result.text = QString::fromUtf8(body);
        if (!result.ok) {
            result.kind = Kind::Unknown;
        }
        return result;
    }
    if (bytes.startsWith("\xFF\xFE") || bytes.startsWith("\xFE\xFF")) {
        const bool le = bytes.startsWith("\xFF\xFE");
        QStringDecoder decoder(le ? QStringConverter::Utf16LE
                                  : QStringConverter::Utf16BE,
                               QStringConverter::Flag::Stateless);
        result.text = decoder.decode(bytes.mid(2));
        result.ok = !decoder.hasError();
        result.kind = result.ok ? (le ? Kind::Utf16Le : Kind::Utf16Be)
                                : Kind::Unknown;
        return result;
    }
    if (isValidUtf8(bytes)) {
        result.kind = Kind::Utf8;
        result.text = QString::fromUtf8(bytes);
        result.ok = true;
        return result;
    }
    QString gbText;
    if (decodeGb18030(bytes, &gbText)) {
        QByteArray roundTrip;
        if (encodeGb18030(gbText, &roundTrip) && roundTrip == bytes) {
            result.kind = Kind::Gb18030;
            result.text = gbText;
            result.ok = true;
            return result;
        }
    }
    result.kind = Kind::Unknown;
    result.text = QString::fromUtf8(bytes);  // lossy, display only
    result.ok = false;
    return result;
}

QByteArray encode(const QString &text, Kind kind, bool *ok) {
    bool success = false;
    QByteArray bytes;
    switch (kind) {
    case Kind::Utf8:
        bytes = text.toUtf8();
        success = true;
        break;
    case Kind::Utf8Bom:
        bytes = QByteArray("\xEF\xBB\xBF") + text.toUtf8();
        success = true;
        break;
    case Kind::Utf16Le:
    case Kind::Utf16Be: {
        const bool le = kind == Kind::Utf16Le;
        QStringEncoder encoder(le ? QStringConverter::Utf16LE
                                  : QStringConverter::Utf16BE,
                               QStringConverter::Flag::Stateless);
        bytes = QByteArray(le ? "\xFF\xFE" : "\xFE\xFF") + encoder.encode(text);
        success = !encoder.hasError();
        break;
    }
    case Kind::Gb18030:
        success = encodeGb18030(text, &bytes);
        break;
    case Kind::Unknown:
        break;
    }
    if (ok) {
        *ok = success;
    }
    return success ? bytes : QByteArray();
}

QString decodeMixedLines(const QByteArray &bytes) {
    if (isValidUtf8(bytes)) {
        return QString::fromUtf8(bytes);  // fast path: whole output is UTF-8
    }
    QString out;
    out.reserve(bytes.size());
    qsizetype start = 0;
    while (start <= bytes.size()) {
        qsizetype end = bytes.indexOf('\n', start);
        const bool last = end < 0;
        if (last) {
            end = bytes.size();
        }
        const QByteArray line = bytes.mid(start, end - start);
        QString decoded;
        if (isValidUtf8(line)) {
            decoded = QString::fromUtf8(line);
        } else if (!decodeGb18030(line, &decoded)) {
            decoded = QString::fromUtf8(line);
        }
        out += decoded;
        if (last) {
            break;
        }
        out += QLatin1Char('\n');
        start = end + 1;
    }
    return out;
}

} // namespace TextEncoding
