#include "MainWindow.hpp"
#include "TouchDialogFilter.hpp"

#ifndef AGBPLAY_QT_AUDIO
#include <portaudiocpp/AutoSystem.hxx>
#endif
#include <QMessageBox>
#include <QApplication>
#include <QWidget>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName("agbplay");
    QCoreApplication::setApplicationName("agbplay");
#ifndef AGBPLAY_QT_AUDIO
    portaudio::AutoSystem paSystem;
#endif

#ifdef Q_OS_ANDROID
    TouchDialogFilter touchDialogs;
    app.installEventFilter(&touchDialogs);
#endif
    MainWindow mainWindow;
    mainWindow.show();

    return app.exec();
}
