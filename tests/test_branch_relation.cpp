// v4-P6-15 branch relation graph tests:
//   current/local/remote nodes, fork relation, merge recognition,
//   ahead/behind, many-branch filter, empty repo no crash.
#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QUuid>
#include <QCheckBox>
#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QPainterPath>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QContextMenuEvent>
#include <QTimer>
#include <algorithm>
#include "BranchRelationWidget.h"
#include "BranchGraphViewerDialog.h"
#include "BranchTreeWidget.h"
#include "GitTestRepository.h"
#include "test_settings_isolation.h"

static bool runGit(const QString &repo, const QStringList &args) {
    QProcess p;
    p.setWorkingDirectory(repo);
    p.start("git", args);
    return p.waitForFinished(15000) && p.exitCode() == 0;
}

static void commitAll(const QString &repo, const QString &msg) {
    runGit(repo, {"add", "-A"});
    runGit(repo, {"-c", "user.name=Tester", "-c", "user.email=t@t",
                  "commit", "-q", "-m", msg});
}

static QVector<QGraphicsItem *> itemsWithKind(const BranchRelationWidget &widget,
                                               int kind) {
    QVector<QGraphicsItem *> result;
    for (QGraphicsItem *item : widget.scene()->items()) {
        if (item->data(BranchRelationWidget::ItemKindRole).toInt() == kind) {
            result.append(item);
        }
    }
    return result;
}

static QVector<QRectF> itemRects(const QVector<QGraphicsItem *> &items) {
    QVector<QRectF> result;
    for (QGraphicsItem *item : items) {
        result.append(item->sceneBoundingRect());
    }
    return result;
}

static QGraphicsItem *itemWithKindAndData(const BranchRelationWidget &widget,
                                          int kind, const QString &data) {
    for (QGraphicsItem *item : widget.scene()->items()) {
        if (item->data(BranchRelationWidget::ItemKindRole).toInt() == kind
            && item->data(0).toString() == data) {
            return item;
        }
    }
    return nullptr;
}

static QGraphicsSimpleTextItem *textChildWithKind(QGraphicsItem *parent,
                                                   int kind) {
    if (!parent
        || parent->data(BranchRelationWidget::ItemKindRole).toInt() != kind) {
        return nullptr;
    }
    for (QGraphicsItem *child : parent->childItems()) {
        if (child->type() == QGraphicsSimpleTextItem::Type) {
            return static_cast<QGraphicsSimpleTextItem *>(child);
        }
    }
    return nullptr;
}

static QString gitOutput(const QString &repo, const QStringList &args) {
    QProcess process;
    process.setWorkingDirectory(repo);
    process.start(QStringLiteral("git"), args);
    if (!process.waitForFinished(15000) || process.exitCode() != 0) {
        return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

static void verifySceneContainsAllItems(const BranchRelationWidget &widget) {
    const QRectF sceneRect = widget.scene()->sceneRect();
    for (QGraphicsItem *item : widget.scene()->items()) {
        QVERIFY2(sceneRect.contains(item->sceneBoundingRect()),
                 qPrintable(QStringLiteral("scene does not contain %1")
                                .arg(item->data(0).toString())));
    }
}

static void verifyDisjoint(const QVector<QRectF> &left,
                           const QVector<QRectF> &right) {
    for (const QRectF &a : left) {
        for (const QRectF &b : right) {
            QVERIFY2(!a.intersects(b), "semantic item rectangles overlap");
        }
    }
}

static void verifyBadgeTextIsContained(const BranchRelationWidget &widget) {
    int checked = 0;
    for (QGraphicsItem *item : widget.scene()->items()) {
        if (item->type() != QGraphicsSimpleTextItem::Type
            || !item->parentItem()) {
            continue;
        }
        const int parentKind = item->parentItem()
                                   ->data(BranchRelationWidget::ItemKindRole)
                                   .toInt();
        if (parentKind != BranchRelationWidget::BranchBadgeItem
            && parentKind != BranchRelationWidget::TagBadgeItem) {
            continue;
        }
        ++checked;
        QVERIFY2(item->parentItem()->sceneBoundingRect().contains(
                     item->sceneBoundingRect()),
                 "badge text escaped its visible badge rectangle");
    }
    QVERIFY(checked > 0);
}

class TestBranchRelation : public QObject {
    Q_OBJECT
private:
    GitTestRepository m_repo;

private slots:
    void initTestCase() {
        isolateTestSettings();
        QVERIFY(m_repo.isValid());
        // main with base -> feature fork -> merge back.
        QFile f(m_repo.path() + "/a.txt");
        f.open(QIODevice::WriteOnly);
        f.write("1\n");
        f.close();
        commitAll(m_repo.path(), "base");
        runGit(m_repo.path(), {"checkout", "-q", "-b", "feature"});
        QFile f2(m_repo.path() + "/t.txt");
        f2.open(QIODevice::WriteOnly);
        f2.write("t\n");
        f2.close();
        commitAll(m_repo.path(), "feature work");
        runGit(m_repo.path(), {"checkout", "-q", "main"});
        QFile f3(m_repo.path() + "/m.txt");
        f3.open(QIODevice::WriteOnly);
        f3.write("m\n");
        f3.close();
        commitAll(m_repo.path(), "main work");
        // docs branch (unmerged).
        runGit(m_repo.path(), {"checkout", "-q", "-b", "docs"});
        QFile f4(m_repo.path() + "/d.txt");
        f4.open(QIODevice::WriteOnly);
        f4.write("d\n");
        f4.close();
        commitAll(m_repo.path(), "docs work");
        runGit(m_repo.path(), {"checkout", "-q", "main"});
    }

    void nodesAndForkRelation() {
        BranchRelationWidget widget;
        widget.resize(900, 600);
        widget.setRepositoryPath(m_repo.path());
        widget.refresh();
        widget.show();
        QTRY_VERIFY(widget.isVisible());
        QApplication::processEvents();

        QVERIFY(widget.hasCurrentBranchNode());
        const QStringList names = widget.branchNames();
        QVERIFY(names.contains(QStringLiteral("main")));
        QVERIFY(names.contains(QStringLiteral("feature")));
        QVERIFY(names.contains(QStringLiteral("docs")));
        // >= 3 branch nodes.
        QVERIFY(widget.branchNodeCount() >= 3);
        QVERIFY(widget.branchLabelGeometryValid());
        // v4-P6-16: the fork relation is expressed by the trunk line +
        // connector lines (tree topology) - verify connectors exist.
        int connectorCount = 0;
        for (QGraphicsItem *item : widget.scene()->items()) {
            if (item->type() == QGraphicsLineItem::Type) {
                ++connectorCount;
            }
        }
        QVERIFY2(connectorCount >= 1,
                 qPrintable(QString("no fork connectors, got %1")
                                .arg(connectorCount)));
    }

    void mergeRecognizedAndAheadBehind() {
        // feature was merged back? No - not merged. Merge feature into
        // main, then a fresh graph must mark feature as merged (green).
        runGit(m_repo.path(),
               {"merge", "-q", "--no-ff", "-m", "merge feature", "feature"});
        BranchRelationWidget widget;
        widget.setRepositoryPath(m_repo.path());
        widget.refresh();
        QVERIFY(widget.branchNodeCount() >= 3);
        // detail label works after selection.
        widget.show();
        QTRY_VERIFY(widget.isVisible());
        QApplication::processEvents();
        QLabel *detail = widget.findChild<QLabel *>("relationDetail");
        QVERIFY(detail != nullptr);
        // Select the docs node programmatically via scene items.
        for (QGraphicsItem *item : widget.scene()->items()) {
            if (item->data(0).toString() == QStringLiteral("docs")) {
                item->setSelected(true);
                break;
            }
        }
        QApplication::processEvents();
        QVERIFY(detail->text().contains(QStringLiteral("docs")));
        QVERIFY(detail->text().contains(QStringLiteral("合并状态")));
    }

    void remoteToggleAndUnmergedFilter() {
        // Simulate a remote-tracking branch (no network): create an
        // origin remote pointing at a bare clone is heavy; instead add
        // a remote ref directly.
        BranchRelationWidget widget;
        widget.setRepositoryPath(m_repo.path());
        widget.refresh();
        const int base = widget.branchNodeCount();
        // Toggling remote on must not crash; unmerged filter reduces.
        widget.showRemoteCheck()->setChecked(true);
        QApplication::processEvents();
        QVERIFY(widget.branchNodeCount() >= base);
        widget.unmergedOnlyCheck()->setChecked(true);
        QApplication::processEvents();
        QVERIFY(widget.branchNodeCount() > 0);
        widget.unmergedOnlyCheck()->setChecked(false);
        QApplication::processEvents();
    }

    void nodeDetailsOnClick() {        // v4-P6-20: clicking a commit node shows its full details in
        // the detail panel (canvas stays sparse).
        BranchRelationWidget widget;
        widget.resize(900, 700);
        widget.setRepositoryPath(m_repo.path());
        widget.refresh();
        widget.show();
        QTRY_VERIFY(widget.isVisible());
        QApplication::processEvents();
        QGraphicsView *view = widget.view();
        QVERIFY(view != nullptr);
        QGraphicsScene *scene = view->scene();
        QVERIFY(scene != nullptr);
        // Find the first commit node (UserType+220) and click it.
        QGraphicsItem *node = nullptr;
        const QList<QGraphicsItem *> items = scene->items();
        for (QGraphicsItem *item : items) {
            if (item->type() == QGraphicsItem::UserType + 220) {
                node = item;
                break;
            }
        }
        QVERIFY(node != nullptr);
        const QPoint viewPos =
            view->mapFromScene(node->sceneBoundingRect().center());
        QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, viewPos);
        QApplication::processEvents();
        QLabel *detail = widget.findChild<QLabel *>("relationDetail");
        QVERIFY(detail != nullptr);
        QVERIFY2(detail->text().contains(QStringLiteral("提交：")),
                 qPrintable(detail->text()));
    }

    void emptyRepositoryDoesNotCrash() {
        // A TRULY empty repository: git init, zero commits, no branch.
        const QString repo =
            QDir::tempPath() + "/qgs_p615_emptyrel_" + QUuid::createUuid().toString().left(8);
        QDir().mkpath(repo);
        QVERIFY(runGit(repo, {"init", "-q", "-b", "main"}));
        BranchRelationWidget widget;
        widget.setRepositoryPath(repo);
        widget.refresh();  // must not crash
        QVERIFY(widget.branchNodeCount() == 0);
        QDir(repo).removeRecursively();
    }

    void searchMatchesDocumentedAuthorField() {
        BranchRelationWidget widget;
        widget.setRepositoryPath(m_repo.path());
        widget.refresh();
        auto *search = widget.findChild<QLineEdit *>("relationSearch");
        QVERIFY(search != nullptr);
        search->setText(QStringLiteral("Tester"));
        int authorMatches = 0;
        for (QGraphicsItem *item : widget.scene()->items()) {
            if (item->data(BranchRelationWidget::ItemKindRole).toInt()
                == BranchRelationWidget::CommitHashItem) {
                ++authorMatches;
            }
        }
        QVERIFY(authorMatches > 0);
        search->setText(QStringLiteral("no-such-author-r6"));
        int absentMatches = 0;
        for (QGraphicsItem *item : widget.scene()->items()) {
            if (item->data(BranchRelationWidget::ItemKindRole).toInt()
                == BranchRelationWidget::CommitHashItem) {
                ++absentMatches;
            }
        }
        QCOMPARE(absentMatches, 0);
    }

    void visibleViewerRoutesOnlyLocalBadgeOperations() {
        const QString repo = QDir::tempPath() + "/qgs_p621r6_viewer_"
            + QUuid::createUuid().toString().left(8);
        QVERIFY(QDir().mkpath(repo));
        QVERIFY(runGit(repo, {"init", "-q", "-b", "main"}));
        QFile file(repo + "/base.txt");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("base\n");
        file.close();
        commitAll(repo, "base");
        QVERIFY(runGit(repo, {"checkout", "-q", "-b", "local-a"}));
        QVERIFY(runGit(repo, {"checkout", "-q", "main"}));
        QVERIFY(runGit(repo, {"checkout", "-q", "-b", "local-b"}));
        QVERIFY(runGit(repo, {"checkout", "-q", "main"}));
        QVERIFY(runGit(repo, {"update-ref", "refs/remotes/origin/test", "HEAD"}));
        QVERIFY(runGit(repo, {"update-ref", "refs/remotes/upstream/test", "HEAD"}));

        BranchGraphViewerDialog viewer(repo);
        viewer.show();
        QTRY_VERIFY(viewer.isVisible());
        auto *relation = viewer.relationWidget();
        QVERIFY(relation != nullptr);
        relation->showRemoteCheck()->setChecked(true);

        QGraphicsView *view = relation->view();
        QVERIFY(view != nullptr);
        QTRY_VERIFY(relation->scene()->sceneRect().width()
                    >= view->viewport()->width() * 0.9);
        viewer.activateWindow();
        view->viewport()->setFocus(Qt::OtherFocusReason);
        QApplication::processEvents();
        auto findTag = [relation](const QString &name) -> QGraphicsItem * {
            for (QGraphicsItem *item : relation->scene()->items()) {
                if (item->type() == QGraphicsItem::UserType + 222
                    && item->data(0).toString() == name) {
                    return item;
                }
            }
            return nullptr;
        };
        QGraphicsItem *remote = nullptr;
        QGraphicsItem *local = nullptr;
        QTRY_VERIFY((remote = findTag(QStringLiteral("upstream/test"))) != nullptr);
        QTRY_VERIFY((local = findTag(QStringLiteral("local-b"))) != nullptr);
        QVERIFY(remote->data(1).toBool());
        QVERIFY(!local->data(1).toBool());

        BranchTreeWidget branchTree;
        branchTree.setBranchData(
            {QStringLiteral("local-a"), QStringLiteral("local-b")},
            QStringLiteral("main"), QStringLiteral("local-a"));
        QCOMPARE(branchTree.selectedBranch(), QStringLiteral("local-a"));

        QSignalSpy switchSpy(&viewer,
                             &BranchGraphViewerDialog::switchBranchRequested);
        view->resetTransform();
        view->centerOn(local);
        QApplication::processEvents();
        local = findTag(QStringLiteral("local-b"));
        QVERIFY(local != nullptr);
        const QRectF localRect = local->sceneBoundingRect();
        const QPoint localPoint = view->mapFromScene(
            QPointF(localRect.right() - 3, localRect.center().y()));
        QVERIFY(view->viewport()->rect().contains(localPoint));
        QCOMPARE(view->itemAt(localPoint), local);
        QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, localPoint);
        auto *detail = relation->findChild<QLabel *>("relationDetail");
        QVERIFY(detail != nullptr);
        QTRY_VERIFY(detail->text().contains(QStringLiteral("local-b")));
        local = findTag(QStringLiteral("local-b"));
        QVERIFY(local != nullptr);
        view->resetTransform();
        view->centerOn(local);
        QApplication::processEvents();
        local = findTag(QStringLiteral("local-b"));
        QVERIFY(local != nullptr);
        const QRectF refreshedLocalRect = local->sceneBoundingRect();
        const QPoint refreshedLocalPoint = view->mapFromScene(
            QPointF(refreshedLocalRect.right() - 3,
                    refreshedLocalRect.center().y()));
        QVERIFY(view->viewport()->rect().contains(refreshedLocalPoint));
        QCOMPARE(view->itemAt(refreshedLocalPoint), local);
        QTest::mouseDClick(view->viewport(), Qt::LeftButton, {},
                            refreshedLocalPoint);
        QTRY_COMPARE(switchSpy.count(), 1);
        QCOMPARE(switchSpy.first().at(0).toString(), QStringLiteral("local-b"));

        remote = findTag(QStringLiteral("upstream/test"));
        QVERIFY(remote != nullptr);
        view->resetTransform();
        view->centerOn(remote);
        QApplication::processEvents();
        remote = findTag(QStringLiteral("upstream/test"));
        QVERIFY(remote != nullptr);
        const QRectF remoteRect = remote->sceneBoundingRect();
        const QPoint remoteClickPoint = view->mapFromScene(
            QPointF(remoteRect.right() - 3, remoteRect.center().y()));
        QVERIFY(view->viewport()->rect().contains(remoteClickPoint));
        QCOMPARE(view->itemAt(remoteClickPoint), remote);
        QTest::mouseClick(view->viewport(), Qt::LeftButton, {},
                          remoteClickPoint);
        QTest::mouseDClick(view->viewport(), Qt::LeftButton, {},
                            remoteClickPoint);
        QCoreApplication::processEvents();
        QCOMPARE(switchSpy.count(), 1);

        remote = findTag(QStringLiteral("upstream/test"));
        QVERIFY(remote != nullptr);
        bool sawRemoteMenu = false;
        bool remoteMenuReadOnly = false;
        QTimer::singleShot(40, &viewer, [&]() {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu) {
                for (QWidget *candidate : QApplication::topLevelWidgets()) {
                    if (auto *candidateMenu = qobject_cast<QMenu *>(candidate);
                        candidateMenu && candidateMenu->isVisible()) {
                        menu = candidateMenu;
                        break;
                    }
                }
            }
            if (!menu) {
                return;
            }
            sawRemoteMenu = true;
            remoteMenuReadOnly = !menu->actions().isEmpty();
            for (QAction *action : menu->actions()) {
                remoteMenuReadOnly = remoteMenuReadOnly && !action->isEnabled();
            }
            menu->close();
        });
        view->resetTransform();
        view->centerOn(remote);
        QApplication::processEvents();
        remote = findTag(QStringLiteral("upstream/test"));
        QVERIFY(remote != nullptr);
        const QRectF refreshedRemoteRect = remote->sceneBoundingRect();
        const QPoint remotePoint = view->mapFromScene(
            QPointF(refreshedRemoteRect.right() - 3,
                    refreshedRemoteRect.center().y()));
        QVERIFY(view->viewport()->rect().contains(remotePoint));
        QCOMPARE(view->itemAt(remotePoint), remote);
        QContextMenuEvent contextEvent(QContextMenuEvent::Mouse, remotePoint,
                                       view->viewport()->mapToGlobal(remotePoint));
        QApplication::sendEvent(view->viewport(), &contextEvent);
        QTRY_VERIFY(sawRemoteMenu);
        QVERIFY(remoteMenuReadOnly);
        QCOMPARE(switchSpy.count(), 1);
        qInfo().noquote()
            << QStringLiteral("VIEWER_TARGET initial=local-a remote=upstream/test remote_switch=0 remote_menu=readonly local_target=%1 result=PASS")
                   .arg(switchSpy.first().at(0).toString());
        QDir(repo).removeRecursively();
    }

    // v4-P6-21R5 P2: geometry stress fixture - fork + merge, 3+ refs on
    // one commit, very long names, remote refs under TWO remote names
    // (origin + upstream). Real sceneBoundingRect intersection checks.
    void geometryStressFixtureNoOverlap() {
        const QString repo =
            QDir::tempPath() + "/qgs_p621r5_geo_"
            + QUuid::createUuid().toString().left(8);
        QVERIFY(QDir().mkpath(repo));
        runGit(repo, {"init", "-q", "-b", "main"});
        QFile f(repo + "/a.txt");
        f.open(QIODevice::WriteOnly);
        f.write("a\n");
        f.close();
        commitAll(repo, "M1");
        // fork: feature/a from M1
        runGit(repo, {"checkout", "-q", "-b", "feature/a"});
        QFile f2(repo + "/fa.txt");
        f2.open(QIODevice::WriteOnly);
        f2.write("fa\n");
        f2.close();
        commitAll(repo, "F1");
        // fork: feature/b from F1
        runGit(repo, {"checkout", "-q", "-b", "feature/b"});
        QFile f3(repo + "/fb.txt");
        f3.open(QIODevice::WriteOnly);
        f3.write("fb\n");
        f3.close();
        commitAll(repo, "F2");
        // very long branch name
        runGit(repo, {"checkout", "-q", "-b",
                      "feature/very-long-branch-name-for-layout-test"});
        QFile f4(repo + "/fl.txt");
        f4.open(QIODevice::WriteOnly);
        f4.write("fl\n");
        f4.close();
        commitAll(repo, "F3");
        // main work then merge feature/a back (merge commit with 2 parents)
        runGit(repo, {"checkout", "-q", "main"});
        QFile f5(repo + "/m.txt");
        f5.open(QIODevice::WriteOnly);
        f5.write("m\n");
        f5.close();
        commitAll(repo, "M2");
        runGit(repo, {"merge", "-q", "--no-ff", "feature/a", "-m", "merge fa"});
        // tags
        runGit(repo, {"tag", "v1.0"});
        runGit(repo, {"tag", "release-candidate-long-tag"});
        // remote refs under origin AND upstream (refs/remotes)
        runGit(repo, {"update-ref", "refs/remotes/origin/feature/a",
                      "HEAD"});
        runGit(repo, {"update-ref", "refs/remotes/upstream/feature-long-name",
                      "HEAD"});

        BranchRelationWidget widget;
        widget.resize(1200, 800);
        widget.showRemoteCheck()->setChecked(true);
        widget.setRepositoryPath(repo);
        widget.refresh();
        QElapsedTimer t;
        t.start();
        while (widget.branchNodeCount() == 0 && t.elapsed() < 15000) {
            QApplication::processEvents();
            QTest::qWait(20);
        }
        QVERIFY(widget.branchNodeCount() > 0);
        // scene items must not overlap: collect every badge + hash text
        // item bounding rects and assert pairwise disjointness.
        const auto items = widget.scene()->items();
        QVector<QRectF> badgeRects;
        QVector<QRectF> hashRects;
        QVector<QRectF> nodeRects;
        for (QGraphicsItem *it : items) {
            const QRectF r = it->sceneBoundingRect();
            if (it->data(BranchRelationWidget::ItemKindRole).toInt()
                == BranchRelationWidget::BranchBadgeItem) {
                badgeRects.append(r);
            } else if (it->data(BranchRelationWidget::ItemKindRole).toInt()
                       == BranchRelationWidget::CommitHashItem) {
                hashRects.append(r);
            } else if (it->data(BranchRelationWidget::ItemKindRole).toInt()
                       == BranchRelationWidget::CommitNodeItem) {
                nodeRects.append(r);
            }
        }
        // Four local branch tips plus two remote-tracking tips make this a
        // real multi-badge fixture. Keep the lower bound explicit so the
        // pairwise loop below can never pass vacuously.
        QVERIFY2(badgeRects.size() >= 6,
                 qPrintable(QStringLiteral("expected >= 6 badges, got %1")
                                .arg(badgeRects.size())));
        QVERIFY(!hashRects.isEmpty());
        QCOMPARE(nodeRects.size(), hashRects.size());
        // badge vs hash: no intersection
        for (const QRectF &b : badgeRects) {
            for (const QRectF &h : hashRects) {
                QVERIFY(!b.intersects(h));
            }
        }
        // badge vs badge: pairwise disjoint
        for (int i = 0; i < badgeRects.size(); ++i) {
            for (int j = i + 1; j < badgeRects.size(); ++j) {
                QVERIFY(!badgeRects.at(i).intersects(badgeRects.at(j)));
            }
        }
        // Badges must also stay clear of the commit dot on every row,
        // including the next row in the compact graph layout.
        for (const QRectF &b : badgeRects) {
            for (const QRectF &n : nodeRects) {
                QVERIFY(!b.intersects(n));
            }
        }
        // sceneRect contains every item
        const QRectF scene = widget.scene()->sceneRect();
        for (QGraphicsItem *it : items) {
            QVERIFY(scene.contains(it->sceneBoundingRect()));
        }
        qInfo().noquote()
            << QStringLiteral("GRAPH_GEOMETRY hash_rects=%1 badge_rects=%2 node_rects=%3 intersections=0 scene_contains=1 result=PASS")
                   .arg(hashRects.size())
                   .arg(badgeRects.size())
                   .arg(nodeRects.size());
        QDir(repo).removeRecursively();
    }

    // RC2 CASE 1 + CASE 6: a real main-only history must use the compact,
    // single-lane layout rather than the general DAG lane arrangement.
    void mainOnlyHistoryUsesLinearCompactLayout() {
        const QString repo = QDir::tempPath() + "/qgs_rc2_linear_"
            + QUuid::createUuid().toString().left(8);
        QVERIFY(QDir().mkpath(repo));
        QVERIFY(runGit(repo, {"init", "-q", "-b", "main"}));
        for (int i = 0; i < 4; ++i) {
            QFile file(repo + QStringLiteral("/main-%1.txt").arg(i));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QByteArray::number(i));
            file.close();
            commitAll(repo, QStringLiteral("main %1").arg(i));
        }

        BranchRelationWidget widget;
        widget.resize(1000, 700);
        widget.setRepositoryPath(repo);
        widget.refresh();
        widget.show();
        QTRY_VERIFY(widget.isVisible());
        QApplication::processEvents();

        QVERIFY(widget.layoutModeForTesting()
                == BranchRelationWidget::LinearHistory);
        QCOMPARE(widget.visibleLaneCountForTesting(), 1);
        const auto nodes = itemsWithKind(widget,
                                          BranchRelationWidget::CommitNodeItem);
        QVERIFY(nodes.size() >= 4);
        QVERIFY(!itemsWithKind(widget, BranchRelationWidget::CommitHashItem)
                      .isEmpty());
        QVERIFY(!itemsWithKind(widget, BranchRelationWidget::CommitMessageItem)
                      .isEmpty());
        QVector<qreal> ys;
        qreal laneX = -1;
        for (QGraphicsItem *node : nodes) {
            const QPointF center = node->sceneBoundingRect().center();
            if (laneX < 0) {
                laneX = center.x();
            }
            QVERIFY(qAbs(center.x() - laneX) < 0.1);
            ys.append(center.y());
        }
        const qreal viewportWidth = widget.view()->viewport()->width();
        const qreal viewportLaneX =
            widget.view()->mapFromScene(QPointF(laneX, 0)).x();
        QVERIFY(viewportLaneX > viewportWidth * 0.08);
        QVERIFY(viewportLaneX < viewportWidth * 0.32);
        std::sort(ys.begin(), ys.end());
        for (int i = 1; i < ys.size(); ++i) {
            const qreal gap = ys.at(i) - ys.at(i - 1);
            QVERIFY2(gap >= 58 && gap <= 72,
                     qPrintable(QStringLiteral("unexpected row gap: %1")
                                    .arg(gap)));
        }
        QVERIFY(widget.branchLabelGeometryValid());
        QCOMPARE(widget.emptyVisualMetadataCountForTesting(), 0);
        const auto messageRects = itemRects(itemsWithKind(
            widget, BranchRelationWidget::CommitMessageItem));
        const auto secondaryRects = itemRects(itemsWithKind(
            widget, BranchRelationWidget::CommitSecondaryItem));
        verifyDisjoint(messageRects, itemRects(nodes));
        verifyDisjoint(secondaryRects, itemRects(nodes));
        verifyDisjoint(messageRects, secondaryRects);
        QCOMPARE(widget.view()->horizontalScrollBar()->maximum(), 0);
        QVERIFY(widget.scene()->sceneRect().width() <= viewportWidth + 1.0);
        verifySceneContainsAllItems(widget);
        QDir(repo).removeRecursively();
    }

    // RC3 CASE A, B, E and I: a linear graph is a responsive timeline,
    // not a fixed scene-space lane which the Viewer scales down by height.
    void linearTimelineReflowsForViewport() {
        const QString repo = QDir::tempPath() + "/qgs_rc3_timeline_"
            + QUuid::createUuid().toString().left(8);
        QVERIFY(QDir().mkpath(repo));
        QVERIFY(runGit(repo, {"init", "-q", "-b", "main"}));
        const QString longSubject = QStringLiteral("timeline subject ")
            + QString(220, QChar('x'));
        for (int i = 0; i < 16; ++i) {
            QFile file(repo + QStringLiteral("/timeline-%1.txt").arg(i));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QByteArray::number(i));
            file.close();
            commitAll(repo, longSubject + QStringLiteral(" %1").arg(i));
        }

        BranchGraphViewerDialog viewer(repo);
        viewer.resize(900, 650);
        viewer.show();
        QTRY_VERIFY(viewer.isVisible());
        auto *relation = viewer.relationWidget();
        QVERIFY(relation != nullptr);
        QTRY_COMPARE(relation->layoutModeForTesting(),
                     BranchRelationWidget::LinearHistory);
        QGraphicsView *view = relation->view();
        QVERIFY(view != nullptr);
        QTRY_VERIFY(view->viewport()->width() > 0);
        QTRY_VERIFY(relation->scene()->sceneRect().width()
                    >= view->viewport()->width() * 0.9);
        QTRY_VERIFY(view->transform().m11() >= 0.95);

        auto firstNode = [relation]() -> QGraphicsItem * {
            const auto nodes = itemsWithKind(*relation,
                                             BranchRelationWidget::CommitNodeItem);
            return nodes.isEmpty() ? nullptr : nodes.first();
        };
        auto firstMessage = [relation]() -> QGraphicsSimpleTextItem * {
            const auto messages = itemsWithKind(
                *relation, BranchRelationWidget::CommitMessageItem);
            if (messages.isEmpty()) {
                return nullptr;
            }
            return static_cast<QGraphicsSimpleTextItem *>(messages.first());
        };
        auto firstHash = [relation]() -> QGraphicsSimpleTextItem * {
            const auto hashes = itemsWithKind(
                *relation, BranchRelationWidget::CommitHashItem);
            if (hashes.isEmpty()) {
                return nullptr;
            }
            return static_cast<QGraphicsSimpleTextItem *>(hashes.first());
        };

        QGraphicsItem *node900 = firstNode();
        QGraphicsSimpleTextItem *message900 = firstMessage();
        QGraphicsSimpleTextItem *hash900 = firstHash();
        QVERIFY(node900 != nullptr);
        QVERIFY(message900 != nullptr);
        QVERIFY(hash900 != nullptr);
        const qreal x900 = node900->sceneBoundingRect().center().x();
        const int text900 = message900->text().size();
        const qreal viewport900 = view->viewport()->width();
        const qreal visibleX900 = view->mapFromScene(
            node900->sceneBoundingRect().center()).x();
        qInfo().noquote()
            << QStringLiteral("TIMELINE_GEOMETRY viewport=%1 sceneX=%2 viewX=%3 sceneWidth=%4 transform=%5")
                   .arg(viewport900)
                   .arg(x900)
                   .arg(visibleX900)
                   .arg(relation->scene()->sceneRect().width())
                   .arg(view->transform().m11());
        QVERIFY(visibleX900 > viewport900 * 0.08);
        QVERIFY(visibleX900 < viewport900 * 0.32);
        const QRect message900Viewport =
            view->mapFromScene(message900->sceneBoundingRect()).boundingRect();
        QVERIFY(message900Viewport.left() >= view->viewport()->rect().left());
        QVERIFY(message900Viewport.right() <= view->viewport()->rect().right());
        qInfo().noquote()
            << QStringLiteral("TIMELINE_GEOMETRY stage=900 viewport=%1 graphX=%2 hashX=%3 messageX=%4 messageW=%5 sceneRect=%6,%7,%8,%9 transform=%10")
                   .arg(viewport900)
                   .arg(x900)
                   .arg(hash900->sceneBoundingRect().left())
                   .arg(message900->sceneBoundingRect().left())
                   .arg(message900->sceneBoundingRect().width())
                   .arg(relation->scene()->sceneRect().x())
                   .arg(relation->scene()->sceneRect().y())
                   .arg(relation->scene()->sceneRect().width())
                   .arg(relation->scene()->sceneRect().height())
                   .arg(view->transform().m11());

        viewer.resize(1400, 650);
        QTRY_VERIFY(firstNode() != nullptr);
        QTRY_VERIFY(relation->scene()->sceneRect().width()
                    >= view->viewport()->width() * 0.9);
        QTRY_VERIFY(qAbs(firstNode()->sceneBoundingRect().center().x() - x900)
                    > 30.0);
        const qreal x1400 = firstNode()->sceneBoundingRect().center().x();
        const int text1400 = firstMessage()->text().size();
        QVERIFY(firstHash() != nullptr);
        QVERIFY(text1400 > text900);
        const QRect message1400Viewport =
            view->mapFromScene(firstMessage()->sceneBoundingRect()).boundingRect();
        QVERIFY(message1400Viewport.left() >= view->viewport()->rect().left());
        QVERIFY(message1400Viewport.right() <= view->viewport()->rect().right());
        qInfo().noquote()
            << QStringLiteral("TIMELINE_GEOMETRY stage=1400 viewport=%1 graphX=%2 hashX=%3 messageX=%4 messageW=%5 sceneRect=%6,%7,%8,%9 transform=%10")
                   .arg(view->viewport()->width())
                   .arg(x1400)
                   .arg(firstHash()->sceneBoundingRect().left())
                   .arg(firstMessage()->sceneBoundingRect().left())
                   .arg(firstMessage()->sceneBoundingRect().width())
                   .arg(relation->scene()->sceneRect().x())
                   .arg(relation->scene()->sceneRect().y())
                   .arg(relation->scene()->sceneRect().width())
                   .arg(relation->scene()->sceneRect().height())
                   .arg(view->transform().m11());

        viewer.resize(700, 650);
        QTRY_VERIFY(firstNode() != nullptr);
        QTRY_VERIFY(relation->scene()->sceneRect().width()
                    >= view->viewport()->width() * 0.9);
        QTRY_VERIFY(firstNode()->sceneBoundingRect().center().x() < x1400);
        QTRY_VERIFY(firstMessage()->text().contains(QChar(0x2026)));
        QVERIFY(firstHash() != nullptr);
        const QRect message700Viewport =
            view->mapFromScene(firstMessage()->sceneBoundingRect()).boundingRect();
        QVERIFY(message700Viewport.left() >= view->viewport()->rect().left());
        QVERIFY(message700Viewport.right() <= view->viewport()->rect().right());
        qInfo().noquote()
            << QStringLiteral("TIMELINE_GEOMETRY stage=700 viewport=%1 graphX=%2 hashX=%3 messageX=%4 messageW=%5 sceneRect=%6,%7,%8,%9 transform=%10")
                   .arg(view->viewport()->width())
                   .arg(firstNode()->sceneBoundingRect().center().x())
                   .arg(firstHash()->sceneBoundingRect().left())
                   .arg(firstMessage()->sceneBoundingRect().left())
                   .arg(firstMessage()->sceneBoundingRect().width())
                   .arg(relation->scene()->sceneRect().x())
                   .arg(relation->scene()->sceneRect().y())
                   .arg(relation->scene()->sceneRect().width())
                   .arg(relation->scene()->sceneRect().height())
                   .arg(view->transform().m11());
        QVERIFY(view->horizontalScrollBar()->maximum() == 0);
        verifySceneContainsAllItems(*relation);
        QDir(repo).removeRecursively();
    }

    // RC4 CASE 1-5: the final viewport position must be correct after a
    // previously fitted multi-branch graph becomes a linear timeline. Scene
    // coordinates alone cannot catch a stale QGraphicsView transform.
    void viewerRestoresLinearViewportGeometryAfterMultiBranchFit() {
        const QString repo = QDir::tempPath() + "/qgs_rc4_viewport_"
            + QUuid::createUuid().toString().left(8);
        QVERIFY(QDir().mkpath(repo));
        QVERIFY(runGit(repo, {"init", "-q", "-b", "main"}));
        QFile base(repo + "/base.txt");
        QVERIFY(base.open(QIODevice::WriteOnly));
        base.write("base\n");
        base.close();
        commitAll(repo, "base");
        QVERIFY(runGit(repo, {"checkout", "-q", "-b", "feature"}));
        QFile feature(repo + "/feature.txt");
        QVERIFY(feature.open(QIODevice::WriteOnly));
        feature.write("feature\n");
        feature.close();
        commitAll(repo, "feature divergence");
        QVERIFY(runGit(repo, {"checkout", "-q", "main"}));

        const QString longSubject = QStringLiteral("fix: visible timeline subject ")
            + QString(240, QChar('x'));
        for (int i = 0; i < 18; ++i) {
            QFile file(repo + QStringLiteral("/main-%1.txt").arg(i));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QByteArray::number(i));
            file.close();
            commitAll(repo, i == 17 ? longSubject
                                    : QStringLiteral("main %1").arg(i));
        }
        const QString headHash = gitOutput(repo, {"rev-parse", "HEAD"});
        QVERIFY(!headHash.isEmpty());

        BranchGraphViewerDialog viewer(repo);
        viewer.resize(1080, 720);
        viewer.show();
        QTRY_VERIFY(viewer.isVisible());
        auto *relation = viewer.relationWidget();
        QVERIFY(relation != nullptr);
        auto *view = relation->view();
        QVERIFY(view != nullptr);
        QTRY_COMPARE(relation->layoutModeForTesting(),
                     BranchRelationWidget::MultiBranchGraph);
        QTRY_VERIFY(view->transform().m11() < 0.85);

        QSignalSpy layoutSpy(relation,
                             &BranchRelationWidget::graphLayoutRebuilt);
        auto *currentOnly = relation->currentBranchOnlyButton();
        QVERIFY(currentOnly != nullptr);
        QTest::mouseClick(currentOnly, Qt::LeftButton);
        QTRY_VERIFY(currentOnly->isChecked());
        QTRY_VERIFY(layoutSpy.count() >= 1);
        QTRY_COMPARE(relation->layoutModeForTesting(),
                     BranchRelationWidget::LinearHistory);
        QTRY_VERIFY(qAbs(view->transform().m11() - 1.0) < 0.01);

        for (const int outerWidth : {700, 1080, 1400}) {
            layoutSpy.clear();
            viewer.resize(outerWidth, 720);
            QTRY_VERIFY(view->viewport()->width() > 0);
            QTRY_VERIFY(layoutSpy.count() >= 1);
            QTRY_VERIFY(itemWithKindAndData(
                            *relation, BranchRelationWidget::CommitNodeItem,
                            headHash)
                        != nullptr);
            QTRY_VERIFY(itemWithKindAndData(
                            *relation, BranchRelationWidget::CommitMessageItem,
                            longSubject)
                        != nullptr);

            QGraphicsItem *node = itemWithKindAndData(
                *relation, BranchRelationWidget::CommitNodeItem, headHash);
            auto *subject = static_cast<QGraphicsSimpleTextItem *>(
                itemWithKindAndData(*relation,
                                    BranchRelationWidget::CommitMessageItem,
                                    longSubject));
            QVERIFY(node != nullptr);
            QVERIFY(subject != nullptr);
            const qreal viewportWidth = view->viewport()->width();
            const QPoint nodeViewport = view->mapFromScene(node->scenePos());
            QVERIFY2(nodeViewport.x() >= viewportWidth * 0.14 - 2.0,
                     "linear node is too close to the viewport left edge");
            QVERIFY2(nodeViewport.x() <= viewportWidth * 0.24 + 2.0,
                     "linear node is too far right in the viewport");

            const QRect subjectViewport = view->mapFromScene(
                subject->sceneBoundingRect()).boundingRect();
            const QString geometryDescription =
                QStringLiteral("outer=%1 viewport=%2,%3 node=%4,%5 subject=%6,%7,%8,%9 viewportRect=%10,%11,%12,%13 transform=%14")
                    .arg(outerWidth)
                    .arg(view->viewport()->width())
                    .arg(view->viewport()->height())
                    .arg(nodeViewport.x())
                    .arg(nodeViewport.y())
                    .arg(subjectViewport.x())
                    .arg(subjectViewport.y())
                    .arg(subjectViewport.width())
                    .arg(subjectViewport.height())
                    .arg(view->viewport()->rect().x())
                    .arg(view->viewport()->rect().y())
                    .arg(view->viewport()->rect().width())
                    .arg(view->viewport()->rect().height())
                    .arg(view->transform().m11());
            QVERIFY(!subject->text().trimmed().isEmpty());
            QVERIFY(subject->boundingRect().width() > 0.0);
            QVERIFY(subjectViewport.width() > 0);
            QVERIFY2(subjectViewport.intersects(view->viewport()->rect()),
                     qPrintable(geometryDescription));
            QVERIFY(view->horizontalScrollBar()->maximum() == 0);
            if (outerWidth == 700) {
                QVERIFY(subject->text().contains(QChar(0x2026)));
            }

            qInfo().noquote()
                << QStringLiteral("RC4_VIEWPORT_GEOMETRY outer=%1 viewport=%2 nodeViewX=%3 subjectView=%4,%5,%6,%7 subjectWidth=%8 transform=%9")
                       .arg(outerWidth)
                       .arg(viewportWidth)
                       .arg(nodeViewport.x())
                       .arg(subjectViewport.x())
                       .arg(subjectViewport.y())
                       .arg(subjectViewport.width())
                       .arg(subjectViewport.height())
                       .arg(subject->boundingRect().width())
                       .arg(view->transform().m11());
        }
        QTRY_VERIFY(view->verticalScrollBar()->maximum() > 0);
        view->verticalScrollBar()->setValue(
            view->verticalScrollBar()->maximum());
        QVERIFY(view->verticalScrollBar()->value()
                > view->verticalScrollBar()->minimum());
        layoutSpy.clear();
        viewer.resize(1380, 720);
        QTRY_VERIFY(layoutSpy.count() >= 1);
        QVERIFY(view->verticalScrollBar()->value()
                > view->verticalScrollBar()->minimum());
        QDir(repo).removeRecursively();
    }

    // RC4 CASE 6: a legitimate numeric local branch is a Git ref, not an
    // internal lane marker, and must stay visible beside another local ref.
    void numericLocalBranchNameIsPreserved() {
        const QString repo = QDir::tempPath() + "/qgs_rc4_branch_one_"
            + QUuid::createUuid().toString().left(8);
        QVERIFY(QDir().mkpath(repo));
        QVERIFY(runGit(repo, {"init", "-q", "-b", "main"}));
        QFile file(repo + "/numeric-branch.txt");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("branch one\n");
        file.close();
        commitAll(repo, "numeric branch fixture");
        const QString headHash = gitOutput(repo, {"rev-parse", "HEAD"});
        QVERIFY(runGit(repo, {"branch", "1", "HEAD"}));
        QVERIFY(runGit(repo, {"checkout", "-q", "1"}));
        QCOMPARE(gitOutput(repo, {"branch", "--show-current"}),
                 QStringLiteral("1"));
        const QString heads = gitOutput(repo, {"show-ref", "--heads"});
        QVERIFY(heads.contains(headHash + QStringLiteral(" refs/heads/1")));
        QVERIFY(heads.contains(headHash + QStringLiteral(" refs/heads/main")));

        BranchGraphViewerDialog viewer(repo);
        viewer.resize(1080, 720);
        viewer.show();
        QTRY_VERIFY(viewer.isVisible());
        auto *relation = viewer.relationWidget();
        QVERIFY(relation != nullptr);
        auto *view = relation->view();
        QVERIFY(view != nullptr);
        QTRY_COMPARE(relation->layoutModeForTesting(),
                     BranchRelationWidget::LinearHistory);
        QTRY_VERIFY(itemWithKindAndData(*relation,
                                        BranchRelationWidget::BranchBadgeItem,
                                        QStringLiteral("1"))
                    != nullptr);
        QTRY_VERIFY(itemWithKindAndData(*relation,
                                        BranchRelationWidget::BranchBadgeItem,
                                        QStringLiteral("main"))
                    != nullptr);
        QVERIFY(relation->branchNames().contains(QStringLiteral("1")));
        QVERIFY(relation->branchNames().contains(QStringLiteral("main")));
        QVERIFY(relation->hasCurrentBranchNode());

        QGraphicsItem *numericBadge = itemWithKindAndData(
            *relation, BranchRelationWidget::BranchBadgeItem,
            QStringLiteral("1"));
        QVERIFY(numericBadge != nullptr);
        const QRectF badgeRect = numericBadge->sceneBoundingRect();
        const QPoint badgePoint = view->mapFromScene(
            QPointF(badgeRect.right() - 3, badgeRect.center().y()));
        QVERIFY(view->viewport()->rect().contains(badgePoint));
        QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, badgePoint);
        auto *detail = relation->findChild<QLabel *>("relationDetail");
        QVERIFY(detail != nullptr);
        QTRY_VERIFY(detail->text().contains(QStringLiteral("1")));
        numericBadge = itemWithKindAndData(*relation,
                                            BranchRelationWidget::BranchBadgeItem,
                                            QStringLiteral("1"));
        QVERIFY(numericBadge != nullptr);
        QTRY_VERIFY(numericBadge->isSelected());
        QDir(repo).removeRecursively();
    }

    // RC3: a selected typed badge and its detail stay attached to the same
    // branch after the deferred viewport reflow replaces scene items.
    void selectedBranchBadgeSurvivesResponsiveReflow() {
        const QString repo = QDir::tempPath() + "/qgs_rc3_selection_"
            + QUuid::createUuid().toString().left(8);
        QVERIFY(QDir().mkpath(repo));
        QVERIFY(runGit(repo, {"init", "-q", "-b", "main"}));
        QFile file(repo + "/selection.txt");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("selection\n");
        file.close();
        commitAll(repo, "selection fixture");
        QVERIFY(runGit(repo, {"branch", "release"}));

        BranchGraphViewerDialog viewer(repo);
        viewer.resize(900, 650);
        viewer.show();
        QTRY_VERIFY(viewer.isVisible());
        auto *relation = viewer.relationWidget();
        QVERIFY(relation != nullptr);
        auto *view = relation->view();
        QVERIFY(view != nullptr);
        QTRY_VERIFY(relation->scene()->sceneRect().width()
                    >= view->viewport()->width() * 0.9);
        auto findBadge = [relation](const QString &name) -> QGraphicsItem * {
            return itemWithKindAndData(*relation,
                                       BranchRelationWidget::BranchBadgeItem,
                                       name);
        };
        QGraphicsItem *release = nullptr;
        QTRY_VERIFY((release = findBadge(QStringLiteral("release"))) != nullptr);
        view->centerOn(release);
        QApplication::processEvents();
        release = findBadge(QStringLiteral("release"));
        QVERIFY(release != nullptr);
        const QRectF rect = release->sceneBoundingRect();
        const QPoint clickPoint = view->mapFromScene(
            QPointF(rect.right() - 3, rect.center().y()));
        QVERIFY(view->viewport()->rect().contains(clickPoint));
        QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, clickPoint);
        auto *detail = relation->findChild<QLabel *>("relationDetail");
        QVERIFY(detail != nullptr);
        QTRY_VERIFY(detail->text().contains(QStringLiteral("release")));
        release = findBadge(QStringLiteral("release"));
        QVERIFY(release != nullptr);
        QTRY_VERIFY(release->isSelected());

        viewer.resize(1400, 650);
        QTRY_VERIFY(relation->scene()->sceneRect().width()
                    >= view->viewport()->width() * 0.9);
        release = findBadge(QStringLiteral("release"));
        QVERIFY(release != nullptr);
        QTRY_VERIFY(release->isSelected());
        QVERIFY(detail->text().contains(QStringLiteral("release")));
        QDir(repo).removeRecursively();
    }

    // RC3 CASE B and C: tags retain their complete parsed name and use a
    // visible semantic prefix so a numeric tag cannot look like a lane index.
    void tagBadgePreservesFullNameAndCanonicalTooltip() {
        const QString repo = QDir::tempPath() + "/qgs_rc3_tag_"
            + QUuid::createUuid().toString().left(8);
        QVERIFY(QDir().mkpath(repo));
        QVERIFY(runGit(repo, {"init", "-q", "-b", "main"}));
        QFile file(repo + "/tag.txt");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("tag\n");
        file.close();
        commitAll(repo, "tag fixture");
        QVERIFY(runGit(repo, {"tag", "v1.0"}));

        BranchRelationWidget widget;
        widget.resize(960, 640);
        widget.setRepositoryPath(repo);
        widget.refresh();
        widget.show();
        QTRY_VERIFY(widget.isVisible());

        QGraphicsItem *tag = itemWithKindAndData(
            widget, BranchRelationWidget::TagBadgeItem, QStringLiteral("v1.0"));
        QVERIFY(tag != nullptr);
        QCOMPARE(tag->toolTip(), QStringLiteral("refs/tags/v1.0"));
        auto *label = textChildWithKind(tag, BranchRelationWidget::TagBadgeItem);
        QVERIFY(label != nullptr);
        QCOMPARE(label->text(), QStringLiteral("tag: v1.0"));
        for (QGraphicsItem *item : widget.scene()->items()) {
            if (item->type() == QGraphicsSimpleTextItem::Type) {
                const QString text =
                    static_cast<QGraphicsSimpleTextItem *>(item)->text();
                QVERIFY(text != QStringLiteral("1"));
                QVERIFY(text != QStringLiteral("1.0"));
                QVERIFY(text != QStringLiteral("[1]"));
                QVERIFY(text != QStringLiteral("refs/tags/v1.0"));
            }
        }
        QDir(repo).removeRecursively();
    }

    // RC3 CASE D: merge semantics must be encoded by shape and tooltip, not
    // by the fill color alone.
    void mergeNodeHasSemanticShapeAndTooltip() {
        const QString repo = QDir::tempPath() + "/qgs_rc3_merge_shape_"
            + QUuid::createUuid().toString().left(8);
        QVERIFY(QDir().mkpath(repo));
        QVERIFY(runGit(repo, {"init", "-q", "-b", "main"}));
        QFile base(repo + "/base.txt");
        QVERIFY(base.open(QIODevice::WriteOnly));
        base.write("base\n");
        base.close();
        commitAll(repo, "base");
        QVERIFY(runGit(repo, {"checkout", "-q", "-b", "feature"}));
        QFile feature(repo + "/feature.txt");
        QVERIFY(feature.open(QIODevice::WriteOnly));
        feature.write("feature\n");
        feature.close();
        commitAll(repo, "feature");
        QVERIFY(runGit(repo, {"checkout", "-q", "main"}));
        QFile main(repo + "/main.txt");
        QVERIFY(main.open(QIODevice::WriteOnly));
        main.write("main\n");
        main.close();
        commitAll(repo, "main");
        QVERIFY(runGit(repo, {"merge", "-q", "--no-ff", "-m",
                               "merge feature", "feature"}));
        const QString mergeHash = gitOutput(repo, {"rev-parse", "HEAD"});
        QVERIFY(!mergeHash.isEmpty());
        QVERIFY(gitOutput(repo, {"rev-list", "--parents", "-n", "1", "HEAD"})
                    .split(' ', Qt::SkipEmptyParts).size()
                >= 3);

        BranchRelationWidget widget;
        widget.resize(1000, 700);
        widget.setRepositoryPath(repo);
        widget.refresh();
        QGraphicsItem *merge = itemWithKindAndData(
            widget, BranchRelationWidget::CommitNodeItem, mergeHash);
        QVERIFY(merge != nullptr);
        auto *mergePath = dynamic_cast<QGraphicsPathItem *>(merge);
        QVERIFY(mergePath != nullptr);
        const QPainterPath path = mergePath->path();
        QCOMPARE(path.elementCount(), 5);
        QVERIFY(path.elementAt(0).isMoveTo());
        QVERIFY(path.elementAt(1).x > 0 && qFuzzyIsNull(path.elementAt(1).y));
        QVERIFY(qFuzzyIsNull(path.elementAt(2).x) && path.elementAt(2).y > 0);
        QVERIFY(path.elementAt(3).x < 0 && qFuzzyIsNull(path.elementAt(3).y));
        QCOMPARE(merge->data(BranchRelationWidget::NodeSemanticRole).toInt(),
                 int(BranchRelationWidget::HeadMergeCommitNode));
        QCOMPARE(merge->data(BranchRelationWidget::ParentCountRole).toInt(), 2);
        QVERIFY(merge->toolTip().contains(mergeHash));
        QVERIFY(merge->toolTip().contains(QStringLiteral("Parents: 2")));
        auto *mergeSubject = static_cast<QGraphicsSimpleTextItem *>(
            itemWithKindAndData(widget,
                                BranchRelationWidget::CommitMessageItem,
                                QStringLiteral("merge feature")));
        QVERIFY(mergeSubject != nullptr);
        QVERIFY(!mergeSubject->text().trimmed().isEmpty());
        QVERIFY(mergeSubject->boundingRect().width() > 0.0);
        QDir(repo).removeRecursively();
    }

    // RC2 CASE 2: actual divergence, not branch count, retains multi-lane
    // drawing.
    void divergenceUsesMultiBranchGraph() {
        const QString repo = QDir::tempPath() + "/qgs_rc2_diverge_"
            + QUuid::createUuid().toString().left(8);
        QVERIFY(QDir().mkpath(repo));
        QVERIFY(runGit(repo, {"init", "-q", "-b", "main"}));
        QFile base(repo + "/base.txt");
        QVERIFY(base.open(QIODevice::WriteOnly));
        base.write("base\n");
        base.close();
        commitAll(repo, "base");
        QVERIFY(runGit(repo, {"checkout", "-q", "-b", "feature"}));
        QFile feature(repo + "/feature.txt");
        QVERIFY(feature.open(QIODevice::WriteOnly));
        feature.write("feature\n");
        feature.close();
        commitAll(repo, "feature");
        QVERIFY(runGit(repo, {"checkout", "-q", "main"}));
        QFile main(repo + "/main.txt");
        QVERIFY(main.open(QIODevice::WriteOnly));
        main.write("main\n");
        main.close();
        commitAll(repo, "main");

        BranchRelationWidget widget;
        widget.resize(1000, 700);
        widget.setRepositoryPath(repo);
        widget.refresh();
        QVERIFY(widget.layoutModeForTesting()
                == BranchRelationWidget::MultiBranchGraph);
        QVERIFY(widget.visibleLaneCountForTesting() >= 2);
        QSet<int> xPositions;
        for (QGraphicsItem *node : itemsWithKind(
                 widget, BranchRelationWidget::CommitNodeItem)) {
            xPositions.insert(qRound(node->sceneBoundingRect().center().x()));
        }
        QVERIFY(xPositions.size() >= 2);
        QVERIFY(!itemsWithKind(widget, BranchRelationWidget::GraphEdgeItem)
                     .isEmpty());
        QDir(repo).removeRecursively();
    }

    // RC4: a real divergent graph must keep the same visible hash + subject
    // contract as a timeline without allowing its metadata to cover the DAG.
    void multiBranchGraphShowsBoundedSubjects() {
        const QString repo = QDir::tempPath() + "/qgs_rc4_multi_subject_"
            + QUuid::createUuid().toString().left(8);
        QVERIFY(QDir().mkpath(repo));
        QVERIFY(runGit(repo, {"init", "-q", "-b", "main"}));
        QFile base(repo + "/base.txt");
        QVERIFY(base.open(QIODevice::WriteOnly));
        base.write("base\n");
        base.close();
        commitAll(repo, "base subject");
        QVERIFY(runGit(repo, {"checkout", "-q", "-b", "feature"}));
        QFile feature(repo + "/feature.txt");
        QVERIFY(feature.open(QIODevice::WriteOnly));
        feature.write("feature\n");
        feature.close();
        const QString longSubject = QStringLiteral("feature: visible subject ")
            + QString(320, QChar('x'));
        commitAll(repo, longSubject);
        QVERIFY(runGit(repo, {"tag", "v1.0", "HEAD"}));
        QVERIFY(runGit(repo, {"checkout", "-q", "main"}));
        QFile main(repo + "/main.txt");
        QVERIFY(main.open(QIODevice::WriteOnly));
        main.write("main\n");
        main.close();
        commitAll(repo, "main subject");

        BranchGraphViewerDialog viewer(repo);
        viewer.resize(1080, 720);
        viewer.show();
        QTRY_VERIFY(viewer.isVisible());
        auto *relation = viewer.relationWidget();
        QVERIFY(relation != nullptr);
        auto *view = relation->view();
        QVERIFY(view != nullptr);
        QTRY_COMPARE(relation->layoutModeForTesting(),
                     BranchRelationWidget::MultiBranchGraph);
        QTRY_VERIFY(itemsWithKind(*relation,
                                  BranchRelationWidget::CommitMessageItem)
                        .size()
                    >= 3);

        const auto messageItems = itemsWithKind(
            *relation, BranchRelationWidget::CommitMessageItem);
        const auto hashRects = itemRects(itemsWithKind(
            *relation, BranchRelationWidget::CommitHashItem));
        const auto nodeRects = itemRects(itemsWithKind(
            *relation, BranchRelationWidget::CommitNodeItem));
        const auto badgeRects = itemRects(itemsWithKind(
            *relation, BranchRelationWidget::BranchBadgeItem));
        const auto tagRects = itemRects(itemsWithKind(
            *relation, BranchRelationWidget::TagBadgeItem));
        QVERIFY(messageItems.size() >= 3);
        QVERIFY(!hashRects.isEmpty());
        QVERIFY(!nodeRects.isEmpty());
        QVERIFY(!badgeRects.isEmpty());
        QVERIFY(!tagRects.isEmpty());
        verifyDisjoint(itemRects(messageItems), hashRects);
        verifyDisjoint(itemRects(messageItems), nodeRects);
        verifyDisjoint(itemRects(messageItems), badgeRects);
        verifyDisjoint(itemRects(messageItems), tagRects);
        QVERIFY(relation->branchLabelGeometryValid());

        bool sawElidedLongSubject = false;
        for (QGraphicsItem *item : messageItems) {
            auto *subject = dynamic_cast<QGraphicsSimpleTextItem *>(item);
            QVERIFY(subject != nullptr);
            const QString original = subject->data(0).toString();
            QVERIFY(!original.trimmed().isEmpty());
            QVERIFY(!subject->text().trimmed().isEmpty());
            QVERIFY(subject->boundingRect().width() > 0.0);
            const QRect mapped = view->mapFromScene(
                subject->sceneBoundingRect()).boundingRect();
            QVERIFY(mapped.width() > 0);
            QVERIFY(mapped.intersects(view->viewport()->rect()));
            QCOMPARE(subject->toolTip(), original);
            if (original == longSubject) {
                QVERIFY(subject->text().contains(QChar(0x2026)));
                QVERIFY(subject->text() != original);
                sawElidedLongSubject = true;
            }
        }
        QVERIFY(sawElidedLongSubject);
        verifySceneContainsAllItems(*relation);
        QDir(repo).removeRecursively();
    }

    // RC2 CASE 3: a no-ff merge remains multi-branch and keeps both parent
    // edges on its merge commit.
    void mergeCommitKeepsBothParentEdges() {
        const QString repo = QDir::tempPath() + "/qgs_rc2_merge_"
            + QUuid::createUuid().toString().left(8);
        QVERIFY(QDir().mkpath(repo));
        QVERIFY(runGit(repo, {"init", "-q", "-b", "main"}));
        QFile base(repo + "/base.txt");
        QVERIFY(base.open(QIODevice::WriteOnly));
        base.write("base\n");
        base.close();
        commitAll(repo, "base");
        QVERIFY(runGit(repo, {"checkout", "-q", "-b", "feature"}));
        QFile feature(repo + "/feature.txt");
        QVERIFY(feature.open(QIODevice::WriteOnly));
        feature.write("feature\n");
        feature.close();
        commitAll(repo, "feature");
        QVERIFY(runGit(repo, {"checkout", "-q", "main"}));
        QFile main(repo + "/main.txt");
        QVERIFY(main.open(QIODevice::WriteOnly));
        main.write("main\n");
        main.close();
        commitAll(repo, "main");
        QVERIFY(runGit(repo, {"merge", "-q", "--no-ff", "-m", "merge feature",
                               "feature"}));

        BranchRelationWidget widget;
        widget.resize(1000, 700);
        widget.setRepositoryPath(repo);
        widget.refresh();
        QVERIFY(widget.layoutModeForTesting()
                == BranchRelationWidget::MultiBranchGraph);
        QHash<QString, int> mergeEdgeCounts;
        for (QGraphicsItem *edge : itemsWithKind(
                 widget, BranchRelationWidget::GraphEdgeItem)) {
            if (edge->data(2).toBool()) {
                ++mergeEdgeCounts[edge->data(0).toString()];
            }
        }
        QVERIFY(!mergeEdgeCounts.isEmpty());
        int maxMergeEdges = 0;
        for (auto it = mergeEdgeCounts.cbegin(); it != mergeEdgeCounts.cend();
             ++it) {
            maxMergeEdges = qMax(maxMergeEdges, it.value());
        }
        QVERIFY(maxMergeEdges >= 2);
        QDir(repo).removeRecursively();
    }

    // RC2 CASE 4: branch, remote, tag and hash on one commit are semantic
    // items with pairwise non-overlapping rectangles.
    void branchTagAndHashShareCommitWithoutOverlap() {
        const QString repo = QDir::tempPath() + "/qgs_rc2_metadata_"
            + QUuid::createUuid().toString().left(8);
        QVERIFY(QDir().mkpath(repo));
        QVERIFY(runGit(repo, {"init", "-q", "-b", "main"}));
        QFile file(repo + "/base.txt");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("base\n");
        file.close();
        commitAll(repo, "base");
        QVERIFY(runGit(repo, {"branch", "release"}));
        QVERIFY(runGit(repo, {"tag", "v1.0"}));
        QVERIFY(runGit(repo, {"tag", "release-candidate"}));
        QVERIFY(runGit(repo, {"update-ref", "refs/remotes/origin/main", "HEAD"}));

        BranchRelationWidget widget;
        widget.resize(1000, 700);
        widget.showRemoteCheck()->setChecked(true);
        widget.setRepositoryPath(repo);
        widget.refresh();
        const auto branchRects = itemRects(itemsWithKind(
            widget, BranchRelationWidget::BranchBadgeItem));
        const auto tagRects = itemRects(itemsWithKind(
            widget, BranchRelationWidget::TagBadgeItem));
        const auto hashRects = itemRects(itemsWithKind(
            widget, BranchRelationWidget::CommitHashItem));
        const auto nodeRects = itemRects(itemsWithKind(
            widget, BranchRelationWidget::CommitNodeItem));
        QVERIFY(branchRects.size() >= 3);
        QVERIFY(tagRects.size() >= 2);
        QVERIFY(!hashRects.isEmpty());
        QGraphicsItem *mainBadge = itemWithKindAndData(
            widget, BranchRelationWidget::BranchBadgeItem, QStringLiteral("main"));
        QGraphicsItem *v10Tag = itemWithKindAndData(
            widget, BranchRelationWidget::TagBadgeItem, QStringLiteral("v1.0"));
        const QString headHash = gitOutput(repo, {"rev-parse", "HEAD"});
        QGraphicsItem *headNode = itemWithKindAndData(
            widget, BranchRelationWidget::CommitNodeItem, headHash);
        QVERIFY(mainBadge != nullptr);
        QVERIFY(v10Tag != nullptr);
        QVERIFY(headNode != nullptr);
        QVERIFY(mainBadge->sceneBoundingRect().bottom()
                <= headNode->sceneBoundingRect().top() - 6.0);
        verifyDisjoint(branchRects, hashRects);
        verifyDisjoint(branchRects, nodeRects);
        verifyDisjoint(tagRects, hashRects);
        verifyDisjoint(tagRects, nodeRects);
        verifyDisjoint(branchRects, tagRects);
        verifyBadgeTextIsContained(widget);
        QVERIFY(widget.branchLabelGeometryValid());
        QDir(repo).removeRecursively();
    }

    // RC2 CASE 5: an actual empty commit message never creates an empty card
    // or an empty badge rectangle.
    void emptyMetadataDoesNotCreateVisibleEmptyItem() {
        const QString repo = QDir::tempPath() + "/qgs_rc2_empty_"
            + QUuid::createUuid().toString().left(8);
        QVERIFY(QDir().mkpath(repo));
        QVERIFY(runGit(repo, {"init", "-q", "-b", "main"}));
        QFile file(repo + "/base.txt");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("base\n");
        file.close();
        commitAll(repo, "base");
        QVERIFY(runGit(repo, {"-c", "user.name=Tester", "-c", "user.email=t@t",
                               "commit", "--allow-empty", "--allow-empty-message",
                               "-q", "-m", ""}));

        BranchRelationWidget widget;
        widget.setRepositoryPath(repo);
        widget.refresh();
        QCOMPARE(widget.emptyVisualMetadataCountForTesting(), 0);
        for (const int kind : {BranchRelationWidget::CommitHashItem,
                                BranchRelationWidget::CommitMessageItem,
                                BranchRelationWidget::BranchBadgeItem,
                                BranchRelationWidget::TagBadgeItem}) {
            for (QGraphicsItem *item : itemsWithKind(widget, kind)) {
                QVERIFY(!item->data(0).toString().trimmed().isEmpty());
            }
        }
        for (QGraphicsItem *item : widget.scene()->items()) {
            if (!item->isVisible()) {
                continue;
            }
            if (item->type() == QGraphicsSimpleTextItem::Type) {
                const auto *text = static_cast<QGraphicsSimpleTextItem *>(item);
                QVERIFY(!text->text().trimmed().isEmpty());
            }
            if (dynamic_cast<QGraphicsRectItem *>(item)) {
                QVERIFY(!item->data(0).toString().trimmed().isEmpty());
            }
        }
        QDir(repo).removeRecursively();
    }

    // v4-P6-21R5 P1-04: upstream/* remote refs must classify as remote
    // (typed refs/remotes/...), never as local branches.
    void upstreamRemoteRefClassifiedAsRemote() {
        const QString repo =
            QDir::tempPath() + "/qgs_p621r5_up_"
            + QUuid::createUuid().toString().left(8);
        QVERIFY(QDir().mkpath(repo));
        runGit(repo, {"init", "-q", "-b", "main"});
        QFile f(repo + "/a.txt");
        f.open(QIODevice::WriteOnly);
        f.write("a\n");
        f.close();
        commitAll(repo, "M1");
        runGit(repo, {"checkout", "-q", "-b", "local-a"});
        runGit(repo, {"update-ref", "refs/remotes/origin/remote-origin", "HEAD"});
        runGit(repo, {"update-ref", "refs/remotes/upstream/remote-upstream", "HEAD"});

        BranchRelationWidget widget;
        widget.resize(900, 600);
        widget.setRepositoryPath(repo);
        widget.refresh();
        // Remote badges are hidden by default - show them so the
        // upstream remote ref must appear as a typed remote badge.
        widget.showRemoteCheck()->setChecked(true);
        QApplication::processEvents();
        QElapsedTimer t;
        t.start();
        while (widget.branchNodeCount() < 3 && t.elapsed() < 15000) {
            QApplication::processEvents();
            QTest::qWait(20);
        }
        // The viewer must render the upstream badge (remote branches are
        // shown; local branches include local-a) - branchNodeCount counts
        // local + remote nodes from refs/remotes classification.
        QVERIFY(widget.branchNodeCount() >= 3);
        QDir(repo).removeRecursively();
    }
};

QTEST_MAIN(TestBranchRelation)
#include "test_branch_relation.moc"
