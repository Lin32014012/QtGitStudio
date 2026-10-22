// v4-P6-7 QSettings Legacy Migration tests:
//   1. legacy has data + new store empty  -> migration succeeds
//   2. new store already has data         -> not overwritten
//   3. invalid (missing) paths filtered
//   4. duplicate paths de-duplicated, order preserved
//   5. second run does not migrate again (marker)
//   6. test configuration isolated from production (fake legacy store
//      injected; the real registry is never read/written by the test)
#include <QtTest>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QDir>
#include <QFileInfo>
#include "ProjectController.h"

class TestLegacyMigration : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;

    QString makeDir(const QString &name) {
        const QString p = m_dir.path() + "/" + name;
        QDir().mkpath(p);
        return p;
    }

    // Per-test store files: QSettings::setPath redirects the org-based
    // path into the test dir, but tests must not share one file (the
    // migration marker would leak across test functions).
    QSettings *newStore(const QString &tag) {
        return new QSettings(m_dir.path() + "/settings/" + tag + "-new.ini",
                             QSettings::IniFormat);
    }

    QSettings *fakeLegacy(const QString &tag, const QStringList &projects) {
        auto *legacy = new QSettings(
            m_dir.path() + "/settings/" + tag + "-legacy.ini",
            QSettings::IniFormat);
        legacy->setValue(QStringLiteral("recentProjects"), projects);
        legacy->sync();
        return legacy;
    }

private slots:
    void initTestCase() {
        // v4-P6-7: same isolation as all other tests.
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_dir.path() + "/settings");
        QCoreApplication::setOrganizationName(QStringLiteral("QtGitStudioTest"));
        QCoreApplication::setApplicationName(QStringLiteral("QtGitStudioTest"));
        QVERIFY(m_dir.isValid());
    }

    void migrateWhenNewStoreEmpty() {
        const QString projA = makeDir("proj-a");
        const QString projB = makeDir("proj-b");
        QScopedPointer<QSettings> store(newStore("t1"));
        QScopedPointer<QSettings> legacy(fakeLegacy("t1", {projA, projB}));

        ProjectController controller(store.data());
        controller.migrateLegacyRecentProjects(legacy.data());

        const QStringList migrated = store->value("recentProjects").toStringList();
        QCOMPARE(migrated, QStringList({projA, projB}));
        QCOMPARE(store->value("migration/version").toInt(), 1);
    }

    void notOverwriteExistingNewData() {
        const QString projA = makeDir("proj-a");
        const QString projB = makeDir("proj-b");
        QScopedPointer<QSettings> store(newStore("t2"));
        store->setValue(QStringLiteral("recentProjects"),
                        QStringList({projA}));
        QScopedPointer<QSettings> legacy(fakeLegacy("t2", {projA, projB}));

        ProjectController controller(store.data());
        controller.migrateLegacyRecentProjects(legacy.data());

        // New-store data wins; legacy list must NOT be merged in.
        const QStringList after = store->value("recentProjects").toStringList();
        QCOMPARE(after, QStringList({projA}));
    }

    void filterMissingPaths() {
        const QString projA = makeDir("proj-a");
        const QString missing = m_dir.path() + "/deleted-project";
        QScopedPointer<QSettings> store(newStore("t3"));
        QScopedPointer<QSettings> legacy(fakeLegacy("t3", {projA, missing}));

        ProjectController controller(store.data());
        controller.migrateLegacyRecentProjects(legacy.data());

        const QStringList migrated = store->value("recentProjects").toStringList();
        QCOMPARE(migrated, QStringList({projA}));
    }

    void deDuplicatesKeepOrder() {
        const QString projA = makeDir("proj-a");
        const QString projB = makeDir("proj-b");
        QScopedPointer<QSettings> store(newStore("t4"));
        QScopedPointer<QSettings> legacy(
            fakeLegacy("t4", {projA, projB, projA, projB}));

        ProjectController controller(store.data());
        controller.migrateLegacyRecentProjects(legacy.data());

        const QStringList migrated = store->value("recentProjects").toStringList();
        QCOMPARE(migrated, QStringList({projA, projB}));
    }

    void secondRunDoesNotMigrateAgain() {
        const QString projA = makeDir("proj-a");
        QScopedPointer<QSettings> store(newStore("t5"));
        QScopedPointer<QSettings> legacy(fakeLegacy("t5", {projA}));

        ProjectController controller(store.data());
        controller.migrateLegacyRecentProjects(legacy.data());
        QCOMPARE(store->value("recentProjects").toStringList(),
                 QStringList({projA}));

        // Second run: marker exists -> no-op, nothing changes even if
        // the legacy store changes.
        legacy->setValue(QStringLiteral("recentProjects"),
                         QStringList({projA, projA}));
        legacy->sync();
        controller.migrateLegacyRecentProjects(legacy.data());
        QCOMPARE(store->value("recentProjects").toStringList(),
                 QStringList({projA}));
        QCOMPARE(store->value("migration/version").toInt(), 1);
    }

    void isolatedFromProduction() {
        const QString uniqueProj = makeDir("unique-legacy-proj");
        QScopedPointer<QSettings> store(newStore("t6"));
        QScopedPointer<QSettings> legacy(fakeLegacy("t6", {uniqueProj}));

        ProjectController controller(store.data());
        controller.migrateLegacyRecentProjects(legacy.data());

        // The migrated data came exactly from the injected fake store
        // (a unique path that cannot exist in the real registry).
        QCOMPARE(store->value("recentProjects").toStringList(),
                 QStringList({uniqueProj}));

        // The production registry store must not contain the test path.
        QSettings production(QSettings::NativeFormat, QSettings::UserScope,
                             QStringLiteral("QtGitStudio"),
                             QStringLiteral("QtGitStudio"));
        const QStringList prodList =
            production.value("recentProjects").toStringList();
        QVERIFY(!prodList.contains(uniqueProj));
    }
};

QTEST_MAIN(TestLegacyMigration)
#include "test_legacy_migration.moc"
