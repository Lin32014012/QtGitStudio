#pragma once

#include <QByteArray>
#include <QString>

// Text encoding detection / lossless round-trip for working-tree files.
//
// Detection order (first match wins):
//   1. BOM:  EF BB BF -> UTF-8 (BOM), FF FE -> UTF-16 LE, FE FF -> UTF-16 BE
//   2. Strictly valid UTF-8 (ASCII included)          -> UTF-8
//   3. Decodes as GB18030 without errors AND re-encodes to the exact same
//      bytes (GBK/GB2312 are subsets of GB18030)      -> GB18030
//   4. Otherwise                                      -> Unknown
//
// GB18030 is provided by QStringConverter when Qt is built with ICU (the
// usual Linux / macOS packages). On Windows builds without ICU the Win32
// code page 54936 is used instead. When neither is available the file is
// reported as Unknown and callers must refuse to write it back.
namespace TextEncoding {

enum class Kind {
    Unknown,
    Utf8,
    Utf8Bom,
    Utf16Le,
    Utf16Be,
    Gb18030,
};

struct Decoded {
    Kind kind = Kind::Unknown;
    QString text;        // decoded text (BOM stripped)
    bool ok = false;     // false -> text is a lossy UTF-8 fallback
};

// Human-readable name, e.g. "UTF-8", "UTF-8 (BOM)", "GB18030".
QString name(Kind kind);

// True when bytes form strictly valid UTF-8 (no replacement needed).
bool isValidUtf8(const QByteArray &bytes);

// True when a GB18030 codec is available in this build/runtime.
bool isGb18030Available();

// Detect the encoding of bytes and decode them.
Decoded decode(const QByteArray &bytes);

// Encode text in the given encoding (BOM re-added for Utf8Bom / UTF-16).
// *ok is false for Unknown or when the codec is unavailable / lossy.
QByteArray encode(const QString &text, Kind kind, bool *ok);

// Decode mixed-encoding tool output (e.g. `git diff`) line by line: each
// line that is valid UTF-8 is decoded as UTF-8, otherwise as GB18030 when
// that decodes cleanly, otherwise as lossy UTF-8. Keeps "\n" separators.
QString decodeMixedLines(const QByteArray &bytes);

} // namespace TextEncoding
