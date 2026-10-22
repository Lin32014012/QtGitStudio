#include <QtTest>
#include "GitStatusParser.h"
#include "GitDiffParser.h"
#include "GitBranchParser.h"

class TestParsers : public QObject {
    Q_OBJECT
private slots:
    void testStatusParserModified();
    void testStatusParserAdded();
    void testStatusParserDeleted();
    void testStatusParserRenamed();
    void testStatusParserConflict();
    void testStatusParserUntracked();
    void testDiffParserAdd();
    void testDiffParserDelete();
    void testDiffParserRename();
    void testDiffParserEmpty();
    void testBranchParserNormal();
    void testBranchParserDetached();
    void testBranchParserTracking();
};

void TestParsers::testStatusParserModified() {
    QByteArray input = "1 .M N... 100644 100644 100644 abc123 def456 file.txt\n";
    GitSnapshot snap = GitStatusParser::parse(input);
    QCOMPARE(snap.files.size(), 1);
    QCOMPARE(snap.files[0].status, FileStatus::Modified);
    QCOMPARE(snap.files[0].path, QString("file.txt"));
}

void TestParsers::testStatusParserAdded() {
    QByteArray input = "1 A. N... 000000 100644 100000 abc123 def456 new.txt\n";
    GitSnapshot snap = GitStatusParser::parse(input);
    QCOMPARE(snap.files.size(), 1);
    QCOMPARE(snap.files[0].status, FileStatus::Added);
    QCOMPARE(snap.files[0].path, QString("new.txt"));
}

void TestParsers::testStatusParserDeleted() {
    QByteArray input = "1 D. N... 100644 000000 000000 abc123 000000 old.txt\n";
    GitSnapshot snap = GitStatusParser::parse(input);
    QCOMPARE(snap.files.size(), 1);
    QCOMPARE(snap.files[0].status, FileStatus::Deleted);
    QCOMPARE(snap.files[0].path, QString("old.txt"));
}

void TestParsers::testStatusParserRenamed() {
    QByteArray input = "2 R. N... 100644 100644 100644 abc123 def456 R100 old.txt new.txt\n";
    GitSnapshot snap = GitStatusParser::parse(input);
    QCOMPARE(snap.files.size(), 1);
    QCOMPARE(snap.files[0].status, FileStatus::Renamed);
    QCOMPARE(snap.files[0].path, QString("new.txt"));
}

void TestParsers::testStatusParserConflict() {
    QByteArray input = "1 UU N... 100644 100644 100644 abc123 def456 conflict.txt\n";
    GitSnapshot snap = GitStatusParser::parse(input);
    QCOMPARE(snap.files.size(), 1);
    QCOMPARE(snap.files[0].status, FileStatus::Conflict);
    QCOMPARE(snap.files[0].path, QString("conflict.txt"));
}

void TestParsers::testStatusParserUntracked() {
    QByteArray input = "1 ?? N... 000000 000000 000000 000000 000000 untracked.txt\n";
    GitSnapshot snap = GitStatusParser::parse(input);
    QCOMPARE(snap.files.size(), 1);
    QCOMPARE(snap.files[0].status, FileStatus::Untracked);
    QCOMPARE(snap.files[0].path, QString("untracked.txt"));
}

void TestParsers::testDiffParserAdd() {
    QByteArray input = "5\t0\tfile.txt\n";
    GitSnapshot snap = GitDiffParser::parse(input);
    QCOMPARE(snap.diffs.size(), 1);
    QCOMPARE(snap.diffs[0].type, DiffEntry::Type::Add);
    QCOMPARE(snap.diffs[0].newPath, QString("file.txt"));
    QCOMPARE(snap.diffs[0].additions, 5);
}

void TestParsers::testDiffParserDelete() {
    QByteArray input = "0\t3\told.txt\n";
    GitSnapshot snap = GitDiffParser::parse(input);
    QCOMPARE(snap.diffs.size(), 1);
    QCOMPARE(snap.diffs[0].type, DiffEntry::Type::Delete);
    QCOMPARE(snap.diffs[0].oldPath, QString("old.txt"));
    QCOMPARE(snap.diffs[0].deletions, 3);
}

void TestParsers::testDiffParserRename() {
    QByteArray input = "2\t1\told.txt\tnew.txt\n";
    GitSnapshot snap = GitDiffParser::parse(input);
    QCOMPARE(snap.diffs.size(), 1);
    QCOMPARE(snap.diffs[0].type, DiffEntry::Type::Rename);
    QCOMPARE(snap.diffs[0].oldPath, QString("old.txt"));
    QCOMPARE(snap.diffs[0].newPath, QString("new.txt"));
}

void TestParsers::testDiffParserEmpty() {
    QByteArray input = "";
    GitSnapshot snap = GitDiffParser::parse(input);
    QCOMPARE(snap.diffs.size(), 0);
}

void TestParsers::testBranchParserNormal() {
    QByteArray input = "refs/heads/main\tabc123\trefs/heads/main\t\t\t*\n";
    GitSnapshot snap = GitBranchParser::parse(input);
    QCOMPARE(snap.branches.size(), 1);
    QCOMPARE(snap.branches[0].name, QString("refs/heads/main"));
    QCOMPARE(snap.branches[0].isCurrent, true);
}

void TestParsers::testBranchParserDetached() {
    QByteArray input = "HEAD\tabc123\tHEAD\t\t\t \n";
    GitSnapshot snap = GitBranchParser::parse(input);
    QCOMPARE(snap.branches.size(), 1);
    QCOMPARE(snap.branches[0].name, QString("HEAD"));
}

void TestParsers::testBranchParserTracking() {
    QByteArray input = "refs/heads/feature\tabc123\trefs/heads/feature\torigin/feature\tahead 3, behind 1\t \n";
    GitSnapshot snap = GitBranchParser::parse(input);
    QCOMPARE(snap.branches.size(), 1);
    QCOMPARE(snap.branches[0].name, QString("refs/heads/feature"));
    QCOMPARE(snap.branches[0].upstream, QString("origin/feature"));
    QCOMPARE(snap.branches[0].ahead, 3);
    QCOMPARE(snap.branches[0].behind, 1);
}

QTEST_MAIN(TestParsers)
#include "test_parsers.moc"
