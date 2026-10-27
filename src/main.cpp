#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QSize>
#include <QStringList>
#include <QTimer>
#include "MainWindow.h"

namespace {

// v4-P6-26: load the single global theme from the Qt resource bundle.
// Missing resource is not fatal - the app falls back to platform styling.
void applyTheme(QApplication &app) {
    QFile qss(QStringLiteral(":/theme/theme.qss"));
    if (qss.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));
        qss.close();
    }
}

}  // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("QtGitStudio"));
    QApplication::setApplicationVersion(QStringLiteral(QTGITSTUDIO_VERSION));
    applyTheme(app);
    QIcon applicationIcon;
    for (const int size : {16, 24, 32, 48, 64, 128, 256}) {
        applicationIcon.addFile(
            QString(":/icons/QtGitStudio-%1.png").arg(size),
            QSize(size, size));
    }
    QApplication::setWindowIcon(applicationIcon);
    QApplication::setQuitOnLastWindowClosed(true);

    // Optional command line: `QtGitStudio --open <repositoryPath>` opens a
    // repository immediately instead of waiting for the picker. Without the
    // flag the app behaves exactly as before (welcome page first).
    QString openPath;
    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) == QStringLiteral("--open") && i + 1 < args.size()) {
            openPath = args.at(i + 1);
            ++i;
        }
    }

    MainWindow window;
    window.setWindowIcon(applicationIcon);
    window.show();

    if (!openPath.isEmpty()) {
        QTimer::singleShot(0, &window, [&window, openPath]() {
            QMetaObject::invokeMethod(&window, "onProjectSelected",
                                      Q_ARG(QString, openPath));
        });
    }

    // Documentation/screenshot hook: QTGITSTUDIO_AUTO_SLOT=<slotName> invokes
    // a MainWindow slot once the repository has loaded (used to capture the
    // commit-graph viewer and the conflict resolver for README images). No
    // effect when the variable is unset.
    const QByteArray autoSlot = qgetenv("QTGITSTUDIO_AUTO_SLOT");
    if (!autoSlot.isEmpty()) {
        QTimer::singleShot(3000, &window, [&window, autoSlot]() {
            QMetaObject::invokeMethod(&window, autoSlot.constData(),
                                      Qt::DirectConnection);
        });
    }

    return app.exec();
}
