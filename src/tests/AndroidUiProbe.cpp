// Emulator-only entry point. Uses the real MainWindow and Android input path;
// no ROM, synthesis or audio device is needed for this UI regression.
#include "MainWindow.hpp"
#include <QApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QWindow>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("agbplay");
    QCoreApplication::setApplicationName("agbplay-ui-probe");
    MainWindow window(nullptr, true);
    QObject::disconnect(&window, &MainWindow::OpenRomRequested, nullptr, nullptr);
    int openRequests = 0, menuRequests = 0;
    QObject::connect(&window, &MainWindow::OpenRomRequested, &app, [&] { ++openRequests; });
    auto *open = window.findChild<QPushButton *>("openRomButton");
    auto *menu = window.findChild<QToolButton *>("touchMenuButton");
    QObject::connect(menu->menu(), &QMenu::aboutToShow, &app, [&] { ++menuRequests; });
    window.show();
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(directory);
    QTimer report;
    QObject::connect(&report, &QTimer::timeout, &app, [&] {
        const auto describe = [&](QWidget *widget) {
            const QPoint center = widget->mapToGlobal(widget->rect().center());
            const qreal scale = window.devicePixelRatioF();
            bool contained = true;
            for (auto *parent = widget->parentWidget(); parent; parent = parent->parentWidget())
                contained &= parent->rect().contains(QRect(widget->mapTo(parent, QPoint{}), widget->size()));
            return QJsonObject{{"visible", widget->isVisible()}, {"contained", contained},
                               {"x", qRound(center.x() * scale)}, {"y", qRound(center.y() * scale)},
                               {"width", widget->width()}, {"height", widget->height()}};
        };
        const QJsonObject state{{"qt", qVersion()}, {"open", describe(open)}, {"menu", describe(menu)},
                                {"openRequests", openRequests}, {"menuRequests", menuRequests},
                                {"windowWidth", window.width()}, {"windowHeight", window.height()}};
        QSaveFile file(directory + "/ui-probe.json");
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(state).toJson());
            file.commit();
        }
    });
    report.start(100);
    return app.exec();
}
