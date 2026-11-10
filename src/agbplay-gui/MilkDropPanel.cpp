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
#include <QJniEnvironment>
#include <QMutex>
#include <QMutexLocker>
#include <QCoreApplication>
#endif

namespace {
#ifdef Q_OS_ANDROID
QMutex backMutex;
MilkDropPanel *backPanel = nullptr;
void AndroidBack(JNIEnv *, jclass)
{
    QMutexLocker lock(&backMutex);
    if (backPanel) QMetaObject::invokeMethod(backPanel, [panel = backPanel] {
        panel->SetFullscreen(false);
    }, Qt::QueuedConnection);
}
void RegisterAndroidBack()
{
    QJniEnvironment env;
    const auto clazz = env.findClass("org/agbplay/android/MilkDropOrientation");
    JNINativeMethod method[]{
        {const_cast<char *>("nativeExit"), const_cast<char *>("()V"), reinterpret_cast<void *>(AndroidBack)}
    };
    if (!clazz || env->RegisterNatives(clazz, method, 1) != JNI_OK) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        qWarning("MilkDrop: could not register Android Back handler");
    }
}
#endif
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
#ifdef Q_OS_ANDROID
    RegisterAndroidBack();
#endif
    viewport->setObjectName("milkDropViewport");
    viewport->setMinimumSize(160, 100);
    layout.setContentsMargins(0, 0, 0, 0);
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/milkdrop";
    QDir().mkpath(directory);
    for (const auto &name : {"Aurora", "Orbitas", "Espectro"}) {
        QFile resource(QString(":/milkdrop/%1.milk").arg(name));
        const QString path = directory + "/" + name + ".milk";
        if (!QFile::exists(path) && resource.open(QIODevice::ReadOnly)) {
            QSaveFile copy(path);
            if (copy.open(QIODevice::WriteOnly)) { copy.write(resource.readAll()); copy.commit(); }
        }
        if (QFile::exists(path)) { presets.append(path); names.append(name); }
    }
    QDir collection(directory);
    for (const auto &entry : collection.entryInfoList({"*.milk"}, QDir::Files, QDir::Name)) {
        if (!presets.contains(entry.filePath()) && entry.size() <= 256 * 1024) {
            presets.append(entry.filePath()); names.append(entry.completeBaseName());
        }
    }
    if (presets.isEmpty()) {
        QFile resource(":/milkdrop/Aurora.milk");
        if (resource.open(QIODevice::ReadOnly)) surface->SetPreset(resource.readAll());
    } else surface->SetFiles(presets, {});
    layout.addWidget(viewport, 1);
    layout.addWidget(MakeControls(false));
    auto *statistics = new QLabel(this);
    statistics->setObjectName("milkDropStatistics");
    statistics->setWordWrap(true);
    layout.addWidget(statistics);
    connect(surface, &MilkDropWindow::statisticsChanged, statistics, &QLabel::setText);
    connect(surface, &MilkDropWindow::statisticsChanged, this, &MilkDropPanel::diagnostic);
    connect(surface, &MilkDropWindow::presetChanged, this, [this](int index) {
        if (index < 0 || index >= presets.size()) return;
        selected = index;
        for (auto *selector : selectors) { QSignalBlocker block(selector); selector->setCurrentIndex(index); }
    });
    auto *libraryStatus = new QLabel(this);
    libraryStatus->setWordWrap(true);
    layout.addWidget(libraryStatus);
    library = new MilkDropLibrary(this);
    indexTimer.setInterval(0);
    connect(&indexTimer, &QTimer::timeout, this, [this] {
        bool incomplete = false;
        for (auto *selector : selectors) {
            const auto start = selector->count();
            if (start >= names.size()) continue;
            const QSignalBlocker blocker(selector);
            selector->addItems(names.mid(start, 128));
            if (selected < selector->count()) selector->setCurrentIndex(selected);
            incomplete |= selector->count() < names.size();
        }
        if (!incomplete) indexTimer.stop();
    });
    connect(library, &MilkDropLibrary::status, this, [this, libraryStatus](const QString &message) {
        collectionStatus = message;
        libraryStatus->setText(message);
        qInfo().noquote() << "MilkDrop collection:" << message;
        emit diagnostic(message);
    });
    connect(library, &MilkDropLibrary::ready, this, [this](const QStringList &files, const QString &textureDir) {
        textures = textureDir;
        presets.append(files);
        for (const auto &path : files) names.append(QFileInfo(path).completeBaseName());
        if (isVisible()) indexTimer.start();
        surface->SetFiles(presets, textures);
    });
#ifdef Q_OS_ANDROID
    library->Start(directory);
#else
    if (qEnvironmentVariableIsSet("AGBPLAY_MILKDROP_DOWNLOAD")) library->Start(directory);
#endif
    connect(surface, &MilkDropWindow::runningChanged, this, [this](bool running) { if (enablePcm) enablePcm(running); });
    selected = std::clamp(QSettings().value("milkdrop/preset", 0).toInt(), 0, std::max(0, int(presets.size()) - 1));
    SelectPreset(selected);
}
MilkDropPanel::~MilkDropPanel()
{
    PrepareForShutdown();
#ifdef Q_OS_ANDROID
    { QMutexLocker lock(&backMutex); if (backPanel == this) backPanel = nullptr; }
#endif
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
    connect(previous, &QPushButton::clicked, this, [this] { surface->Navigate(true); });
    connect(next, &QPushButton::clicked, this, [this] { surface->Navigate(false); });
    connect(selector, &QComboBox::currentIndexChanged, this, &MilkDropPanel::SelectPreset);
    connect(toggle, &QPushButton::clicked, this, [this, full] { SetFullscreen(!full); });
    if (!full) {
        auto *import = new QPushButton("+", row);
        import->setAccessibleName("Importar preset MilkDrop"); import->setMinimumHeight(48);
        buttons->addWidget(import);
        auto *unlimited = new QPushButton("Sin límite", row);
        unlimited->setCheckable(true); unlimited->setMinimumHeight(48);
        unlimited->setToolTip("Desactivar el limitador adaptativo de FPS");
        buttons->addWidget(unlimited);
        connect(unlimited, &QPushButton::toggled, this, [this](bool enabled) { surface->SetFrameLimit(enabled ? -1 : 0); });
        connect(import, &QPushButton::clicked, this, &MilkDropPanel::ImportPreset);
    }
    return row;
}
void MilkDropPanel::SelectPreset(int index)
{
    if (presets.isEmpty()) return;
    selected = (index % int(presets.size()) + int(presets.size())) % int(presets.size());
    surface->SelectFile(selected);
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
    presets.append(directory + "/" + name + ".milk"); names.append(name);
    surface->SetFiles(presets, textures);
    for (auto *selector : selectors) {
        if (selector->count() == names.size() - 1) { QSignalBlocker block(selector); selector->addItem(name); }
        else if (isVisible()) indexTimer.start();
    }
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
#ifdef Q_OS_ANDROID
        { QMutexLocker lock(&backMutex); backPanel = this; }
#endif
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
        fullscreenLayout->setSpacing(0);
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
#ifdef Q_OS_ANDROID
        { QMutexLocker lock(&backMutex); if (backPanel == this) backPanel = nullptr; }
#endif
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
void MilkDropPanel::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    for (const auto *selector : selectors) if (selector->count() < names.size()) { indexTimer.start(); break; }
    UpdateEnabled();
}
void MilkDropPanel::hideEvent(QHideEvent *event) {
    QWidget::hideEvent(event);
    indexTimer.stop();
    UpdateEnabled();
}
QJsonObject MilkDropPanel::Diagnostics() const
{
    auto result = surface->Diagnostics();
    result["fullscreen"] = bool(fullscreen); result["preset"] = selected;
    result["collectionStatus"] = collectionStatus;
    result["indexedPresets"] = int(presets.size());
    result["texturesAvailable"] = !textures.isEmpty();
    return result;
}
