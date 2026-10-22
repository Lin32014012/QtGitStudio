// v4-P6-6: shared test-settings isolation for every test that touches
// MainWindow / ProjectController. ProjectController persists recent
// projects through QSettings(IniFormat, UserScope, "QtGitStudio",
// "QtGitStudio"); without isolation tests would write the PRODUCTION
// ini file and pollute the Welcome recent list with temp repos.
// Call isolateTestSettings() once in initTestCase().
#ifndef TEST_SETTINGS_ISOLATION_H
#define TEST_SETTINGS_ISOLATION_H

#include <QSettings>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDir>

inline void isolateTestSettings() {
    QStandardPaths::setTestModeEnabled(true);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       QDir::tempPath() + QStringLiteral("/qgs-test-settings"));
    QCoreApplication::setOrganizationName(QStringLiteral("QtGitStudioTest"));
    QCoreApplication::setApplicationName(QStringLiteral("QtGitStudioTest"));
}

#endif // TEST_SETTINGS_ISOLATION_H
