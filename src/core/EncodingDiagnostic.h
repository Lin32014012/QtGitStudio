// v4-P0-R1.3 Stability Fix: binary-file encoding diagnostic. Pure static
// analysis of file bytes - never modifies the file. Explains WHY git treats
// a file as binary (UTF-16 BOMs / heavy NUL bytes). UI hosts (diff viewer /
// teaching panel) do not exist in this baseline; this module provides the
// detection layer plus tests, and the diagnosis text is ready for any host.
#ifndef ENCODINGDIAGNOSTIC_H
#define ENCODINGDIAGNOSTIC_H

#include <QByteArray>
#include <QString>

struct EncodingDiagnostic {
    enum class Kind {
        None,        // no anomaly detected
        Utf16LeBom,  // FF FE
        Utf16BeBom,  // FE FF
        Utf8Bom,     // EF BB BF (informational; git still diffs UTF-8 BOM text)
        NullBytes,   // no BOM, but NUL bytes in the first 8000 bytes
    };

    Kind kind = Kind::None;
    QString encodingName;   // "UTF-16 LE" / "UTF-16 BE" / "UTF-8 (BOM)" / empty
    bool hasNullBytes = false;

    // True when this diagnosis explains a binary verdict (UTF-16 BOMs or
    // heavy NUL bytes). UTF-8 BOM alone does not make git treat a file as
    // binary, so it is reported but not a binary cause.
    bool isBinaryCause() const {
        return kind == Kind::Utf16LeBom || kind == Kind::Utf16BeBom
            || kind == Kind::NullBytes;
    }

    // Human-readable Chinese explanation ready for UI display.
    QString explanation() const {
        QString text = QStringLiteral("Git 认为该文件是二进制文件。");
        if (!encodingName.isEmpty()) {
            text += QStringLiteral("\n检测：%1 编码。").arg(encodingName);
        }
        text += QStringLiteral("\n原因：文件包含大量空字节。");
        text += QStringLiteral("\n建议：转换为 UTF-8。");
        return text;
    }
};

// Analyze the first 8000 bytes (git's binary heuristic window):
// - FF FE        -> UTF-16 LE BOM
// - FE FF        -> UTF-16 BE BOM
// - EF BB BF     -> UTF-8 BOM
// - otherwise    -> NUL-byte scan (>=1 NUL byte -> NullBytes)
EncodingDiagnostic diagnoseEncoding(const QByteArray &data);

#endif // ENCODINGDIAGNOSTIC_H
