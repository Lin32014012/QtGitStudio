#include <QtTest>

#include "ConflictMarkerParser.h"

class TestConflictMarkerParser : public QObject {
    Q_OBJECT

private slots:
    void simpleBlockPreservesSides();
    void multipleHunksKeepOrder();
    void crlfIsPreserved();
    void diff3ProvidesBase();
    void malformedMarkerReturnsError();
    void markerLikeTextAwayFromLineStartIsContent();
    void noConflictHasNoHunks();
    void unicodeContentIsPreserved();
};

void TestConflictMarkerParser::simpleBlockPreservesSides() {
    const auto parsed = ConflictMarkerParser::parse(
        QStringLiteral("before\n<<<<<<< ours\nleft\n=======\nright\n>>>>>>> theirs\nafter\n"));
    QVERIFY(parsed.isValid());
    QCOMPARE(parsed.hunks.size(), 1);
    QCOMPARE(parsed.hunks[0].ours, QStringLiteral("left\n"));
    QCOMPARE(parsed.hunks[0].theirs, QStringLiteral("right\n"));
    QVERIFY(!parsed.hunks[0].hasBase);
}

void TestConflictMarkerParser::multipleHunksKeepOrder() {
    const auto parsed = ConflictMarkerParser::parse(
        QStringLiteral("<<<<<<< a\none\n=======\ntwo\n>>>>>>> b\nmid\n<<<<<<< c\nthree\n=======\nfour\n>>>>>>> d\n"));
    QVERIFY(parsed.isValid());
    QCOMPARE(parsed.hunks.size(), 2);
    QCOMPARE(parsed.hunks[0].ours, QStringLiteral("one\n"));
    QCOMPARE(parsed.hunks[1].theirs, QStringLiteral("four\n"));
    QVERIFY(parsed.hunks[0].endOffset < parsed.hunks[1].startOffset);
}

void TestConflictMarkerParser::crlfIsPreserved() {
    const auto parsed = ConflictMarkerParser::parse(
        QStringLiteral("<<<<<<< a\r\nours\r\n=======\r\ntheirs\r\n>>>>>>> b\r\n"));
    QVERIFY(parsed.isValid());
    QCOMPARE(parsed.hunks[0].ours, QStringLiteral("ours\r\n"));
    QCOMPARE(parsed.hunks[0].theirs, QStringLiteral("theirs\r\n"));
}

void TestConflictMarkerParser::diff3ProvidesBase() {
    const auto parsed = ConflictMarkerParser::parse(
        QStringLiteral("<<<<<<< a\nours\n||||||| base\nbase\n=======\ntheirs\n>>>>>>> b\n"));
    QVERIFY(parsed.isValid());
    QCOMPARE(parsed.hunks.size(), 1);
    QVERIFY(parsed.hunks[0].hasBase);
    QCOMPARE(parsed.hunks[0].base, QStringLiteral("base\n"));
}

void TestConflictMarkerParser::malformedMarkerReturnsError() {
    const auto missingDivider = ConflictMarkerParser::parse(
        QStringLiteral("<<<<<<< a\ntext\n>>>>>>> b\n"));
    QVERIFY(!missingDivider.isValid());
    QVERIFY(!missingDivider.error.isEmpty());

    const auto unclosed = ConflictMarkerParser::parse(QStringLiteral("<<<<<<< a\ntext\n"));
    QVERIFY(!unclosed.isValid());
}

void TestConflictMarkerParser::markerLikeTextAwayFromLineStartIsContent() {
    const auto parsed = ConflictMarkerParser::parse(
        QStringLiteral("const char *text = \"<<<<<<< not a marker\";\n  ======= also text\n"));
    QVERIFY(parsed.isValid());
    QVERIFY(!parsed.hasConflicts());
}

void TestConflictMarkerParser::noConflictHasNoHunks() {
    const auto parsed = ConflictMarkerParser::parse(QStringLiteral("plain text\n"));
    QVERIFY(parsed.isValid());
    QVERIFY(!parsed.hasConflicts());
}

void TestConflictMarkerParser::unicodeContentIsPreserved() {
    const auto parsed = ConflictMarkerParser::parse(
        QStringLiteral("<<<<<<< 我们\n中文内容\n=======\n另一侧内容\n>>>>>>> 他们\n"));
    QVERIFY(parsed.isValid());
    QCOMPARE(parsed.hunks[0].ours, QStringLiteral("中文内容\n"));
    QCOMPARE(parsed.hunks[0].theirs, QStringLiteral("另一侧内容\n"));
}

QTEST_MAIN(TestConflictMarkerParser)
#include "test_conflict_marker_parser.moc"
