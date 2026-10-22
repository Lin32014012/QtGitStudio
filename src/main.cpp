#include <QApplication>
#include <QIcon>
#include <QSize>
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("QtGitStudio"));
    QApplication::setApplicationVersion(QStringLiteral(QTGITSTUDIO_VERSION));
    QIcon applicationIcon;
    for (const int size : {16, 24, 32, 48, 64, 128, 256}) {
        applicationIcon.addFile(
            QString(":/icons/QtGitStudio-%1.png").arg(size),
            QSize(size, size));
    }
    QApplication::setWindowIcon(applicationIcon);
    QApplication::setQuitOnLastWindowClosed(true);

    MainWindow window;
    window.setWindowIcon(applicationIcon);
    window.show();

    return app.exec();
}
