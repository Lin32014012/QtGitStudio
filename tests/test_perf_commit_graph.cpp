// Commit-graph performance benchmark.
//
// Generates synthetic repositories with `git fast-import` (fast even for
// 10k commits): a linear `main` history plus a short feature branch every
// 250 commits that is merged back 10 commits later, so the layout has to
// handle real fork/merge lanes. Then measures, per repository size:
//   load      GitCommitGraphService::load   (git log + parse)
//   parse     GitCommitGraphService::parseLog only (pure CPU)
//   graph60   CommitGraphWidget::setCommits (default view, newest 60)
//   graphAll  CommitGraphWidget "show all"  (layout + scene for every commit)
//   relation  BranchRelationWidget::refresh (branch graph viewer)
//
// Sizes default to 1000,5000,10000; override with QGS_PERF_SIZES=...
// When QGS_PERF_OUTPUT is set, a Markdown table is appended to that file.
#include <QtTest>
#include <QElapsedTimer>
#include <QProcess>
#include <QTemporaryDir>

#include "BranchRelationWidget.h"
#include "CommitGraphWidget.h"
#include "GitCommitGraphService.h"

namespace {

QByteArray dataBlock(const QByteArray &payload) {
    return "data " + QByteArray::number(payload.size()) + "\n" + payload + "\n";
}

// Build a fast-import stream. Marks: main commits 1..n, feature commits
// start at n+1.
QByteArray buildStream(int n, int *branchCount) {
    QByteArray s;
    s.reserve(n * 220);
    const qint64 t0 = 1700000000;
    int nextFeatureMark = n + 1;
    int pendingMergeAt = -1;
    int pendingMergeMark = -1;
    *branchCount = 0;
    for (int i = 1; i <= n; ++i) {
        s += "commit refs/heads/main\nmark :" + QByteArray::number(i) + "\n";
        s += "committer Perf <perf@qtgitstudio.invalid> "
             + QByteArray::number(t0 + i * 60) + " +0000\n";
        s += dataBlock("main commit " + QByteArray::number(i));
        if (i > 1) {
            s += "from :" + QByteArray::number(i - 1) + "\n";
        }
        if (i == pendingMergeAt) {
            s += "merge :" + QByteArray::number(pendingMergeMark) + "\n";
            pendingMergeAt = -1;
        }
        s += "M 644 inline main.txt\n" + dataBlock("line " + QByteArray::number(i));

        if (i % 250 == 0 && i + 10 <= n) {
            ++*branchCount;
            const QByteArray ref = "refs/heads/feature-" + QByteArray::number(i);
            int parent = i;
            for (int k = 0; k < 3; ++k) {
                const int mark = nextFeatureMark++;
                s += "commit " + ref + "\nmark :" + QByteArray::number(mark) + "\n";
                s += "committer Perf <perf@qtgitstudio.invalid> "
                     + QByteArray::number(t0 + i * 60 + 10 + k) + " +0000\n";
                s += dataBlock("feature " + QByteArray::number(i) + " step "
                               + QByteArray::number(k));
                s += "from :" + QByteArray::number(parent) + "\n";
                s += "M 644 inline feature-" + QByteArray::number(i) + ".txt\n"
                     + dataBlock(QByteArray::number(k));
                parent = mark;
            }
            pendingMergeAt = i + 10;
            pendingMergeMark = parent;
        }
    }
    return s;
}

bool runGit(const QString &repo, const QStringList &args,
            const QByteArray &stdinData = QByteArray()) {
    QProcess p;
    p.setWorkingDirectory(repo);
    p.start(QStringLiteral("git"), args);
    if (!p.waitForStarted(10000)) return false;
    if (!stdinData.isEmpty()) {
        p.write(stdinData);
    }
    p.closeWriteChannel();
    if (!p.waitForFinished(300000)) return false;
    if (p.exitCode() != 0) {
        qWarning().noquote() << p.readAllStandardError();
    }
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

template <typename F>
double medianMs(int runs, F &&fn) {
    QList<double> samples;
    for (int r = 0; r < runs; ++r) {
        QElapsedTimer t;
        t.start();
        fn();
        samples << t.nsecsElapsed() / 1e6;
    }
    std::sort(samples.begin(), samples.end());
    return samples.at(samples.size() / 2);
}

} // namespace

class TestPerfCommitGraph : public QObject {
    Q_OBJECT
private slots:
    void commitGraph_data() {
        QTest::addColumn<int>("commits");
        QStringList sizes = qEnvironmentVariable("QGS_PERF_SIZES",
                                                 QStringLiteral("1000,5000,10000"))
                                .split(',', Qt::SkipEmptyParts);
        for (const QString &size : sizes) {
            const int n = size.trimmed().toInt();
            if (n > 0) {
                QTest::newRow(qPrintable(QStringLiteral("n=%1").arg(n))) << n;
            }
        }
    }

    void commitGraph() {
        QFETCH(int, commits);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString repo = dir.path();
        int branches = 0;
        QElapsedTimer genTimer;
        genTimer.start();
        QVERIFY(runGit(repo, {QStringLiteral("init"), QStringLiteral("-q"),
                              QStringLiteral("-b"), QStringLiteral("main")}));
        QVERIFY(runGit(repo, {QStringLiteral("fast-import"), QStringLiteral("--quiet")},
                       buildStream(commits, &branches)));
        QVERIFY(runGit(repo, {QStringLiteral("checkout"), QStringLiteral("-q"),
                              QStringLiteral("main")}));
        const double genMs = genTimer.nsecsElapsed() / 1e6;

        GitCommitGraphService service;
        GitCommitGraphService::LoadResult loaded;
        const double loadMs = medianMs(3, [&] { loaded = service.load(repo); });
        QVERIFY(loaded.success);
        const int total = loaded.commits.size();
        QCOMPARE(total, commits + branches * 3);

        // Re-run git log once to isolate pure parsing cost.
        QProcess log;
        log.setWorkingDirectory(repo);
        log.start(QStringLiteral("git"),
                  {QStringLiteral("log"), QStringLiteral("--all"),
                   QStringLiteral("--date-order"), QStringLiteral("--decorate=full"),
                   QStringLiteral("--pretty=format:%H%x09%P%x09%an%x09%aI%x09%s%x09%d")});
        QVERIFY(log.waitForFinished(120000));
        const QByteArray raw = log.readAllStandardOutput();
        const double parseMs = medianMs(3, [&] {
            const auto parsed = GitCommitGraphService::parseLog(raw);
            QCOMPARE(parsed.size(), total);
        });

        CommitGraphWidget graph;
        graph.resize(1200, 800);
        const double graph60Ms = medianMs(3, [&] { graph.setCommits(loaded.commits); });
        QCOMPARE(graph.nodeCount(), qMin(total, 60));
        const double graphAllMs = medianMs(1, [&] { graph.toggleShowAllCommits(); });
        QCOMPARE(graph.nodeCount(), total);

        BranchRelationWidget relation;
        relation.resize(1200, 800);
        relation.setRepositoryPath(repo);
        const double relationMs = medianMs(1, [&] { relation.refresh(); });

        const QString line = QStringLiteral(
            "| %1 | %2 | %3 | %4 | %5 | %6 | %7 | %8 |")
            .arg(commits).arg(total)
            .arg(loadMs, 0, 'f', 1).arg(parseMs, 0, 'f', 1)
            .arg(graph60Ms, 0, 'f', 1).arg(graphAllMs, 0, 'f', 1)
            .arg(relationMs, 0, 'f', 1).arg(genMs, 0, 'f', 0);
        qInfo().noquote() << "PERF" << line;
        const QString out = qEnvironmentVariable("QGS_PERF_OUTPUT");
        if (!out.isEmpty()) {
            QFile f(out);
            if (f.open(QIODevice::Append | QIODevice::Text)) {
                f.write(line.toUtf8() + '\n');
            }
        }
    }
};

QTEST_MAIN(TestPerfCommitGraph)
#include "test_perf_commit_graph.moc"
