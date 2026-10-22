// Unit tests for TextEncoding: detection order, lossless round-trip and
// per-line decoding of mixed git output (UTF-8 + GBK).
#include <QtTest>

#include "GitCommitDiffService.h"
#include "TextEncoding.h"
#include "gbk_fixture.h"

using TextEncoding::Kind;

class TestTextEncoding : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qInfo().noquote() << "GB18030 backend:" << TextEncoding::gb18030Backend();
    }

    void asciiIsUtf8() {
        const auto d = TextEncoding::decode("hello\n");
        QVERIFY(d.ok);
        QCOMPARE(d.kind, Kind::Utf8);
        QCOMPARE(d.text, QStringLiteral("hello\n"));
    }

    void utf8ChineseStaysUtf8() {
        const QByteArray bytes = QStringLiteral("中文内容\n").toUtf8();
        const auto d = TextEncoding::decode(bytes);
        QVERIFY(d.ok);
        QCOMPARE(d.kind, Kind::Utf8);
        bool ok = false;
        QCOMPARE(TextEncoding::encode(d.text, d.kind, &ok), bytes);
        QVERIFY(ok);
    }

    void utf8BomRoundTrip() {
        const QByteArray bytes = QByteArray("\xEF\xBB\xBF") + QStringLiteral("内容").toUtf8();
        const auto d = TextEncoding::decode(bytes);
        QVERIFY(d.ok);
        QCOMPARE(d.kind, Kind::Utf8Bom);
        QCOMPARE(d.text, QStringLiteral("内容"));
        bool ok = false;
        QCOMPARE(TextEncoding::encode(d.text, d.kind, &ok), bytes);
        QVERIFY(ok);
    }

    void utf16BomRoundTrip() {
        for (const bool le : {true, false}) {
            QStringEncoder enc(le ? QStringConverter::Utf16LE : QStringConverter::Utf16BE);
            const QByteArray bytes = QByteArray(le ? "\xFF\xFE" : "\xFE\xFF")
                + QByteArray(enc.encode(QStringLiteral("双字节")));
            const auto d = TextEncoding::decode(bytes);
            QVERIFY(d.ok);
            QCOMPARE(d.kind, le ? Kind::Utf16Le : Kind::Utf16Be);
            QCOMPARE(d.text, QStringLiteral("双字节"));
            bool ok = false;
            QCOMPARE(TextEncoding::encode(d.text, d.kind, &ok), bytes);
        }
    }

    void gbkIsDetectedAsGb18030() {
        if (!TextEncoding::isGb18030Available()) {
            QSKIP("GB18030 codec not available in this Qt build");
        }
        const auto d = TextEncoding::decode(GbkFixture::ours());
        QVERIFY(d.ok);
        QCOMPARE(d.kind, Kind::Gb18030);
        QCOMPARE(d.text, QStringLiteral("版本=我们的修改\r\n"));
        bool ok = false;
        QCOMPARE(TextEncoding::encode(d.text, d.kind, &ok), GbkFixture::ours());
        QVERIFY(ok);
    }

    void gb18030EncodesAnyUnicode() {
        if (!TextEncoding::isGb18030Available()) {
            QSKIP("GB18030 codec not available in this Qt build");
        }
        // Characters outside GBK (emoji) still encode losslessly in GB18030.
        const QString text = QStringLiteral("表情 \U0001F600");
        bool ok = false;
        const QByteArray bytes = TextEncoding::encode(text, Kind::Gb18030, &ok);
        QVERIFY(ok);
        QCOMPARE(TextEncoding::decode(bytes).text, text);
    }

    void undecodableBytesAreUnknown() {
        // 0xFF is never valid in UTF-8 nor as a GB18030 lead byte.
        const auto d = TextEncoding::decode(QByteArray("abc\xFF\xFF\n"));
        QVERIFY(!d.ok);
        QCOMPARE(d.kind, Kind::Unknown);
        bool ok = true;
        QVERIFY(TextEncoding::encode(QStringLiteral("x"), Kind::Unknown, &ok).isEmpty());
        QVERIFY(!ok);
    }

    void mixedDiffOutputDecodesPerLine() {
        if (!TextEncoding::isGb18030Available()) {
            QSKIP("GB18030 codec not available in this Qt build");
        }
        QByteArray diff = "diff --git a/a.txt b/a.txt\n@@ -1 +1 @@\n-";
        diff += GbkFixture::base().chopped(2);
        diff += "\n+";
        diff += QStringLiteral("UTF-8 行").toUtf8();
        diff += "\n";
        const QString text = TextEncoding::decodeMixedLines(diff);
        QVERIFY(text.contains(QStringLiteral("-版本=基线\n")));
        QVERIFY(text.contains(QStringLiteral("+UTF-8 行\n")));
        QVERIFY(!text.contains(QChar::ReplacementCharacter));
    }

    void commitDiffShowsGbkLines() {
        if (!TextEncoding::isGb18030Available()) {
            QSKIP("GB18030 codec not available in this Qt build");
        }
        QByteArray show = "diff --git a/gbk.txt b/gbk.txt\n--- a/gbk.txt\n+++ b/gbk.txt\n"
                          "@@ -1 +1 @@\n-";
        show += GbkFixture::base().chopped(2);
        show += "\n+";
        show += GbkFixture::ours().chopped(2);
        show += "\n";
        const CommitDiff diff = GitCommitDiffService::parseShow(QStringLiteral("abc"), show);
        QVERIFY(diff.diffText.contains(QStringLiteral("+版本=我们的修改")));
        QVERIFY(!diff.diffText.contains(QChar::ReplacementCharacter));
    }

    void pureUtf8DiffIsUnchanged() {
        const QByteArray diff = QStringLiteral("+中文\n-旧\n").toUtf8();
        QCOMPARE(TextEncoding::decodeMixedLines(diff), QString::fromUtf8(diff));
    }
};

QTEST_GUILESS_MAIN(TestTextEncoding)
#include "test_text_encoding.moc"
