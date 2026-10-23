#include "MainWindow.hpp"
#include "AndroidPlayback.hpp"
#include "DocumentAccess.hpp"
#include "TouchPlayerLayout.hpp"
#include <QToolButton>
#include <QApplication>
#include <QScroller>
#include <QProgressDialog>
#include <QSignalBlocker>
#include <QScopedValueRollback>

#include "AboutWindow.hpp"
#include "Debug.hpp"
#include "FileReader.hpp"
#include "Gsf.hpp"
#include "PlaybackEngine.hpp"
#include "ProfileManager.hpp"
#include "ProfileSettingsWindow.hpp"
#include "Rom.hpp"
#include "SelectProfileDialog.hpp"
#include "Settings.hpp"
#include "OS.hpp"
#include <QFile>
#include <QDir>
#include "SettingsWindow.hpp"
#include "SoundExporter.hpp"

#include <algorithm>
#include <array>
#include <fmt/format.h>
#include <fstream>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QShortcut>
#include <QStatusBar>
#include <QTextEdit>
#include <QTextDocument>
#include <QToolBar>
#include <thread>
#include <tuple>
#include <utility>

MainWindow::MainWindow(QWidget *parent, bool touchInterface) : QMainWindow(parent)
{
    // Android's native menu integration can show an Activity ActionBar even
    // after the QWidget menu bar is hidden. Keep all touch menus in Qt from
    // the outset, before creating any platform menu handles.
    if (touchInterface)
        menuBar()->setNativeMenuBar(false);
    SetupMenuBar();
    SetupToolBar();
    SetupWidgets();
    SetupStatusBar();
    if (touchInterface)
        SetupTouchInterface();
#ifdef Q_OS_ANDROID
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        // Keep the Qt control plane alive, but never paint a suspended surface.
        for (auto *window : QApplication::topLevelWidgets())
            window->setUpdatesEnabled(state == Qt::ApplicationActive);
        statusUpdateTimer.setInterval(state == Qt::ApplicationActive ? 16 : 250);
        if (state == Qt::ApplicationActive) QTimer::singleShot(0, this, [this] {
            auto tasks = std::exchange(deferredUiTasks, {});
            for (auto &task : tasks) RunWhenActive(std::move(task));
        });
        if (state != Qt::ApplicationActive && settings && settings->dirty)
            settings->Save();
    });
#endif

    pm = std::make_unique<ProfileManager>();
    pm->LoadProfiles();

    settings = std::make_unique<Settings>();
    settings->Load();

    volumeSlider.setValue(static_cast<int>(settings->playbackVolume));

    // Because we programmatically modify the slider above, the dirty flag is set.
    // Reset this, to avoid an unnecessary save.
    settings->dirty = false;

#ifdef Q_OS_ANDROID
    logWidget.document()->setMaximumBlockCount(2000);
    androidPlayback = std::make_unique<AndroidPlayback>(this);
    connect(androidPlayback.get(), &AndroidPlayback::command, this, &MainWindow::AndroidPlaybackCommand);
    connect(androidPlayback.get(), &AndroidPlayback::error, this, [this](const QString &message) {
        AndroidPlaybackCommand(AndroidPlayback::Stop);
        LogAppend("Android playback service: " + message.toStdString());
        statusBar()->showMessage(message);
    });
#endif

    connect(&statusUpdateTimer, &QTimer::timeout, this, &MainWindow::StatusUpdate);
    statusUpdateTimer.setTimerType(Qt::PreciseTimer);
    statusUpdateTimer.setInterval(16);
    statusUpdateTimer.start();

    Debug::set_callback(&MainWindow::LogCallback, static_cast<void *>(this));

    connect(&songlistWidget.addRemoveButton, &QAbstractButton::clicked, [this](bool) { PlaylistAdd(); });
    connect(&playlistWidget.addRemoveButton, &QAbstractButton::clicked, [this](bool) { PlaylistRemove(); });

    connect(&songlistWidget, &SonglistWidget::ContextMenuActionAdd, [this]() { PlaylistAdd(); });
    connect(&playlistWidget, &SonglistWidget::ContextMenuActionRemove, [this]() { PlaylistRemove(); });

    connect(&playlistWidget, &SonglistWidget::ContentChanged, [this]() {
        if (!profile)
            return;
        profile->dirty = true;
        saveButton.setEnabled(true);
        saveProfileAction->setEnabled(true);
    });

    connect(&statusWidget, &StatusWidget::audibilityChanged, this, &MainWindow::UpdateMute);

    new QShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_G), this, [this]() { JumpSong(); });

    setWindowTitle("agbplay");
    setWindowIcon(QIcon(":/icons/main-logo.ico"));

    /* Use a signal instead of QMetaObject::invokeMethod for pre Qt 6.7 compatibility. */
    connect(this, &MainWindow::LogMessageReceived, this, &MainWindow::LogAppend, Qt::QueuedConnection);
}

MainWindow::~MainWindow()
{
#ifdef Q_OS_ANDROID
    androidPlayback.reset(); // detach JNI before native player/UI destruction
#endif
    if (importCancelled) importCancelled->store(true);
    if (importWorker.joinable()) importWorker.join();
    // Release gesture registrations while their widget targets still exist.
    for (auto *widget : findChildren<QWidget *>())
        if (QScroller::hasScroller(widget))
            QScroller::ungrabGesture(widget);
    playbackEngine.reset();
    if (exportThread && exportThread->joinable())
        exportThread->join();
    Debug::set_callback(nullptr, nullptr);
}

void MainWindow::SetupMenuBar()
{
    /* File */
    QMenu *fileMenu = menuBar()->addMenu("&File");

    openRomAction = fileMenu->addAction("Open ROM/GSF");
    openRomAction->setObjectName("openRomAction");
    openRomAction->setIcon(QIcon(":/icons/open-rom.ico"));
    openRomAction->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_O));
    connect(openRomAction, &QAction::triggered, this, &MainWindow::OpenRomRequested);
    connect(this, &MainWindow::OpenRomRequested, this, &MainWindow::LoadGame);

    openGsfFolderAction = fileMenu->addAction("Abrir carpeta GSF…");
    openGsfFolderAction->setObjectName("openGsfFolderAction");
    openGsfFolderAction->setIcon(QIcon(":/icons/open-rom.ico"));
    connect(openGsfFolderAction, &QAction::triggered, this, &MainWindow::OpenGsfFolderRequested);
    connect(this, &MainWindow::OpenGsfFolderRequested, this, &MainWindow::LoadGsfFolder);

    fileMenu->addSeparator();

    saveProfileAction = fileMenu->addAction("Save Profile");
    saveProfileAction->setIcon(QIcon(":/icons/profile-save.ico"));
    saveProfileAction->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_S));
    saveProfileAction->setEnabled(false);
    connect(saveProfileAction, &QAction::triggered, [this](bool) { SaveProfile(); });

    exportSongsAction = fileMenu->addAction("Export Songs by Selection");
    exportSongsAction->setIcon(QIcon(":/icons/export-audio.ico"));
    exportSongsAction->setToolTip("Export songs based on selection.");
    exportSongsAction->setShortcut(QKeySequence(Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_E));
    exportSongsAction->setEnabled(false);
    connect(exportSongsAction, &QAction::triggered, [this](bool) { ExportAudio(false, false, false); });

    exportStemsAction = fileMenu->addAction("Export Stems by Selection");
    exportStemsAction->setIcon(QIcon(":/icons/export-audio.ico"));
    exportStemsAction->setToolTip("Export songs based on selection. Tracks are stored to separate stem files.");
    exportStemsAction->setShortcut(QKeySequence(Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_T));
    exportStemsAction->setEnabled(false);
    connect(exportStemsAction, &QAction::triggered, [this](bool) { ExportAudio(false, true, false); });

    quickExportSongAction = fileMenu->addAction("Quick Export Song");
    quickExportSongAction->setIcon(QIcon(":/icons/export-audio.ico"));
    quickExportSongAction->setToolTip("Export single song only.");
    quickExportSongAction->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_E));
    quickExportSongAction->setEnabled(false);
    connect(quickExportSongAction, &QAction::triggered, [this](bool) { ExportAudio(false, false, true); });

    quickExportStemsAction = fileMenu->addAction("Quick Export Stems");
    quickExportStemsAction->setIcon(QIcon(":/icons/export-audio.ico"));
    quickExportStemsAction->setToolTip("Export single song only. Tracks are stored to separate stem files.");
    quickExportStemsAction->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_T));
    quickExportStemsAction->setEnabled(false);
    connect(quickExportStemsAction, &QAction::triggered, [this](bool) { ExportAudio(false, true, true); });

    benchmarkSelectedAction = fileMenu->addAction("Run Benchmark by Selection");
    benchmarkSelectedAction->setIcon(QIcon(":/icons/benchmark.ico"));
    benchmarkSelectedAction->setToolTip("Render selected songs and measure the time. No files are written to disk.");
    benchmarkSelectedAction->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_B));
    benchmarkSelectedAction->setEnabled(false);
    connect(benchmarkSelectedAction, &QAction::triggered, [this](bool) { ExportAudio(true, false, false); });

#ifdef Q_OS_ANDROID
    auto *shareExport = fileMenu->addAction("Share Exported WAV");
    connect(shareExport, &QAction::triggered, this, [this] {
        if (exportBusy) { ExportStillInProgress(); return; }
        QDir directory(QString::fromStdString(OS::GetMusicDirectory().string()));
        const auto files = directory.entryList({"*.wav"}, QDir::Files, QDir::Time);
        if (files.isEmpty()) { MBoxInfo("Export", "No WAV exports yet."); return; }
        bool ok = false;
        const auto file = QInputDialog::getItem(this, "Share Export", "WAV file:", files, 0, false, &ok);
        if (ok && !DocumentAccess::ShareFile(directory.absoluteFilePath(file)))
            MBoxError("Share Export", "Android could not share the selected export.");
    });
#endif
    fileMenu->addSeparator();

    QAction *fileQuit = fileMenu->addAction("Quit");
    fileQuit->setIcon(QIcon(":/icons/exit.ico"));
    fileQuit->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_Q));

    connect(fileQuit, &QAction::triggered, [this](bool) { close(); });
    fileQuit->setMenuRole(QAction::QuitRole);

    /* Edit */
    QMenu *editMenu = menuBar()->addMenu("&Edit");
    QAction *editPreferences = editMenu->addAction("Global Preferences");
    editPreferences->setIcon(QIcon(":/icons/preferences.ico"));
    connect(editPreferences, &QAction::triggered, [this](bool) {
        const auto previousMode = settings->androidAudioOutputMode;
        SettingsWindow w(this, *settings);
        connect(&w, &SettingsWindow::diagnosticWavRequested, this, &MainWindow::LoadDiagnosticWav, Qt::QueuedConnection);
        w.exec();

        ApplyAudioPreferences(previousMode);
    });

    /* Profile */
    QMenu *profileMenu = menuBar()->addMenu("&Profile");
    profileSettings = profileMenu->addAction("Profile Settings");
    profileSettings->setIcon(QIcon(":/icons/profile-settings.ico"));
    profileSettings->setEnabled(false);
    connect(profileSettings, &QAction::triggered, [this](bool) {
        if (!pm)
            return;
        ProfileSettingsWindow w(this, *pm, profile, Rom::Instance().GetROMCode());
        w.exec();

        /* apply relevant changes to current running program. */
        profile->ApplyScanToPlayback();
        UpdateSoundMode();
        playbackEngine->UpdateSoundMode();

        if (profile->dirty) {
            saveButton.setEnabled(true);
            saveProfileAction->setEnabled(true);
        }
    });
    profileMinigsfImport = profileMenu->addAction("Import GSF Playlist");
    profileMinigsfImport->setIcon(QIcon(":/icons/profile-import-minigsf.ico"));
    profileMinigsfImport->setEnabled(false);
    connect(profileMinigsfImport, &QAction::triggered, [this](bool) { ProfileImportGsfPlaylist(); });
    profileMenu->addSeparator();
    QAction *profileDirectory = profileMenu->addAction("Open User Profile Directory");
    profileDirectory->setIcon(QIcon(":/icons/profile-open-folder.ico"));
    connect(profileDirectory, &QAction::triggered, [](bool) {
        const QUrl url = QUrl::fromLocalFile(QString::fromStdWString(ProfileManager::ProfileUserPath().wstring()));
        QDesktopServices::openUrl(url);
    });

    /* Help */
    QMenu *helpMenu = menuBar()->addMenu("&Help");
    QAction *helpSaveLog = helpMenu->addAction("Save Log");
    helpSaveLog->setIcon(QIcon(":/icons/save-log.ico"));
    connect(helpSaveLog, &QAction::triggered, [this](bool) { SaveLog(); });
    helpMenu->addSeparator();
    QAction *helpAboutAction = helpMenu->addAction("About");
    helpAboutAction->setIcon(QIcon(":/icons/about.ico"));
    connect(helpAboutAction, &QAction::triggered, [this](bool) {
        AboutWindow w(this);
        w.exec();
    });
    helpAboutAction->setMenuRole(QAction::AboutRole);
}

void MainWindow::SetupToolBar()
{
    QToolBar *toolBar = addToolBar("Main Toolbar");

    saveButton.setIcon(QIcon(":/icons/save-profile-large.ico"));
    saveButton.setFixedSize(32, 32);
    saveButton.setIconSize(QSize(32, 32));
    saveButton.setEnabled(false);
    toolBar->addWidget(&saveButton);
    connect(&saveButton, &QAbstractButton::clicked, [this](bool) { SaveProfile(); });

    toolBar->addSeparator();

    stopButton.setIcon(QIcon(":/icons/playback-stop.ico"));
    stopButton.setFixedSize(32, 32);
    stopButton.setIconSize(QSize(32, 32));
    stopButton.setCheckable(true);
    stopButton.setChecked(true);
    toolBar->addWidget(&stopButton);
    connect(&stopButton, &QAbstractButton::clicked, [this](bool) { Stop(); });

    playButton.setIcon(QIcon(":/icons/playback-play.ico"));
    playButton.setFixedSize(32, 32);
    playButton.setIconSize(QSize(32, 32));
    playButton.setCheckable(true);
    toolBar->addWidget(&playButton);
    connect(&playButton, &QAbstractButton::clicked, [this](bool) { Play(); });

    pauseButton.setIcon(QIcon(":/icons/playback-pause.ico"));
    pauseButton.setFixedSize(32, 32);
    pauseButton.setIconSize(QSize(32, 32));
    pauseButton.setCheckable(true);
    toolBar->addWidget(&pauseButton);
    connect(&pauseButton, &QAbstractButton::clicked, [this](bool) { Pause(); });

    prevButton.setIcon(QIcon(":/icons/playback-previous.ico"));
    prevButton.setFixedSize(32, 32);
    prevButton.setIconSize(QSize(32, 32));
    toolBar->addWidget(&prevButton);
    connect(&prevButton, &QAbstractButton::clicked, [this](bool) { AdvanceSong(-1); });

    nextButton.setIcon(QIcon(":/icons/playback-next.ico"));
    nextButton.setFixedSize(32, 32);
    nextButton.setIconSize(QSize(32, 32));
    toolBar->addWidget(&nextButton);
    connect(&nextButton, &QAbstractButton::clicked, [this](bool) { AdvanceSong(1); });

    toolBar->addSeparator();

    speedHalveButton.setIcon(QIcon(":/icons/speed-halve.ico"));
    speedHalveButton.setFixedSize(32, 32);
    speedHalveButton.setIconSize(QSize(32, 32));
    toolBar->addWidget(&speedHalveButton);
    connect(&speedHalveButton, &QAbstractButton::clicked, [this](bool) { SpeedHalve(); });

    speedDoubleButton.setIcon(QIcon(":/icons/speed-double.ico"));
    speedDoubleButton.setFixedSize(32, 32);
    speedDoubleButton.setIconSize(QSize(32, 32));
    toolBar->addWidget(&speedDoubleButton);
    connect(&speedDoubleButton, &QAbstractButton::clicked, [this](bool) { SpeedDouble(); });

    toolBar->addSeparator();

    toolBar->addWidget(&vuMeter);

    toolBar->addSeparator();

    volumeSlider.setFixedSize(128, 32);
    volumeSlider.setOrientation(Qt::Horizontal);
    volumeSlider.setMinimum(0);
    volumeSlider.setMaximum(100);
    volumeSlider.setTickInterval(10);
    volumeSlider.setTickPosition(QSlider::TicksBothSides);
    volumeSlider.setToolTip("Playback Volume");
    toolBar->addWidget(&volumeSlider);
    connect(&volumeSlider, &QAbstractSlider::valueChanged, [this](int value) {
        if (!settings)
            return;
        settings->playbackVolume = static_cast<uint32_t>(value);
        settings->dirty = true;
        if (playbackEngine)
            playbackEngine->SetPlaybackVolume(ConvertSliderVolume(value));
    });
}

void MainWindow::SetupWidgets()
{
    /* 1. Create containers. */
    containerCentralLayout.addWidget(&containerCentralSplitter);
    containerCentralLayout.setContentsMargins(0, 0, 4, 0);
    containerCentralSplitter.setChildrenCollapsible(false);
    containerCentralSplitter.addWidget(&containerLeft);
    containerCentralSplitter.addWidget(&containerRight);
    containerCentralSplitter.setStretchFactor(0, 1);
    containerCentralSplitter.setStretchFactor(1, 4);
    setCentralWidget(&containerCentral);

    /* 2. Create songlist and playlist. */
    containerLeftLayout.addWidget(&songlistWidget);
    connect(&songlistWidget.listWidget, &QAbstractItemView::doubleClicked, [this](const QModelIndex &index) {
        SonglistSwitchSong(index.row());
    });
    connect(&songlistWidget, &SonglistWidget::PlayActionTriggered, [this](int row) { SonglistSwitchSong(row); });
    containerLeftLayout.addWidget(&playlistWidget);
    connect(&playlistWidget.listWidget, &QAbstractItemView::doubleClicked, [this](const QModelIndex &index) {
        PlaylistSwitchSong(index.row());
    });
    connect(&playlistWidget, &SonglistWidget::PlayActionTriggered, [this](int row) { PlaylistSwitchSong(row); });
    containerLeftLayout.setContentsMargins(0, 0, 0, 0);

    /* 3. Create rom info and main status view. */
    containerStatusInfoLayout.addWidget(&containerStatusInfoSplitter);
    containerStatusInfoLayout.setContentsMargins(0, 0, 0, 0);
    containerStatusInfoSplitter.setChildrenCollapsible(false);
    containerStatusInfoSplitter.addWidget(&statusWidgetScrollArea);
    containerStatusInfoSplitter.addWidget(&infoWidget);
    containerStatusInfoSplitter.setStretchFactor(0, 5);
    containerStatusInfoSplitter.setStretchFactor(1, 1);
    statusWidgetScrollArea.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    statusWidgetScrollArea.setWidgetResizable(true);
    statusWidgetScrollArea.setWidget(&statusWidget);
    infoWidget.setMaximumWidth(250);

    /* 4. Create log */
    containerRightLayout.addWidget(&containerRightSplitter);
    containerRightSplitter.setChildrenCollapsible(false);
    containerRightSplitter.addWidget(&containerStatusInfo);
    containerRightSplitter.addWidget(&logWidget);
    containerRightSplitter.setStretchFactor(0, 5);
    containerRightSplitter.setStretchFactor(1, 1);
    containerRightLayout.setContentsMargins(0, 0, 0, 0);
    logWidget.setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    logWidget.setFont(QFont("Monospace"));    // TODO does this work on windows?
    logWidget.setAcceptRichText(false);
    LogAppend("agbplay ready!");

    centralWidget()->show();
}

void MainWindow::SetupTouchInterface()
{
    for (auto *toolbar : findChildren<QToolBar *>()) {
        // Release QWidgetActions before moving their widgets; otherwise the
        // toolbar continues managing their visibility and geometry.
        for (auto *action : toolbar->actions())
            toolbar->removeAction(action);
        toolbar->hide();
    }
    auto *controls = new TouchControlRows(this);
    controls->setObjectName("touchControls");
    auto *headerRow = new QWidget(controls);
    headerRow->setObjectName("touchOpeningRow");
    auto *transportRow = new QWidget(controls);
    auto *levelsRow = new QWidget(controls);
    auto *header = new QHBoxLayout(headerRow);
    header->setContentsMargins(0, 0, 0, 0);
    auto *open = new QPushButton(QIcon(":/icons/open-rom.ico"), "Abrir ROM/GSF", headerRow);
    open->setObjectName("openRomButton");
    open->setAccessibleName("Abrir ROM/GSF");
    open->setMinimumHeight(48);
    connect(open, &QPushButton::clicked, openRomAction, &QAction::trigger);
    header->addWidget(open, 1);
    auto *menu = new QToolButton(headerRow);
    menu->setObjectName("touchMenuButton");
    menu->setText("Menú");
    menu->setAccessibleName("Menú de agbplay");
    menu->setMinimumSize(64, 48);
    menu->setPopupMode(QToolButton::InstantPopup);
    auto *popup = new QMenu(menu);
    // A direct entry in the touch menu; no additional cramped control row.
    popup->addAction(openGsfFolderAction);
    for (auto *action : menuBar()->actions())
        if (action->menu())
            popup->addMenu(action->menu());
    popup->addAction("Ir a canción…", this, [this] { JumpSong(); });
    for (auto *submenu : findChildren<QMenu *>())
        // QMenu item sizing ignores min-height on Qt's Android style. Padding
        // contributes to sizeFromContents and keeps real touch targets >=48px.
        submenu->setStyleSheet("QMenu::item { padding: 16px 12px; }");
    menu->setMenu(popup);
    header->addWidget(menu);
    menuBar()->hide();

    auto *transport = new QHBoxLayout(transportRow);
    transport->setContentsMargins(0, 0, 0, 0);
    transport->setSpacing(4);
    const std::array<QPushButton *, 5> buttons{&prevButton, &stopButton, &playButton, &pauseButton, &nextButton};
    const std::array<QString, 5> names{"Anterior", "Detener", "Reproducir", "Pausa", "Siguiente"};
    for (size_t i = 0; i < buttons.size(); ++i) {
        auto *button = buttons[i];
        button->setMaximumSize(QWIDGETSIZE_MAX, 48);
        button->setMinimumSize(48, 48);
        button->setToolTip(names[i]);
        button->setAccessibleName(names[i]);
        transport->addWidget(button, 1);
        button->show();
    }
    auto *levels = new QHBoxLayout(levelsRow);
    levels->setContentsMargins(0, 0, 0, 0);
    for (auto *button : {&speedHalveButton, &speedDoubleButton}) {
        button->setFixedSize(48, 48);
        button->setText(button == &speedHalveButton ? "½×" : "2×");
        button->setIcon(QIcon());
        button->setAccessibleName(button == &speedHalveButton ? "Reducir velocidad" : "Aumentar velocidad");
        levels->addWidget(button);
        button->show();
    }
    volumeSlider.setMinimumSize(80, 48);
    volumeSlider.setMaximumSize(QWIDGETSIZE_MAX, 48);
    volumeSlider.setAccessibleName("Volumen");
    auto *volume = new QLabel(levelsRow);
    volume->setMinimumWidth(32);
    connect(&volumeSlider, &QSlider::valueChanged, volume, [volume](int value) { volume->setText(QString::number(value)); });
    volume->setText(QString::number(volumeSlider.value()));
    levels->addWidget(&volumeSlider, 1);
    volumeSlider.show();
    levels->addWidget(volume);
    controls->setRows({headerRow, transportRow, levelsRow});
    // A heap-owned wrapper outlives all MainWindow value-member controls.
    // Do not parent vuMeter to statusWidget: statusWidget is destroyed first.
    auto *tracksPage = new QWidget(this);
    auto *tracksLayout = new QVBoxLayout(tracksPage);
    tracksLayout->setContentsMargins(0, 0, 0, 0);
    tracksLayout->addWidget(&vuMeter, 0, Qt::AlignHCenter);
    vuMeter.show();
    tracksLayout->addWidget(&statusWidgetScrollArea, 1);
    statusWidget.songWidget.setTouchLayout();
    for (auto *track : statusWidget.findChildren<TrackWidget *>())
        track->setTouchLayout();
    statusWidgetScrollArea.setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    QScroller::grabGesture(statusWidgetScrollArea.viewport(), QScroller::TouchGesture);
    for (auto *list : {&songlistWidget, &playlistWidget}) {
        list->EnableTouchInterface();
        // Activated is supplied by a single tap by Android's item view style.
        // clicked is explicit here for consistent behaviour across Qt styles.
        connect(&list->listWidget, &QAbstractItemView::clicked, this, [this, list](const QModelIndex &index) {
            if (list == &songlistWidget) SonglistSwitchSong(index.row());
            else PlaylistSwitchSong(index.row());
        });
    }
    auto *infoScroll = new QScrollArea(this);
    infoWidget.setMaximumWidth(QWIDGETSIZE_MAX);
    infoScroll->setWidgetResizable(true);
    infoScroll->setWidget(&infoWidget);
    QScroller::grabGesture(infoScroll->viewport(), QScroller::TouchGesture);
    QScroller::grabGesture(logWidget.viewport(), QScroller::TouchGesture);
    auto *oldCentral = takeCentralWidget();
    oldCentral->setParent(this);
    oldCentral->hide();
    auto *touch = new TouchPlayerLayout({&songlistWidget, &playlistWidget, tracksPage, infoScroll, &logWidget}, controls, this);
    setCentralWidget(touch);
    statusBar()->setSizeGripEnabled(false);
}

void MainWindow::SetupStatusBar()
{
    statusBar()->showMessage("No game loaded");
    statusBar()->addPermanentWidget(&progressBar);
    progressBar.setMaximumHeight(16);
    progressBar.setMinimumHeight(16);
    progressBar.setMaximumWidth(128);
    progressBar.setMinimumWidth(128);
    progressBar.setRange(0, 100);
    progressBar.setValue(20);
    progressBar.hide();    // progress bar is only shown on demand
}

void MainWindow::Play()
{
    if (!playbackEngine) {
        playButton.setChecked(false);
        return;
    }

#ifdef Q_OS_ANDROID
    if (!platformCommand) { androidPlayback->play(statusWidget.songWidget.titleLabel.text()); return; }
#endif
    playbackEngine->Play();
#ifdef Q_OS_ANDROID
    if (outputSuspended) { playbackEngine->SuspendOutput(false); outputSuspended = false; }
#endif
    playing = true;

    if (playlistFocus)
        playlistWidget.SetPlayState(true);
    else
        songlistWidget.SetPlayState(true);

    playButton.setChecked(true);
    stopButton.setChecked(false);
    pauseButton.setChecked(false);
}

void MainWindow::Pause()
{
    if (!playbackEngine) {
        pauseButton.setChecked(false);
        return;
    }

#ifdef Q_OS_ANDROID
    if (!platformCommand) {
        if (playing) androidPlayback->pause(); else Play();
        return;
    }
    if (!outputSuspended) { playbackEngine->SuspendOutput(true); outputSuspended = true; }
    if (playing) playing = playbackEngine->Pause();
#else
    playing = playbackEngine->Pause();
#endif

    playButton.setChecked(playing);
    stopButton.setChecked(false);
    pauseButton.setChecked(!playing);
}

void MainWindow::Stop()
{
#ifdef Q_OS_ANDROID
    if (androidPlayback) androidPlayback->stop();
#endif
    if (!playbackEngine) {
        stopButton.setChecked(true);
        return;
    }

#ifdef Q_OS_ANDROID
    if (!outputSuspended) { playbackEngine->SuspendOutput(true); outputSuspended = true; }
#endif
    playbackEngine->Stop();
#ifdef Q_OS_ANDROID
    playbackEngine->DiscardBufferedAudio();
#endif
    playing = false;

    if (playlistFocus)
        playlistWidget.SetPlayState(false);
    else
        songlistWidget.SetPlayState(false);

    playButton.setChecked(false);
    stopButton.setChecked(true);
    pauseButton.setChecked(false);
}

#ifdef Q_OS_ANDROID
void MainWindow::AndroidPlaybackCommand(int command)
{
    QScopedValueRollback guard(platformCommand, true);
    if (!playbackEngine) { androidPlayback->stop(); return; }
    switch (command) {
    case AndroidPlayback::Play: Play(); break;
    case AndroidPlayback::Pause: Pause(); break;
    case AndroidPlayback::Stop: Stop(); break;
    case AndroidPlayback::Next: AdvanceSong(1); break;
    case AndroidPlayback::Previous: AdvanceSong(-1); break;
    case AndroidPlayback::Failed:
        Stop();
        LogAppend("Android audio focus was not granted; playback remains stopped.");
        break;
    }
}
#endif

void MainWindow::AdvanceSong(int indexDelta)
{
    if (!playbackEngine)
        return;

    SonglistWidget &listWidget = playlistFocus ? playlistWidget : songlistWidget;
    const int oldIndex = listWidget.GetSelectedSong();
    const int newIndex = oldIndex + indexDelta;

    QListWidgetItem *oldItem = listWidget.listWidget.item(oldIndex);
    QListWidgetItem *newItem = listWidget.listWidget.item(newIndex);

    if (oldItem == nullptr)
        return;

    if (newItem == nullptr) {
        Stop();
    } else {
        listWidget.SelectSong(newIndex);
        LoadSong(newItem->text().toStdString(), static_cast<uint16_t>(newItem->data(Qt::UserRole).toUInt()));
    }
}

void MainWindow::JumpSong()
{
    if (!playbackEngine)
        return;

    if (songlistWidget.listWidget.count() <= 0)
        return;

    bool ok = false;
    int index = QInputDialog::getInt(
        this,
        "Jump to Song Index",
        "Index:",
        songlistWidget.GetSelectedSong(),
        0,
        songlistWidget.listWidget.count() - 1,
        1,
        &ok
    );
    if (!ok)
        return;

    JumpSong(index);
}

void MainWindow::JumpSong(int index)
{
    if (!playbackEngine)
        return;

    if (index < 0 || index >= songlistWidget.listWidget.count()) {
        MBoxError("Jump to song error", "Unable to jump to song ID, which is beyond the song table.");
        return;
    }

    QListWidgetItem *item = songlistWidget.listWidget.item(index);
    if (!item) {
        assert(false);
        return;
    }

    playlistFocus = false;
    songlistWidget.SelectSong(index);
    LoadSong(item->text().toStdString(), static_cast<uint16_t>(item->data(Qt::UserRole).toUInt()));
}

void MainWindow::LoadSong(const std::string &title, uint16_t id)
{
    if (!playbackEngine)
        return;

    playbackEngine->LoadSong(id);
#ifdef Q_OS_ANDROID
    if (outputSuspended) playbackEngine->DiscardBufferedAudio();
#endif
    statusWidget.songWidget.titleLabel.setText(QString::fromStdString(fmt::format("{} - {}", id, title)));
#ifdef Q_OS_ANDROID
    androidPlayback->title(statusWidget.songWidget.titleLabel.text());
#endif

    if (playlistFocus) {
        songlistWidget.SetPlayState(false);
        playlistWidget.SetPlayState(playing);
    } else {
        songlistWidget.SetPlayState(playing);
        playlistWidget.SetPlayState(false);
    }

    /* Unmute all tracks. This prevents the mute/solo status in the engine and the UI to diverge. */
    statusWidget.loadSongReset();

    if (playing)
        playbackEngine->Play();
}

void MainWindow::SpeedHalve()
{
    if (!playbackEngine)
        return;

    playbackEngine->SpeedHalve();
}

void MainWindow::SpeedDouble()
{
    if (!playbackEngine)
        return;

    playbackEngine->SpeedDouble();
}

void MainWindow::PlaylistAdd()
{
    QList<QListWidgetItem *> items = songlistWidget.listWidget.selectedItems();
    for (int i = 0; i < items.count(); i++) {
        QListWidgetItem *item = items.at(i);
        if (!item)
            continue;
        const std::string name = item->text().toStdString();
        const uint16_t id = static_cast<uint16_t>(item->data(Qt::UserRole).toUInt());
        playlistWidget.AddSong(name, id);
    }
}

void MainWindow::PlaylistRemove()
{
    playlistWidget.RemoveSong();
}

void MainWindow::SonglistSwitchSong(int row)
{
    const QListWidgetItem *item = songlistWidget.listWidget.item(row);
    assert(item);
    if (!item)
        return;
    playlistFocus = false;
    LoadSong(item->text().toStdString(), static_cast<uint16_t>(item->data(Qt::UserRole).toUInt()));
    songlistWidget.SelectSong(row);
    Play();
}

void MainWindow::PlaylistSwitchSong(int row)
{
    const QListWidgetItem *item = playlistWidget.listWidget.item(row);
    assert(item);
    if (!item)
        return;
    playlistFocus = true;
    LoadSong(item->text().toStdString(), static_cast<uint16_t>(item->data(Qt::UserRole).toUInt()));
    playlistWidget.SelectSong(row);
    Play();
}

void MainWindow::RunImport(std::function<std::function<void()>(const std::function<bool()> &)> task)
{
    if (importBusy) return;
    if (importWorker.joinable()) importWorker.join();
    importBusy = true;
    importCancelled = std::make_shared<std::atomic<bool>>(false);
    const auto cancelled = importCancelled;
    importProgress = new QProgressDialog("Importando documentos…", "Cancelar", 0, 0, this);
    importProgress->setWindowModality(Qt::WindowModal);
    importProgress->setMinimumDuration(0);
    connect(importProgress, &QProgressDialog::canceled, this, [cancelled] { cancelled->store(true); });
    importProgress->show();
    importWorker = std::thread([this, task = std::move(task), cancelled] {
        std::function<void()> finish;
        QString error;
        try { finish = task([cancelled] { return cancelled->load(); }); }
        catch (const std::exception &e) { error = QString::fromUtf8(e.what()); }
        QMetaObject::invokeMethod(this, [this, finish = std::move(finish), error, cancelled]() mutable {
            RunWhenActive([this, finish = std::move(finish), error, cancelled] {
            importBusy = false;
            disconnect(importProgress, nullptr, this, nullptr);
            importProgress->close();
            importProgress->deleteLater();
            importProgress = nullptr;
            if (cancelled->load()) return;
            if (!error.isEmpty()) { MBoxError("Document import", error.toStdString()); return; }
            try { if (finish) finish(); }
            catch (const std::exception &e) { MBoxError("Document import", e.what()); }
            });
        }, Qt::QueuedConnection);
    });
}

void MainWindow::RunWhenActive(std::function<void()> task)
{
#ifdef Q_OS_ANDROID
    if (qApp->applicationState() != Qt::ApplicationActive) {
        deferredUiTasks.push_back(std::move(task));
        return;
    }
#endif
    task();
}

void MainWindow::ImportGsfPaths(const std::vector<std::filesystem::path> &paths)
{
    if (!profile) return;
    const auto library = Rom::Instance().IsGsf() ? Rom::Instance().GsfPath() : std::filesystem::path{};
    const auto count = profile->songTableInfoPlayback.count;
    const auto session = profile->sessionId;
    RunImport([this, paths, library, count, session](const std::function<bool()> &cancelled) {
        auto songs = Gsf::ReadPlaylist(paths, library, count, cancelled);
        return [this, songs = std::move(songs), session] {
            if (!profile || profile->sessionId != session) return;
            if (songs.empty()) { MBoxInfo("GSF playlist", "No MINIGSF files were found in the selected documents."); return; }
            ApplyGsfPlaylist(songs, false);
        };
    });
}

void MainWindow::ApplyGsfPlaylist(const std::vector<Gsf::Song> &songs, bool replaceWithoutPrompt)
{
    const bool replace = replaceWithoutPrompt || (playlistWidget.listWidget.count() > 0 &&
        QMessageBox::question(this, "GSF playlist", "Keep the current songs before adding the imported playlist?",
                              QMessageBox::Yes | QMessageBox::No) == QMessageBox::No);
    const QSignalBlocker blocker(&playlistWidget);
    playlistWidget.setUpdatesEnabled(false);
    if (replace) playlistWidget.Clear();
    for (const auto &song : songs) playlistWidget.AddSong(song.title, song.id);
    playlistWidget.setUpdatesEnabled(true);
    profile->dirty = true;
    saveButton.setEnabled(true);
    saveProfileAction->setEnabled(true);
}

void MainWindow::ProfileImportGsfPlaylist(const std::filesystem::path &gameFilePath)
{
    if (!profile || importBusy) return;
    if (FileReader::cmpPathExt(gameFilePath, "zip")) { ImportGsfPaths({gameFilePath}); return; }
#ifndef Q_OS_ANDROID
    if (!gameFilePath.empty()) {
        std::vector<std::filesystem::path> paths;
        for (const auto &entry : std::filesystem::directory_iterator(gameFilePath.parent_path()))
            if (entry.is_regular_file() && FileReader::cmpPathExt(entry.path(), "minigsf")) paths.push_back(entry.path());
        ImportGsfPaths(paths);
        return;
    }
#endif
    QFileDialog dialog(this);
    dialog.setWindowTitle("Seleccionar MINIGSF asociados o ZIP");
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
#ifdef Q_OS_ANDROID
    dialog.setOption(QFileDialog::DontUseNativeDialog, false);
    dialog.setNameFilter("MINIGSF/ZIP (*);;MINIGSF (*.minigsf);;ZIP (*.zip)");
#else
    dialog.setNameFilter("MINIGSF/ZIP (*.minigsf *.zip)");
#endif
    if (!dialog.exec() || dialog.selectedUrls().isEmpty()) return;
    const auto urls = dialog.selectedUrls();
    RunImport([this, urls](const std::function<bool()> &cancelled) {
        std::vector<std::filesystem::path> paths;
        for (const auto &url : urls) {
            if (cancelled()) return std::function<void()>{};
            paths.push_back(DocumentAccess::LocalCopy(url.toString(QUrl::FullyEncoded)));
        }
        return std::function<void()>([this, paths = std::move(paths)] { ImportGsfPaths(paths); });
    });
}

bool MainWindow::AskGameCodeAndUpdateProfile()
{
    /* Create Window */
    QDialog dialog{this};
    dialog.setWindowTitle("Unknown game code");

    QVBoxLayout layout{&dialog};

    QLabel gameCodeLabel{"Unable to determine game code automatically. Please specify manually. Must be four captial letters or digits", this};
    QLineEdit gameCodeLineEdit{&dialog};

    gameCodeLineEdit.setPlaceholderText("AB9J");

    QRegularExpressionValidator charValidator{QRegularExpression{"[A-Z0-9]*"}};

    gameCodeLineEdit.setValidator(&charValidator);

    QDialogButtonBox gameCodeButtonBox{QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog};
    gameCodeButtonBox.button(QDialogButtonBox::Ok)->setEnabled(false);

    layout.addWidget(&gameCodeLabel);
    layout.addWidget(&gameCodeLineEdit);
    layout.addWidget(&gameCodeButtonBox);

    dialog.setLayout(&layout);


    /* Only enable Ok Button for valid input */
    auto resultValidator = [&](const QString &) {
        // Only allow game code up to four characters
        if (gameCodeLineEdit.text().size() > 4)
            gameCodeLineEdit.setText(gameCodeLineEdit.text().left(4));

        QPushButton *b = gameCodeButtonBox.button(QDialogButtonBox::Ok);

        if (gameCodeLineEdit.text().size() == 4)
            b->setEnabled(true);
        else
            b->setEnabled(false);
    };

    connect(&gameCodeLineEdit, &QLineEdit::textChanged, resultValidator);
    connect(&gameCodeButtonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&gameCodeButtonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    /* Actually query the user */
    if (dialog.exec() != QDialog::Accepted)
        return false;

    const std::string gameCode = gameCodeLineEdit.text().toStdString();

    profile->gameMatch.gameCodes.clear();
    profile->gameMatch.gameCodes.emplace_back(gameCode);
    ProfileManager::SetStandardPath(*profile, gameCode, profile->songTableInfoConfig.tableIdx);
    return true;
}

void MainWindow::ApplyAudioPreferences(AudioOutputMode previousMode)
{
        if (playbackEngine) {
#ifdef Q_OS_ANDROID
            if (settings->androidAudioOutputMode != previousMode) {
                try { playbackEngine->SetOutputMode(settings->androidAudioOutputMode, outputSuspended); }
                catch (const std::exception &error) { Stop(); MBoxError("Audio output", error.what()); }
            }
#else
            (void)previousMode;
#endif
            playbackEngine->SetOutputNumBuffers(settings->playbackOutputNumBuffers);
            playbackEngine->SetMaxLoops(settings->playbackLoopIndefinitely ? -1 : settings->playbackMaxLoops);
        }
}

void MainWindow::LoadDiagnosticWav()
{
    if (!playbackEngine) { MBoxInfo("Comparar WAV", "Primero abre una ROM/GSF. El WAV sustituye temporalmente la síntesis; selecciona una canción para volver."); return; }
    if (importBusy) return;
    QFileDialog dialog(this);
    dialog.setWindowTitle("Comparar WAV estéreo por la salida seleccionada");
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setNameFilter("WAV PCM (*.wav);;Todos (*)");
    if (!dialog.exec() || dialog.selectedUrls().isEmpty()) return;
    const auto url = dialog.selectedUrls().front().toString(QUrl::FullyEncoded);
    const auto expected = playbackEngine.get();
    const auto rate = playbackEngine->SampleRate();
    RunImport([this, url, expected, rate](const std::function<bool()> &cancelled) {
        const auto path = DocumentAccess::LocalCopy(url);
        if (cancelled()) return std::function<void()>{};
        auto wav = std::make_shared<std::unique_ptr<DiagnosticWav>>(std::make_unique<DiagnosticWav>(path, rate));
        return std::function<void()>([this, expected, wav, path] {
            if (playbackEngine.get() != expected) return;
            Stop();
#ifndef Q_OS_ANDROID
            playbackEngine->SuspendOutput(true);
#endif
            playbackEngine->LoadDiagnosticWav(std::move(*wav));
            LogAppend("Diagnostic WAV (loop): " + path.filename().string() + "; no synthesis/resampling, same output gain. Select a song to return to MP2K.");
#ifdef Q_OS_ANDROID
            statusWidget.songWidget.titleLabel.setText(QString::fromStdString("WAV: " + path.filename().string()));
            androidPlayback->title(statusWidget.songWidget.titleLabel.text());
#else
            playbackEngine->SuspendOutput(false);
#endif
            Play();
        });
    });
}

void MainWindow::LoadGame()
{
    QFileDialog fileDialog(this);
    fileDialog.setFileMode(QFileDialog::ExistingFiles);
    fileDialog.setAcceptMode(QFileDialog::AcceptOpen);
    fileDialog.setWindowTitle("Abrir ROM/GSF y MINIGSF asociados");
#ifdef Q_OS_ANDROID
    // Qt's Android platform plugin maps ExistingFile + AcceptOpen to SAF's
    // ACTION_OPEN_DOCUMENT. Unknown GBA/GSF MIME types must remain selectable.
    fileDialog.setOption(QFileDialog::DontUseNativeDialog, false);
    fileDialog.setNameFilter("ROM/GSF/ZIP (*);;GBA (*.gba);;GSF (*.gsflib *.gsf *.minigsf);;ZIP (*.zip)");
#else
    fileDialog.setNameFilter("GBA-ROMs (*.gba *.zip);;GSF-Lib (*.gsflib *.gsf *.zip)");
#endif

    if (!settings->lastOpenFileDirectory.empty())
        fileDialog.setDirectory(QString::fromStdWString(settings->lastOpenFileDirectory.wstring()));

    if (!fileDialog.exec())
        return;

    if (fileDialog.selectedUrls().isEmpty() || importBusy) return;
    const auto urls = fileDialog.selectedUrls();
    RunImport([this, urls](const std::function<bool()> &cancelled) {
        std::vector<std::filesystem::path> paths;
        for (const auto &url : urls) {
            if (cancelled()) return std::function<void()>{};
            paths.push_back(DocumentAccess::LocalCopy(url.toString(QUrl::FullyEncoded)));
        }
        return std::function<void()>([this, paths = std::move(paths)] { OpenDocuments(paths); });
    });
}

void MainWindow::LoadGsfFolder()
{
    if (importBusy) return;
    QFileDialog dialog(this);
    dialog.setWindowTitle("Seleccionar carpeta del OST GSF");
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
#ifdef Q_OS_ANDROID
    // Qt maps this mode to ACTION_OPEN_DOCUMENT_TREE, including its URI grant.
    dialog.setOption(QFileDialog::DontUseNativeDialog, false);
#endif
    if (dialog.exec() && !dialog.selectedUrls().isEmpty())
        OpenGsfFolder(dialog.selectedUrls().front().toString(QUrl::FullyEncoded));
}

void MainWindow::OpenGsfFolder(const QString &directory)
{
    RunImport([this, directory](const std::function<bool()> &cancelled) {
        auto documents = DocumentAccess::ListGsfFolder(directory);
        if (cancelled()) return std::function<void()>{};
        return std::function<void()>([this, documents = std::move(documents)] {
            std::vector<size_t> libraries;
            QStringList names;
            for (size_t i = 0; i < documents.size(); ++i) {
                if (documents[i].name.endsWith(".gsflib", Qt::CaseInsensitive) ||
                    documents[i].name.endsWith(".gsf", Qt::CaseInsensitive)) {
                    if (names.contains(documents[i].name, Qt::CaseInsensitive)) {
                        MBoxError("Carpeta GSF", "Several libraries have the same filename. Their MINIGSF _lib tags cannot distinguish them; use separate OST folders.");
                        return;
                    }
                    libraries.push_back(i);
                    names.push_back(documents[i].name);
                }
            }
            if (libraries.empty()) {
                MBoxError("Carpeta GSF", "No GSFLIB was found. Select the OST folder containing the GSFLIB and MINIGSF files directly, not its parent folder.");
                return;
            }
            size_t library = libraries.front();
            if (libraries.size() > 1) {
                bool ok = false;
                const auto name = QInputDialog::getItem(this, "Carpeta GSF",
                    "Hay varias bibliotecas. ¿Qué conjunto deseas abrir?", names, 0, false, &ok);
                if (!ok) return;
                library = libraries.at(static_cast<size_t>(names.indexOf(name)));
            }
            ImportGsfFolder(documents, library);
        });
    });
}

void MainWindow::ImportGsfFolder(const std::vector<DocumentAccess::FolderDocument> &documents, size_t library)
{
    RunImport([this, documents, library](const std::function<bool()> &cancelled) {
        if (cancelled()) return std::function<void()>{};
        const auto &chosen = documents.at(library);
        const auto lib = DocumentAccess::LocalCopy(chosen.uri, chosen.name);
        std::vector<std::filesystem::path> minis;
        for (const auto &document : documents) {
            if (cancelled()) return std::function<void()>{};
            if (document.name.endsWith(".minigsf", Qt::CaseInsensitive))
                minis.push_back(DocumentAccess::LocalCopy(document.uri, document.name));
        }
        // Fully parse before closing the current game; never publish a partial
        // import. Ignore only valid selectors associated with another library.
        auto songs = Gsf::ReadPlaylist(minis, lib, UINT16_MAX, cancelled, true);
        return std::function<void()>([this, lib, songs = std::move(songs)] {
            OpenDocuments({lib}, &songs);
        });
    });
}

void MainWindow::OpenDocuments(const std::vector<std::filesystem::path> &paths,
                               const std::vector<Gsf::Song> *automaticPlaylist)
{
    std::vector<std::filesystem::path> libraries, minis;
    for (const auto &path : paths)
        (FileReader::cmpPathExt(path, "minigsf") ? minis : libraries).push_back(path);
    if (libraries.empty() && profile) { ImportGsfPaths(minis); return; }
    if (libraries.size() != 1) {
        MBoxError("Open documents", "Select one GBA/GSFLIB/ZIP and optionally its MINIGSF files. A MINIGSF alone needs an already loaded library.");
        return;
    }
    const auto selectedFile = libraries.front();
    // A provider/read failure must leave the current game and playlist intact.
    if (!CloseGame())
        return;
    std::vector<std::shared_ptr<Profile>> profileCandidates;

    /* Load ROM and find matching profiles. GetProfiles creates a new profile if the scanner
     * is able to find something. */
    try {
        Rom::CreateInstance(selectedFile);
        profileCandidates = pm->GetProfiles(Rom::Instance());
    } catch (const std::exception &e) {
        MBoxError("Load Error", e.what());
        return;
    }

    if (profileCandidates.size() > 1) {
        SelectProfileDialog profileDialog;
        for (const std::shared_ptr<Profile> &profile : profileCandidates)
            profileDialog.addToSelectionDialog(*profile);

        if (!profileDialog.exec())
            return;

        assert(profileDialog.selectedProfile() >= 0);

        profile = profileCandidates.at(static_cast<size_t>(profileDialog.selectedProfile()));
    } else {
        profile = profileCandidates.at(0);
    }

    /* If the final profile doesn't have a path set, this is probably a GSF file without game code.
     * Manually ask the user for game code. */
    if (profile->path.empty()) {
        if (!AskGameCodeAndUpdateProfile()) {
            profile->dirty = false; // <-- Do not ask to save the current profile and discard immediately
            CloseGame();
            MBoxError("GSF Load Error", "Unable to create profile with unknown game code. Please try again and specify a valid game code");
            return;
        }
    }

    if (automaticPlaylist) {
        for (const auto &song : *automaticPlaylist) {
            if (song.id >= profile->songTableInfoPlayback.count) {
                MBoxError("GSF playlist", fmt::format("MINIGSF '{}' selects song {}, outside the detected song table ({})",
                    song.filename.string(), song.id, profile->songTableInfoPlayback.count));
                profile->dirty = false;
                CloseGame();
                return;
            }
        }
    }

    for (uint16_t i = 0; i < profile->songTableInfoPlayback.count; i++) {
        auto songName = fmt::format("{:04}", i);
        songlistWidget.AddSong(songName, i);
    }

    for (size_t i = 0; i < profile->playlist.size(); i++) {
        playlistWidget.AddSong(profile->playlist.at(i).name, profile->playlist.at(i).id);
    }

    /* Inserting songs into the playlistWidget falsely marks the profile as dirty.
     * Undo this. */
    profile->dirty = false;
    saveButton.setEnabled(false);
    saveProfileAction->setEnabled(false);

    infoWidget.romNameLineEdit.setText(QString::fromStdString(Rom::Instance().ReadString(0xA0, 12)));
    infoWidget.romCodeLineEdit.setText(QString::fromStdString(Rom::Instance().GetROMCode()));
    infoWidget.songTableLineEdit.setText(
        QString::fromStdString(fmt::format("0x{:X}", profile->songTableInfoPlayback.pos))
    );
    infoWidget.songCountLineEdit.setText(QString::number(profile->songTableInfoPlayback.count));

    UpdateSoundMode();

    if (Rom::Instance().IsGsf()) {
        const auto inactive = std::count_if(profile->playerTablePlayback.begin(), profile->playerTablePlayback.end(),
            [](const auto &player) { return player.maxTracks == 0; });
        LogAppend(fmt::format("GSF profile: scanned players={}, configured players={}, playback players={}, inactive zero-track slots={}",
            profile->playerTableScanned.size(), profile->playerTableConfig.size(), profile->playerTablePlayback.size(), inactive));
    }
    try {
        playbackEngine = std::make_unique<PlaybackEngine>(settings->playbackSampleRate, ConvertSliderVolume(settings->playbackVolume), *profile,
            AudioBufferPolicy::DefaultReserveMs, settings->androidAudioOutputMode);
    } catch (const std::exception &e) {
        MBoxError("Audio initialization", e.what());
        CloseGame();
        return;
    }
#ifdef Q_OS_ANDROID
    playbackEngine->SuspendOutput(true);
    outputSuspended = true;
#endif
    visualizerState = std::make_unique<MP2KVisualizerState>();

    playbackEngine->SetOutputNumBuffers(settings->playbackOutputNumBuffers);
    playbackEngine->SetMaxLoops(settings->playbackLoopIndefinitely ? -1 : settings->playbackMaxLoops);

    exportSongsAction->setEnabled(true);
    exportStemsAction->setEnabled(true);
    quickExportSongAction->setEnabled(true);
    quickExportStemsAction->setEnabled(true);
    benchmarkSelectedAction->setEnabled(true);
    profileSettings->setEnabled(true);
    profileMinigsfImport->setEnabled(true);

    if (automaticPlaylist) {
        if (!automaticPlaylist->empty()) {
            ApplyGsfPlaylist(*automaticPlaylist, true);
            playlistFocus = true;
            playlistWidget.listWidget.setCurrentRow(0);
            if (auto *tabs = findChild<QTabWidget *>("compactNavigation"); tabs && tabs->isVisible())
                tabs->setCurrentIndex(1);
        }
        LogAppend(fmt::format("GSF folder: imported {} associated MINIGSF songs", automaticPlaylist->size()));
    } else if (Rom::Instance().IsGsf() && profile->playlist.size() == 0) {
        const QString title = "Load Playlist from GSF set?";
        QString message = "You just loaded a GSF set. ";
        message += "The current profile does not contain any songs in the playlist.\n\n";
        message += "Do you want to import the playlist from the accompanying MINIGSFs?";
        QMessageBox mbox(QMessageBox::Icon::Question, title, message, QMessageBox::Yes | QMessageBox::No, this);
        if (mbox.exec() == QMessageBox::Yes) {
            if (!minis.empty()) ImportGsfPaths(minis);
            else ProfileImportGsfPlaylist(selectedFile);
        }
    }

    if (settings->lastOpenFileDirectory != selectedFile.parent_path()) {
        settings->lastOpenFileDirectory = selectedFile.parent_path();
        settings->Save();
    }
}

bool MainWindow::CloseGame()
{
    if (int result = AskSaveProfile(); result == QMessageBox::Cancel)
        return false;
    else if (result == QMessageBox::Save)
        SaveProfile();
    else
        DiscardProfile();
    Stop();
    playbackEngine.reset();
    profile.reset();

    infoWidget.romNameLineEdit.setText("<none>");
    infoWidget.romCodeLineEdit.setText("<none>");
    infoWidget.songTableLineEdit.setText("<none>");
    infoWidget.songCountLineEdit.setText("<none>");

    infoWidget.pcmVolValLabel.setText("<none>");
    infoWidget.pcmRevValLabel.setText("<none>");
    infoWidget.pcmFreqValLabel.setText("<none>");
    infoWidget.pcmChnValLabel.setText("<none>");
    infoWidget.pcmDacValLabel.setText("<none>");

    statusWidget.reset();
    songlistWidget.Clear();
    playlistWidget.Clear();
    exportSongsAction->setEnabled(false);
    exportStemsAction->setEnabled(false);
    quickExportSongAction->setEnabled(false);
    quickExportStemsAction->setEnabled(false);
    benchmarkSelectedAction->setEnabled(false);
    profileSettings->setEnabled(false);
    profileMinigsfImport->setEnabled(false);
    return true;
}

void MainWindow::ExportAudio(bool benchmarkOnly, bool separateTracks, bool quick)
{
    /* Check if export is currently going on and join thread afterwards. */
    if (exportBusy) {
        ExportStillInProgress();
        return;
    }

    if (!profile)
        return;

    if (exportThread) {
        exportThread->join();
        exportThread.reset();
    }

    /* Select directory to export to. */
    QFileDialog fileDialog{this};
    fileDialog.setFileMode(QFileDialog::Directory);
    fileDialog.setDirectory(QString::fromStdWString(settings->exportQuickExportDirectory.wstring()));

    std::filesystem::path directory;
#ifdef Q_OS_ANDROID
    // Native directory picker returns a SAF tree URI, which libsndfile cannot open.
    // Render privately; the Share Export action grants access to a selected WAV.
    directory = OS::GetMusicDirectory();
    if (!benchmarkOnly)
        LogAppend("Export directory: " + directory.string());
#else
    if (benchmarkOnly) {
        directory.clear();
    } else if (quick && !settings->exportQuickExportAsk) {
        directory = settings->exportQuickExportDirectory;
    } else {
        if (!fileDialog.exec())
            return;

        if (fileDialog.selectedFiles().size() != 1)
        return;
        directory = fileDialog.selectedFiles().at(0).toStdWString();
    }

#endif

    /* Make a Profile copy for use in export thread.
     * That way we do not overwrite it while exporting. */
    Profile profileToExport = *profile;

    /* Modify export profile to only contain selected songs. */
    profileToExport.playlist.clear();

    if (quick) {
        QListWidgetItem *item;
        if (playlistFocus) {
            item = playlistWidget.listWidget.item(playlistWidget.GetSelectedSong());
        } else {
            item = songlistWidget.listWidget.item(songlistWidget.GetSelectedSong());
        }

        if (item) {
            const std::string name = item->text().toStdString();
            const uint16_t id = static_cast<uint16_t>(item->data(Qt::UserRole).toUInt());
            profileToExport.playlist.emplace_back(Profile::PlaylistEntry{name, id});
        }
    } else {
        for (int i = 0; i < songlistWidget.listWidget.count(); i++) {
            QListWidgetItem *item = songlistWidget.listWidget.item(i);
            if (!item)
                continue;

            const std::string name = item->text().toStdString();
            const uint16_t id = static_cast<uint16_t>(item->data(Qt::UserRole).toUInt());
            if (item->checkState() == Qt::Checked)
                profileToExport.playlist.emplace_back(Profile::PlaylistEntry{name, id});
        }

        for (int i = 0; i < playlistWidget.listWidget.count(); i++) {
            QListWidgetItem *item = playlistWidget.listWidget.item(i);
            if (!item)
                continue;

            const std::string name = item->text().toStdString();
            const uint16_t id = static_cast<uint16_t>(item->data(Qt::UserRole).toUInt());
            if (item->checkState() == Qt::Checked)
                profileToExport.playlist.emplace_back(Profile::PlaylistEntry{name, id});
        }
    }

    exportBusy = true;

    // TODO implement progress bar
    exportThread = std::make_unique<std::thread>(
        [this](
            std::filesystem::path tDirectory,
            Settings tSettings,
            Profile tProfile,
            bool tBenchmarkOnly,
            bool tSeparateTracks
        ) {
            try {
                SoundExporter se(tDirectory, {}, tSettings, tProfile, tBenchmarkOnly, tSeparateTracks);
                se.Export();
            } catch (std::exception &e) {
                Debug::print("Export failed:\n{}", e.what());
            }
            exportBusy = false;
        },
        directory,
        *settings,
        profileToExport,
        benchmarkOnly,
        separateTracks
    );
#ifdef __linux__
    pthread_setname_np(exportThread->native_handle(), "export thread");
#endif
}

void MainWindow::ExportStillInProgress()
{
    MBoxError(
        "Export in progress", "There is already an export in progress. Please wait for the current export to complete."
    );
}

void MainWindow::SaveLog()
{
    QFileDialog fileDialog(this);
    fileDialog.setFileMode(QFileDialog::AnyFile);
    fileDialog.setAcceptMode(QFileDialog::AcceptSave);
    fileDialog.setNameFilter("Log File (*.log *.txt)");
    fileDialog.setDefaultSuffix("log");

    if (!fileDialog.exec())
        return;

    if (fileDialog.selectedFiles().size() != 1)
        return;

    QFile output(fileDialog.selectedFiles().at(0));
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        output.write(logWidget.toPlainText().toUtf8()) < 0) {
        MBoxError("Save log file failed", output.errorString().toStdString());
    }
}

void MainWindow::SaveProfile()
{
    if (!profile || !profile->dirty)
        return;

    // TODO handle saving when profile is no longer in profile list, do not save.
    // TODO ask (on close?) if it should be added back to the list.

    profile->playlist.clear();
    for (int i = 0; i < playlistWidget.listWidget.count(); i++) {
        QListWidgetItem *item = playlistWidget.listWidget.item(i);
        if (!item)
            continue;

        const std::string title = item->text().toStdString();
        const uint16_t id = static_cast<uint16_t>(item->data(Qt::UserRole).toUInt());
        profile->playlist.emplace_back(Profile::PlaylistEntry{title, id});
    }

    saveButton.setEnabled(false);
    saveProfileAction->setEnabled(false);
    assert(pm);
    pm->SaveProfiles();
}

void MainWindow::DiscardProfile()
{
    if (!profile)
        return;

    saveButton.setEnabled(false);
    saveProfileAction->setEnabled(false);
    profile->dirty = false;
}

int MainWindow::AskSaveProfile()
{
    if (!profile || !profile->dirty)
        return QMessageBox::Discard;

    const QString title = "Save changed profile?";
    const QString message = "The current profile has unsaved changes. Do you want to save them?";
    QMessageBox mbox(
        QMessageBox::Icon::Question,
        title,
        message,
        QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard,
        this
    );
    return mbox.exec();
}

void MainWindow::MBoxInfo(const std::string &title, const std::string &msg)
{
    RunWhenActive([this, title, msg] {
    const QString qtitle = QString::fromStdString(title);
    const QString qmsg = QString::fromStdString(msg);
    QMessageBox mbox(QMessageBox::Icon::Information, qtitle, qmsg, QMessageBox::Ok, this);
    mbox.exec();
    });
}

void MainWindow::MBoxError(const std::string &title, const std::string &msg)
{
    RunWhenActive([this, title, msg] {
    const QString qtitle = QString::fromStdString(title);
    const QString qmsg = QString::fromStdString(msg);
    QMessageBox mbox(QMessageBox::Icon::Critical, qtitle, qmsg, QMessageBox::Ok, this);
    mbox.exec();
    });
}

float MainWindow::ConvertSliderVolume(int value)
{
    const float valueFloat = static_cast<float>(value) / static_cast<float>(volumeSlider.maximum());
    return std::clamp(valueFloat * valueFloat, 0.0f, 1.0f);
}

void MainWindow::UpdateSoundMode()
{
    /* This table is duplicate and can be found elsewhere, but I currently don't know
     * where to put it in a common place. */
    static const std::array<uint32_t, 16> rateTable{
        0, 5734, 7884, 10512, 13379, 15768, 18157, 21024, 26758, 31536, 36314, 40137, 42048, 0, 0, 0
    };

    static const std::array<const char *, 4> dacTable{
        "32 kHz @ 9 bit",
        "65 kHz @ 8 bit",
        "131 kHz @ 7 bit",
        "262 kHz @ 6 bit",
    };

    const std::string vol = fmt::format("{}/15", profile->mp2kSoundModePlayback.vol);
    const std::string rev = fmt::format("{}/127", profile->mp2kSoundModePlayback.rev % 128);
    const std::string freq = fmt::format(
        "{}: {} Hz", profile->mp2kSoundModePlayback.freq, rateTable[profile->mp2kSoundModePlayback.freq % 16]
    );
    const std::string chn = fmt::format("{}/12", profile->mp2kSoundModePlayback.maxChannels);
    const std::string dac = dacTable[profile->mp2kSoundModePlayback.dacConfig % 4];

    infoWidget.pcmVolValLabel.setText(QString::fromStdString(vol));
    infoWidget.pcmRevValLabel.setText(QString::fromStdString(rev));
    infoWidget.pcmFreqValLabel.setText(QString::fromStdString(freq));
    infoWidget.pcmChnValLabel.setText(QString::fromStdString(chn));
    infoWidget.pcmDacValLabel.setText(QString::fromStdString(dac));
}

void MainWindow::UpdateMute(size_t trackNo, bool audible, bool visualOnly)
{
    if (!playbackEngine || visualOnly)
        return;

    playbackEngine->Mute(trackNo, !audible);
}

void MainWindow::StatusUpdate()
{
    if (playbackEngine) {
#ifdef Q_OS_ANDROID
        static auto lastAudioReport = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        if (now - lastAudioReport >= std::chrono::seconds(5)) {
            playbackEngine->PollOutputDiagnostics();
            const auto d = playbackEngine->TakeAudioDiagnostics();
            Debug::print("Android audio ({}s): underrun callbacks={}, missing frames={}, "
                         "queue before callback min/max={}/{}, producer peak={}, target/low={}/{} frames "
                         "({:.2f}/{:.2f} ms), synthesis block={} frames",
                         d.elapsedSeconds, d.queue.underrunCallbacks, d.queue.underrunFrames,
                         d.queue.minQueued, d.queue.maxQueued, d.queue.maxStored,
                         d.targetFrames, d.lowWatermarkFrames,
                         1000.0 * static_cast<double>(d.targetFrames) / d.sampleRate,
                         1000.0 * static_cast<double>(d.lowWatermarkFrames) / d.sampleRate,
                         d.synthesisBlockFrames);
            Debug::print("Android timing: callbacks={}..{} frames, {:.2f} Hz / {:.0f} frames/s, "
                         "produced={}, max synth={:.3f} ms, visualizer={:.3f} ms, producer work={:.3f} ms, "
                         "producer gap={:.3f} ms, wake lateness={:.3f} ms, callback interval={:.3f} ms, callback={:.3f} ms",
                         d.minCallbackFrames, d.maxCallbackFrames,
                         d.elapsedSeconds > 0 ? static_cast<double>(d.callbackCount) / d.elapsedSeconds : 0.0,
                         d.elapsedSeconds > 0 ? static_cast<double>(d.requestedFrames) / d.elapsedSeconds : 0.0,
                         d.producedFrames,
                         static_cast<double>(d.maxSynthesisNs) / 1.0e6,
                         static_cast<double>(d.maxVisualizerNs) / 1.0e6,
                         static_cast<double>(d.maxProducerWorkNs) / 1.0e6,
                         static_cast<double>(d.maxProducerGapNs) / 1.0e6,
                         static_cast<double>(d.maxWakeLatenessNs) / 1.0e6,
                         static_cast<double>(d.maxCallbackIntervalNs) / 1.0e6,
                         static_cast<double>(d.maxCallbackNs) / 1.0e6);
            Debug::print("Android PCM: output RMS/peak={:.6f}/{:.6f}, clipped/nonfinite samples={}/{}, "
                         "metered samples={}, telemetry dropped frames={}",
                         d.outputRms, d.outputPeak, d.clippedSamples, d.nonFiniteSamples,
                         d.meteredSamples, d.meterDroppedFrames);
            lastAudioReport = now;
        }
#endif
        const auto error = playbackEngine->TakeError();
        if (!error.empty()) {
#ifdef Q_OS_ANDROID
            Stop();
#endif
            playing = false;
            playButton.setChecked(false);
            pauseButton.setChecked(false);
            stopButton.setChecked(true);
            songlistWidget.SetPlayState(false);
            playlistWidget.SetPlayState(false);
            statusBar()->showMessage(QString::fromStdString("Song stopped: " + error));
        }
    }

    if (!playbackEngine || !visualizerState)
        return;

#ifdef Q_OS_ANDROID
    // Song completion/transport still runs while hidden; visualizers do not.
    if (qApp->applicationState() != Qt::ApplicationActive) {
        if (playing && playbackEngine->SongEnded()) AdvanceSong(1);
        return;
    }
#endif
    playbackEngine->GetVisualizerState(*visualizerState);
    vuMeter.SetLevel(
        visualizerState->masterRmsLeft,
        visualizerState->masterRmsRight,
        visualizerState->masterPeakLeft,
        visualizerState->masterPeakRight
    );
    statusWidget.setVisualizerState(*visualizerState);

    if (playing && playbackEngine->SongEnded()) {
        /* AdvanceSong automatically stops if this was the last song in the list. */
        AdvanceSong(1);
    }
}

void MainWindow::LogCallback(const std::string &msg, void *void_this)
{
    MainWindow *_this = static_cast<MainWindow *>(void_this);
    // Qt 6.7 only
    // QMetaObject::invokeMethod(_this, &MainWindow::LogAppend, Qt::QueuedConnection, msg);
    emit _this->LogMessageReceived(msg);
}

void MainWindow::LogAppend(std::string msg)
{
    logWidget.append(QString::fromStdString(msg));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (exportBusy) {
        ExportStillInProgress();
        event->ignore();
        return;
    }

    if (exportThread) {
        exportThread->join();
        exportThread.reset();
    }

    if (CloseGame())
        event->accept();
    else
        event->ignore();

    settings->Save();
}
