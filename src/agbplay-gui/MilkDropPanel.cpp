#include "MilkDropPanel.hpp"
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStyle>
#include <QStandardPaths>
#include <QDir>
#include <QSaveFile>
#include <QCloseEvent>
#include <QScreen>
#include <algorithm>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QCoreApplication>
#endif

namespace {
class PresentationWindow : public QWidget {
public:
    std::function<void()> leave;
    void closeEvent(QCloseEvent *event) override { event->accept(); if (leave) leave(); }
};
}
MilkDropPanel::MilkDropPanel(QWidget *parent) : QWidget(parent), surface(new MilkDropWindow),
    viewport(QWidget::createWindowContainer(surface, this))
{
    setObjectName("milkDropPanel");
    viewport->setObjectName("milkDropViewport");
    viewport->setMinimumSize(160, 100);
    layout.setContentsMargins(0, 0, 0, 0);
    for (const auto &name : {"Aurora", "Orbitas", "Espectro"}) {
        QFile file(QString(":/milkdrop/%1.milk").arg(name));
        if (file.open(QIODevice::ReadOnly)) { presets.append(file.readAll()); names.append(name); }
    }
    QDir collection(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/milkdrop");
    for (const auto &entry : collection.entryInfoList({"*.milk"}, QDir::Files, QDir::Name)) {
        QFile file(entry.filePath());
        if (file.size() <= 256 * 1024 && file.open(QIODevice::ReadOnly)) {
            presets.append(file.readAll()); names.append(entry.completeBaseName());
        }
    }
    layout.addWidget(viewport, 1);
    layout.addWidget(MakeControls(false));
    auto *statistics = new QLabel(this);
    statistics->setObjectName("milkDropStatistics");
    statistics->setWordWrap(true);
    layout.addWidget(statistics);
    connect(surface, &MilkDropWindow::statisticsChanged, statistics, &QLabel::setText);
    connect(surface, &MilkDropWindow::runningChanged, this, [this](bool running) { if (enablePcm) enablePcm(running); });
    selected = std::clamp(QSettings().value("milkdrop/preset", 0).toInt(), 0, std::max(0, int(presets.size()) - 1));
    SelectPreset(selected);
}
MilkDropPanel::~MilkDropPanel()
{
    PrepareForShutdown();
    // Deferred deletes may not run after QApplication::exec() has returned.
    for (const auto &retired : retiredPresentations) if (retired) delete retired.data();
    for (auto *selector : selectors) disconnect(selector, nullptr, this, nullptr);
    delete viewport;
}
void MilkDropPanel::PrepareForShutdown()
{
    if (closing) return;
    closing = true;
    surface->SetEnabled(false);
    surface->SetSource({});
    if (enablePcm) enablePcm(false);
    enablePcm = {};
    SetFullscreen(false);
}
void MilkDropPanel::SetSource(MilkDropWindow::Source source, std::function<void(bool)> enable)
{
    if (enablePcm) enablePcm(false);
    surface->SetSource(std::move(source));
    enablePcm = std::move(enable);
    if (enablePcm) enablePcm(surface->Diagnostics()["running"].toBool());
}
QWidget *MilkDropPanel::MakeControls(bool full)
{
    auto *row = new QWidget;
    auto *buttons = new QHBoxLayout(row);
    buttons->setContentsMargins(0, 0, 0, 0);
    auto *previous = new QPushButton("‹", row);
    auto *next = new QPushButton("›", row);
    auto *selector = new QComboBox(row);
    selector->addItems(names);
    selector->setCurrentIndex(selected);
    selector->setMinimumWidth(70);
    selectors.append(selector);
    connect(selector, &QObject::destroyed, this, [this, selector] { selectors.removeAll(selector); });
    auto *toggle = new QPushButton(full ? "Salir" : "", row);
    if (!full) {
        const auto icon = style()->standardIcon(QStyle::SP_TitleBarMaxButton);
        if (icon.isNull()) toggle->setText("Full"); else toggle->setIcon(icon);
    }
    toggle->setAccessibleName(full ? "Salir de pantalla completa" : "Pantalla completa");
    toggle->setToolTip(toggle->accessibleName());
    toggle->setObjectName(full ? "milkDropExitFullscreen" : "milkDropFullscreen");
    previous->setAccessibleName("Preset anterior"); next->setAccessibleName("Preset siguiente");
    buttons->addWidget(previous); buttons->addWidget(selector, 1); buttons->addWidget(next); buttons->addWidget(toggle);
    for (auto *button : {previous, next, toggle}) { button->setMinimumSize(48, 48); button->setMaximumWidth(64); }
    selector->setMinimumHeight(48);
    connect(previous, &QPushButton::clicked, this, [this] { SelectPreset(selected - 1); });
    connect(next, &QPushButton::clicked, this, [this] { SelectPreset(selected + 1); });
    connect(selector, &QComboBox::currentIndexChanged, this, &MilkDropPanel::SelectPreset);
    connect(toggle, &QPushButton::clicked, this, [this, full] { SetFullscreen(!full); });
    if (!full) {
        auto *import = new QPushButton("+", row);
        import->setAccessibleName("Importar preset MilkDrop"); import->setMinimumHeight(48);
        buttons->addWidget(import);
        connect(import, &QPushButton::clicked, this, &MilkDropPanel::ImportPreset);
    }
    return row;
}
void MilkDropPanel::SelectPreset(int index)
{
    if (presets.isEmpty()) return;
    selected = (index % int(presets.size()) + int(presets.size())) % int(presets.size());
    surface->SetPreset(presets[selected]);
    for (auto *selector : selectors) { QSignalBlocker block(selector); selector->setCurrentIndex(selected); }
    QSettings().setValue("milkdrop/preset", selected);
}
void MilkDropPanel::ImportPreset()
{
    const auto path = QFileDialog::getOpenFileName(this, "Importar preset MilkDrop", {}, "MilkDrop (*.milk)");
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;
    const auto data = file.read(256 * 1024 + 1);
    if (data.size() > 256 * 1024 || !data.contains("[preset")) return;
    const QString name = QString("Importado %1").arg(presets.size() + 1);
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/milkdrop";
    QDir().mkpath(directory);
    QSaveFile copy(directory + "/" + name + ".milk");
    if (!copy.open(QIODevice::WriteOnly) || copy.write(data) != data.size() || !copy.commit()) return;
    presets.append(data); names.append(name);
    for (auto *selector : selectors) { QSignalBlocker block(selector); selector->addItem(name); }
    SelectPreset(int(presets.size()) - 1);
}
void MilkDropPanel::SetOrientation(bool landscape)
{
#ifdef Q_OS_ANDROID
    // Java marshals this to the Activity main thread and restores its exact
    // previous requestedOrientation, including UNSPECIFIED/sensor settings.
    const auto activity = QNativeInterface::QAndroidApplication::context();
    QJniObject::callStaticMethod<void>("org/agbplay/android/MilkDropOrientation", "setLandscape",
        "(Landroid/app/Activity;Z)V", activity.object(), jboolean(landscape));
#else
    Q_UNUSED(landscape);
#endif
}
void MilkDropPanel::SetFullscreen(bool value)
{
    if (closing && value) return;
    if (value == bool(fullscreen)) return;
    surface->SetEnabled(false);
    if (value) {
        previousWindow = window();
        previousVisible = previousWindow->isVisible();
        auto *host = new PresentationWindow;
        fullscreen.reset(host);
        host->setObjectName("milkDropPresentation");
        host->setWindowTitle("agbplay · MilkDrop");
        host->leave = [this] { SetFullscreen(false); };
        fullscreenLayout = new QVBoxLayout(host);
        fullscreenLayout->setContentsMargins(0, 0, 0, 0);
        fullscreenLayout->addWidget(viewport, 1);
        fullscreenLayout->addWidget(MakeControls(true));
        new QShortcut(QKeySequence(Qt::Key_Escape), host, [this] { SetFullscreen(false); });
        new QShortcut(QKeySequence(Qt::Key_Back), host, [this] { SetFullscreen(false); });
        SetOrientation(true);
        // Also size explicitly: window systems without a WM may ignore the
        // fullscreen state hint. Android resizes again when landscape arrives.
        host->setGeometry(screen()->geometry());
        host->showFullScreen();
        // Keep one QWidget backing-store window exposed. Leaving the original
        // window painting behind the presentation wastes GPU and can race its
        // Android surface teardown when the screen is switched off.
        if (previousVisible) previousWindow->hide();
    } else {
        if (!closing && previousVisible && previousWindow) previousWindow->show();
        fullscreen->hide();
        fullscreenLayout->removeWidget(viewport);
        viewport->setParent(this);
        layout.insertWidget(0, viewport, 1);
        viewport->show();
        // Avoid deleting a QWidget while its close/key event is on the stack.
        auto *retired = fullscreen.release();
        retiredPresentations.removeIf([](const auto &entry) { return entry.isNull(); });
        retiredPresentations.append(retired);
        retired->deleteLater();
        fullscreenLayout = nullptr;
        SetOrientation(false);
        if (!closing) { window()->raise(); window()->activateWindow(); }
    }
    UpdateEnabled();
}
void MilkDropPanel::UpdateEnabled() { surface->SetEnabled(!closing && (isVisible() || bool(fullscreen))); }
void MilkDropPanel::showEvent(QShowEvent *event) { QWidget::showEvent(event); UpdateEnabled(); }
void MilkDropPanel::hideEvent(QHideEvent *event) { QWidget::hideEvent(event); UpdateEnabled(); }
QJsonObject MilkDropPanel::Diagnostics() const
{
    auto result = surface->Diagnostics();
    result["fullscreen"] = bool(fullscreen); result["preset"] = selected;
    return result;
}
