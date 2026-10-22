// Emulator-only entry point. Uses the real MainWindow and Android input path;
// no ROM, synthesis or audio device is needed for this UI regression.
#include "MainWindow.hpp"
#include <QApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJniObject>
#include <QMenu>
#include <QMenuBar>
#include <QCoreApplication>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QWindow>
#include <QAction>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("agbplay");
    QCoreApplication::setApplicationName("agbplay-ui-probe");
    MainWindow window(nullptr, true);
    QObject::disconnect(&window, &MainWindow::OpenRomRequested, nullptr, nullptr);
    QObject::disconnect(&window, &MainWindow::OpenGsfFolderRequested, nullptr, nullptr);
    int openRequests = 0, menuRequests = 0, folderRequests = 0;
    bool actionBarChecked = false, actionBarPresent = false;
    QTimer::singleShot(500, &app, [&] {
        QNativeInterface::QAndroidApplication::runOnAndroidMainThread([] {
            QJniObject activity = QNativeInterface::QAndroidApplication::context();
            return QVariant(activity.callObjectMethod("getActionBar", "()Landroid/app/ActionBar;").isValid());
        }).then(&app, [&](const QVariant &present) {
            actionBarPresent = present.toBool();
            actionBarChecked = true;
        });
    });
    QObject::connect(&window, &MainWindow::OpenRomRequested, &app, [&] { ++openRequests; });
    QObject::connect(&window, &MainWindow::OpenGsfFolderRequested, &app, [&] { ++folderRequests; });
    auto *open = window.findChild<QPushButton *>("openRomButton");
    auto *menu = window.findChild<QToolButton *>("touchMenuButton");
    auto *folder = window.findChild<QAction *>("openGsfFolderAction");
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
        const auto rect = menu->menu()->actionGeometry(folder);
        const auto center = menu->menu()->mapToGlobal(rect.center());
        const auto scale = window.devicePixelRatioF();
        const QJsonObject folderState{{"visible", menu->menu()->isVisible() && folder->isVisible()},
                                      {"x", qRound(center.x() * scale)}, {"y", qRound(center.y() * scale)},
                                      {"height", rect.height()}};
        const QJsonObject state{{"qt", qVersion()}, {"open", describe(open)}, {"menu", describe(menu)},
                                {"applicationState", int(app.applicationState())},
                                {"updatesEnabled", window.updatesEnabled()}, {"enabled", window.isEnabled()},
                                {"folder", folderState}, {"folderRequests", folderRequests},
                                {"nativeMenuBar", window.menuBar()->isNativeMenuBar()},
                                {"actionBarChecked", actionBarChecked}, {"actionBarPresent", actionBarPresent},
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
