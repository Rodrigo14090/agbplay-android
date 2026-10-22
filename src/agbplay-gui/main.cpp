#include "MainWindow.hpp"

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

    MainWindow mainWindow;
    mainWindow.show();

    return app.exec();
}
