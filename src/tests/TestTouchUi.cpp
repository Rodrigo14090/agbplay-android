#include "MainWindow.hpp"
#include "TouchPlayerLayout.hpp"
#include "SyntheticGsf.hpp"
#include "PlaybackEngine.hpp"
#include "DocumentAccess.hpp"
#include "Settings.hpp"
#include "SettingsWindow.hpp"
#include "OS.hpp"
#include <nlohmann/json.hpp>

#include <QAction>
#include <QDir>
#include <QMenu>
#include <QMenuBar>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QMessageBox>
#include <QProgressDialog>
#include <QInputDialog>
#include <QScroller>
#include <QScrollBar>
#include <QWheelEvent>
#include <fstream>

class TestTouchUi : public QObject
{
    Q_OBJECT
    QTemporaryDir config;

    void settle() { QTest::qWait(30); }
    void capture(QWidget &window, const QString &name)
    {
        const auto directory = qEnvironmentVariable("AGBPLAY_UI_CAPTURE_DIR");
        if (!directory.isEmpty()) {
            QDir().mkpath(directory);
            QVERIFY(window.grab().save(directory + "/" + name + ".png"));
        }
    }
    void checkControls(MainWindow &window)
    {
        auto *controls = window.findChild<QWidget *>("touchControls");
        QVERIFY(controls);
        QVERIFY(window.rect().contains(QRect(controls->mapTo(&window, QPoint{}), controls->size())));
        const auto buttons = controls->findChildren<QAbstractButton *>();
        for (auto *button : buttons) {
            QVERIFY2(button->isVisible(), qPrintable(button->accessibleName()));
            QVERIFY(button->width() >= 48 && button->height() >= 48);
            const QRect bounds(button->mapTo(controls, QPoint{}), button->size());
            QVERIFY2(controls->rect().contains(bounds), qPrintable(button->accessibleName()));
            for (auto *ancestor = button->parentWidget(); ancestor; ancestor = ancestor->parentWidget())
                QVERIFY2(ancestor->rect().contains(QRect(button->mapTo(ancestor, QPoint{}), button->size())),
                         qPrintable(button->accessibleName() + " clipped by ancestor"));
            const QPoint center = button->mapTo(&window, button->rect().center());
            QVERIFY2(window.childAt(center) == button, qPrintable(button->accessibleName() + " is covered"));
            for (auto *other : buttons)
                if (other != button)
                    QVERIFY2(!bounds.intersects(QRect(other->mapTo(controls, QPoint{}), other->size())),
                             qPrintable(button->accessibleName() + " overlaps " + other->accessibleName()));
        }
    }

private slots:
    void initTestCase()
    {
        QVERIFY(config.isValid());
        qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
        QCoreApplication::setOrganizationName("agbplay");
        QCoreApplication::setApplicationName("agbplay-ui-test");
    }
    void songListTouchScroll_data()
    {
        QTest::addColumn<bool>("editable");
        QTest::newRow("songs") << false;
        QTest::newRow("playlist") << true;
    }
    void songListTouchScroll()
    {
        QFETCH(bool, editable);
        SonglistWidget songs("Songs", editable);
        songs.EnableTouchInterface();
        for (int i = 0; i < 200; ++i)
            songs.AddSong("Song " + std::to_string(i), uint16_t(i));
        songs.resize(400, 600);
        songs.show();
        settle();
        auto &list = songs.listWidget;
        auto *scroller = QScroller::scroller(list.viewport());
        // The second movement is wholly above the gesture threshold. Measure
        // rendered content, not scrollbar units: 20 logical pixels must not
        // turn into 20 rows. Hold the finger down so inertia cannot mask it.
        scroller->handleInput(QScroller::InputPress, QPointF(160, 360), 0);
        scroller->handleInput(QScroller::InputMove, QPointF(160, 320), 1000);
        QTest::qWait(60);
        const int before = list.visualItemRect(list.item(100)).top();
        scroller->handleInput(QScroller::InputMove, QPointF(160, 300), 1500);
        QTest::qWait(60);
        const int distance = before - list.visualItemRect(list.item(100)).top();
        QVERIFY2(qAbs(distance - 20) <= 2, qPrintable(QString("20px drag moved content %1px").arg(distance)));
        scroller->handleInput(QScroller::InputRelease, QPointF(160, 300), 2000);
        scroller->stop();
        QCOMPARE(list.currentRow(), 0);
        for (int i = 0; i < list.count(); ++i) {
            QCOMPARE(list.item(i)->data(Qt::UserRole).toUInt(), unsigned(i));
            QCOMPARE(list.item(i)->checkState(), Qt::Checked);
        }
        // A quick flick has a finite coast and settles, rather than traversing
        // dozens of rows or accelerating an already moving list.
        const int start = list.verticalScrollBar()->value();
        scroller->handleInput(QScroller::InputPress, QPointF(160, 360), 3000);
        scroller->handleInput(QScroller::InputMove, QPointF(160, 320), 3010);
        scroller->handleInput(QScroller::InputMove, QPointF(160, 280), 3020);
        scroller->handleInput(QScroller::InputRelease, QPointF(160, 280), 3021);
        QVERIFY(scroller->velocity().y() > 0);
        QVERIFY(scroller->velocity().y() <= 0.251);
        QTRY_COMPARE_WITH_TIMEOUT(scroller->state(), QScroller::Inactive, 1500);
        const qreal maxCoast = scroller->pixelPerMeter().y() * 0.25 * 0.25 / (2 * 0.35);
        const int travelled = list.verticalScrollBar()->value() - start;
        QVERIFY(travelled > 0);
        QVERIFY(travelled <= 80 + maxCoast + 2);
    }
    void songListWheelAndEditing()
    {
        SonglistWidget desktop("Desktop", true), touch("Touch", true);
        QVERIFY(!QScroller::hasScroller(desktop.listWidget.viewport()));
        const auto desktopMode = desktop.listWidget.verticalScrollMode();
        touch.EnableTouchInterface();
        QCOMPARE(desktop.listWidget.verticalScrollMode(), desktopMode);
        // Reference uses the original row-based mode and identical touch row
        // sizes. Compare actual content displacement, not scrollbar values.
        SonglistWidget reference("Reference", true);
        reference.listWidget.setStyleSheet(touch.listWidget.styleSheet());
        reference.listWidget.setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
        for (auto *songs : {&touch, &reference}) {
            for (int i = 0; i < 100; ++i)
                songs->AddSong("Song " + std::to_string(i), uint16_t(i));
            songs->resize(400, 600);
            songs->show();
        }
        settle();
        for (auto *songs : {&touch, &reference})
            songs->listWidget.scrollToItem(songs->listWidget.item(30), QAbstractItemView::PositionAtTop);
        for (int delta : {-120, -120, 120}) {
            int distances[2];
            int index = 0;
            for (auto *songs : {&touch, &reference}) {
                auto &list = songs->listWidget;
                const int before = list.visualItemRect(list.item(50)).top();
                const auto pos = list.viewport()->rect().center();
                QWheelEvent wheel(pos, list.viewport()->mapToGlobal(pos), {}, QPoint(0, delta),
                                  Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QApplication::sendEvent(list.viewport(), &wheel);
                QVERIFY(wheel.isAccepted());
                distances[index++] = before - list.visualItemRect(list.item(50)).top();
            }
            QCOMPARE(distances[0], distances[1]);
        }
        // Click selection and checkboxes still use the original QListWidget
        // handlers. Playlist up/down must retain IDs and the playing marker.
        touch.SelectSong(32);
        touch.listWidget.scrollToItem(touch.listWidget.item(32), QAbstractItemView::PositionAtCenter);
        settle();
        const auto rowRect = touch.listWidget.visualItemRect(touch.listWidget.item(32));
        QTest::mouseClick(touch.listWidget.viewport(), Qt::LeftButton, Qt::NoModifier, rowRect.center());
        QCOMPARE(touch.listWidget.currentRow(), 32);
        QTest::mouseClick(touch.listWidget.viewport(), Qt::LeftButton, Qt::NoModifier,
                          QPoint(rowRect.left() + 14, rowRect.center().y()));
        QCOMPARE(touch.listWidget.item(32)->checkState(), Qt::Unchecked);
        auto *more = touch.findChild<QToolButton *>();
        QVERIFY(more && more->menu());
        QAction *down = nullptr;
        for (auto *action : more->menu()->actions())
            if (action->text() == "Bajar canción") down = action;
        QVERIFY(down);
        down->trigger();
        QCOMPARE(touch.listWidget.currentRow(), 33);
        QCOMPARE(touch.GetSelectedSong(), 33);
        QCOMPARE(touch.listWidget.item(33)->data(Qt::UserRole).toUInt(), 32u);
        QCOMPARE(touch.listWidget.item(33)->checkState(), Qt::Unchecked);
        QCOMPARE(touch.listWidget.dragDropMode(), QAbstractItemView::InternalMove);
    }
    void audioPreferenceRoundTrip()
    {
        Settings saved; saved.Load();
        QCOMPARE(saved.androidAudioOutputMode, AudioOutputMode::Automatic);
        saved.androidAudioOutputMode = AudioOutputMode::MediaCompatibility;
        saved.dirty = true; saved.Save();
        Settings loaded; loaded.Load();
        QCOMPARE(loaded.androidAudioOutputMode, AudioOutputMode::MediaCompatibility);
        const auto path = OS::GetLocalConfigDirectory() / "agbplay" / "config.json";
        auto json = nlohmann::json::parse(std::ifstream(path));
        json["androidAudioOutputMode"] = 12345;
        std::ofstream(path) << json;
        loaded.Load();
        QCOMPARE(loaded.androidAudioOutputMode, AudioOutputMode::Automatic);
        loaded.dirty = true; loaded.Save();
        SettingsWindow preferences(nullptr, loaded);
        QVERIFY(!preferences.findChild<QWidget *>("androidAudioOutputMode")); // Android-only control
    }
    void openActionAndPlaylistStayDistinct()
    {
        MainWindow window(nullptr, true);
        QVERIFY(!window.menuBar()->isNativeMenuBar());
        window.resize(360, 740);
        window.show();
        settle();
        // Observe the actual button -> QAction -> open request path without
        // showing a host file dialog. Android's native SAF is tested manually.
        disconnect(&window, &MainWindow::OpenRomRequested, nullptr, nullptr);
        QSignalSpy open(&window, &MainWindow::OpenRomRequested);
        auto *button = window.findChild<QPushButton *>("openRomButton");
        QVERIFY(button && button->isVisible());
        QTest::mouseClick(button, Qt::LeftButton);
        QCOMPARE(open.count(), 1);
        window.findChild<QAction *>("openRomAction")->trigger();
        QCOMPARE(open.count(), 2);
        SonglistWidget *songs = nullptr, *playlist = nullptr;
        for (auto *list : window.findChildren<SonglistWidget *>())
            (list->title.text() == "Songlist" ? songs : playlist) = list;
        QVERIFY(songs && playlist);
        songs->AddSong("Synthetic song", 7);
        songs->listWidget.setCurrentRow(0);
        QTest::mouseClick(&songs->addRemoveButton, Qt::LeftButton);
        QCOMPARE(playlist->listWidget.count(), 1);
        QCOMPARE(playlist->listWidget.item(0)->data(Qt::UserRole).toUInt(), 7u);
        QCOMPARE(open.count(), 2);
        auto *menu = window.findChild<QToolButton *>("touchMenuButton");
        QVERIFY(menu && menu->isVisible() && menu->menu());
        QCOMPARE(menu->menu()->actions().size(), 6); // folder + original four menus + Jump
        auto *folder = window.findChild<QAction *>("openGsfFolderAction");
        QVERIFY(folder && menu->menu()->actions().contains(folder));
        menu->menu()->popup(menu->mapToGlobal(QPoint{}));
        settle();
        QVERIFY(menu->menu()->actionGeometry(folder).height() >= 48);
        menu->menu()->close();
        disconnect(&window, &MainWindow::OpenGsfFolderRequested, nullptr, nullptr);
        QSignalSpy folderOpen(&window, &MainWindow::OpenGsfFolderRequested);
        folder->trigger();
        QCOMPARE(folderOpen.count(), 1);
        QCOMPARE(open.count(), 2);
    }
    void automaticGsfFolder()
    {
        QTemporaryDir files;
        QVERIFY(files.isValid());
        const auto save = [&](const QString &name, const auto &bytes) {
            const auto path = (files.path() + "/" + name).toStdString();
            std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        };
        save("fixture.gsflib", Fixture::Psf(Fixture::ScannableRom()));
        save("01.minigsf", Fixture::Psf({0}, "title=Primero\n_lib=fixture.gsflib\n"));
        save("02.minigsf", Fixture::Psf({1}, "title=Segundo\n_lib=fixture.gsflib\n"));
        save("03.minigsf", Fixture::Psf({2}, "title=Other library\n_lib=another.gsflib\n"));
        save("cover.jpg", std::vector<uint8_t>{1, 2});
        QDir(files.path()).mkdir("nested");
        save("nested/unused.gsflib", Fixture::Psf(Fixture::ScannableRom()));
        const auto folderUrl = QUrl::fromLocalFile(files.path()).toString();
        const auto documents = DocumentAccess::ListGsfFolder(folderUrl);
        QCOMPARE(documents.size(), size_t(4));
        MainWindow window(nullptr, true);
        window.resize(384, 854);
        window.show();
        int dialogs = 0;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, &window, [&] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                    ++dialogs;
                    box->done(QMessageBox::No);
                }
        });
        dismiss.start(5);
        window.OpenGsfFolder(folderUrl); // no prior library or manual playlist selection
        QTRY_VERIFY_WITH_TIMEOUT(!window.importBusy, 5000);
        QCOMPARE(dialogs, 0);
        QVERIFY(window.playbackEngine);
        QCOMPARE(window.playlistWidget.listWidget.count(), 2);
        QCOMPARE(window.playlistWidget.listWidget.item(0)->text(), QString("Primero"));
        QCOMPARE(window.playlistWidget.listWidget.item(1)->data(Qt::UserRole).toUInt(), 1u);
        QCOMPARE(window.findChild<QTabWidget *>("compactNavigation")->currentIndex(), 1);
        window.PlaylistSwitchSong(1);
        settle();
        QVERIFY(window.playing);
        // A malformed selector fails before closing the currently playing game.
        save("broken.minigsf", std::vector<uint8_t>{1, 2});
        auto *engine = window.playbackEngine.get();
        window.OpenGsfFolder(folderUrl);
        QTRY_VERIFY_WITH_TIMEOUT(!window.importBusy, 5000);
        QTRY_COMPARE(dialogs, 1);
        QCOMPARE(window.playbackEngine.get(), engine);
        QCOMPARE(window.playlistWidget.listWidget.count(), 2);
        QFile::remove(files.path() + "/broken.minigsf");
        // No library: report it, keep the current game, do not scan descendants.
        QTemporaryDir empty;
        window.OpenGsfFolder(QUrl::fromLocalFile(empty.path()).toString());
        QTRY_VERIFY_WITH_TIMEOUT(!window.importBusy, 5000);
        QTRY_COMPARE(dialogs, 2);
        QCOMPARE(window.playbackEngine.get(), engine);
        window.profile->dirty = false;
        QVERIFY(window.CloseGame());
    }
    void ambiguousAndLibraryOnlyFolders()
    {
        QTemporaryDir files;
        QVERIFY(files.isValid());
        const auto save = [&](const QString &name, const auto &bytes) {
            std::ofstream((files.path() + "/" + name).toStdString(), std::ios::binary)
                .write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        };
        save("alpha.gsflib", Fixture::Psf(Fixture::ScannableRom()));
        save("beta.gsflib", Fixture::Psf(Fixture::ScannableRom()));
        save("01.minigsf", Fixture::Psf({0}, "title=Alpha\n_lib=alpha.gsflib\n"));
        save("02.minigsf", Fixture::Psf({1}, "title=Beta\n_lib=beta.gsflib\n"));
        MainWindow window(nullptr, true);
        window.show();
        int choices = 0, messages = 0;
        bool accept = false;
        QTimer respond;
        connect(&respond, &QTimer::timeout, &window, [&] {
            for (auto *widget : QApplication::topLevelWidgets()) {
                if (auto *input = qobject_cast<QInputDialog *>(widget); input && input->isVisible()) {
                    ++choices;
                    if (accept) { input->setTextValue("beta.gsflib"); input->accept(); }
                    else input->reject();
                } else if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                    ++messages;
                    box->done(QMessageBox::No);
                }
            }
        });
        respond.start(5);
        const auto folder = QUrl::fromLocalFile(files.path()).toString();
        window.OpenGsfFolder(folder);
        QTRY_VERIFY_WITH_TIMEOUT(!window.importBusy, 5000);
        QCOMPARE(choices, 1);
        QVERIFY(!window.playbackEngine); // cancel does not guess a library
        accept = true;
        window.OpenGsfFolder(folder);
        QTRY_VERIFY_WITH_TIMEOUT(!window.importBusy, 5000);
        QCOMPARE(choices, 2);
        QCOMPARE(messages, 0);
        QVERIFY(window.playbackEngine);
        QCOMPARE(window.playlistWidget.listWidget.count(), 1);
        QCOMPARE(window.playlistWidget.listWidget.item(0)->text(), QString("Beta"));
        window.profile->dirty = false;
        QVERIFY(window.CloseGame());
        for (const QString name : {"alpha.gsflib", "01.minigsf", "02.minigsf"})
            QVERIFY(QFile::remove(files.path() + "/" + name));
        window.OpenGsfFolder(folder);
        QTRY_VERIFY_WITH_TIMEOUT(!window.importBusy, 5000);
        QVERIFY(window.playbackEngine); // a library without minis needs no playlist dialog
        QCOMPARE(window.playlistWidget.listWidget.count(), 0);
        QCOMPARE(choices, 2);
        QCOMPARE(messages, 0);
        window.profile->dirty = false;
        QVERIFY(window.CloseGame());
    }
    void gsfYesNoAndRecovery()
    {
        QTemporaryDir files;
        QVERIFY(files.isValid());
        const auto save = [&](const QString &name, const auto &bytes) {
            const std::filesystem::path path = (files.path() + "/" + name).toStdString();
            std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
            return path;
        };
        const auto library = save("fixture.gsflib", Fixture::Psf(Fixture::ScannableRom()));
        const auto mini = save("01.minigsf", Fixture::Psf({0}, "title=Prueba\n_lib=fixture.gsflib\n", 0x08000700));
        for (auto answer : {QMessageBox::No, QMessageBox::Yes}) {
            MainWindow window(nullptr, true);
            window.resize(384, 854);
            window.show();
            int questions = 0, errors = 0;
            QTimer respond;
            connect(&respond, &QTimer::timeout, &window, [&] {
                for (auto *widget : QApplication::topLevelWidgets()) {
                    if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                        if (box->windowTitle() == "Load Playlist from GSF set?") {
                            ++questions;
                            box->button(answer)->click();
                        } else { ++errors; box->accept(); }
                    }
                }
            });
            respond.start(5);
            window.OpenDocuments({library, mini});
            QCOMPARE(questions, 1);
            QCOMPARE(errors, 0);
            QVERIFY(window.playbackEngine);
            QTRY_VERIFY_WITH_TIMEOUT(!window.importBusy, 5000);
            QCOMPARE(window.playlistWidget.listWidget.count(), answer == QMessageBox::Yes ? 1 : 0);
            window.LoadSong("Prueba", 0);
            window.Play();
            settle();
            QVERIFY(window.playing);
            // A slow document provider cannot stop UI timer delivery.
            int heartbeats = 0;
            QTimer heartbeat;
            connect(&heartbeat, &QTimer::timeout, &window, [&] { ++heartbeats; });
            heartbeat.start(5);
            window.RunImport([](const std::function<bool()> &) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                return std::function<void()>{};
            });
            QTRY_VERIFY_WITH_TIMEOUT(!window.importBusy, 5000);
            QVERIFY(heartbeats >= 10);
            bool publishedAfterCancel = false;
            window.RunImport([&](const std::function<bool()> &) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                return std::function<void()>([&] { publishedAfterCancel = true; });
            });
            auto *cancel = window.importProgress->findChild<QPushButton *>();
            QVERIFY(cancel);
            QTest::mouseClick(cancel, Qt::LeftButton);
            QTRY_VERIFY_WITH_TIMEOUT(!window.importBusy, 5000);
            QVERIFY(!publishedAfterCancel);
            window.profile->dirty = false;
            QVERIFY(window.CloseGame());
            QVERIFY(!window.playbackEngine);
            const auto bad = save("broken.gsflib", std::vector<uint8_t>(40, 1));
            window.OpenDocuments({bad});
            QVERIFY(!window.playbackEngine);
            QCOMPARE(errors, 1);
            window.OpenDocuments({library, mini});
            QVERIFY(window.playbackEngine);
            QTRY_VERIFY_WITH_TIMEOUT(!window.importBusy, 5000);
            window.profile->dirty = false;
            QVERIFY(window.CloseGame());
        }
    }
    void coldPhoneStartAndFontChanges()
    {
        // Start at phone size (rather than first laying out a tablet), then
        // exercise both responsive breakpoints with larger platform fonts.
        for (const QSize initialSize : {QSize(360, 800), QSize(384, 854), QSize(412, 900), QSize(740, 360)}) {
            MainWindow window(nullptr, true);
            disconnect(&window, &MainWindow::OpenRomRequested, nullptr, nullptr);
            QSignalSpy open(&window, &MainWindow::OpenRomRequested);
            window.resize(initialSize);
            window.show();
            auto *tabs = window.findChild<QTabWidget *>("compactNavigation");
            QVERIFY(tabs);
            tabs->setCurrentIndex(3); // The ROM view in the Android report.
            auto *button = window.findChild<QPushButton *>("openRomButton");
            QVERIFY(button);
            // Native styles can require a taller action row than the 48px
            // touch minimum. It must still fit above transport and volume.
            button->setMinimumHeight(72);
            for (int fontSize : {12, 20, 12}) {
                auto font = window.font();
                font.setPointSize(fontSize);
                window.setFont(font);
                window.hide();
                window.show();
                for (const QSize size : {initialSize, QSize(612, 800), QSize(1012, 768), initialSize}) {
                    window.resize(size);
                    settle();
                    checkControls(window);
                    // Resolve the recipient from window coordinates: sending
                    // directly to a widget can click even a clipped button.
                    const QPoint point = button->mapTo(&window, button->rect().center());
                    auto *target = window.childAt(point);
                    QCOMPARE(target, button);
                    const int before = open.count();
                    QTest::mouseClick(target, Qt::LeftButton, Qt::NoModifier, target->mapFrom(&window, point));
                    QCOMPARE(open.count(), before + 1);
                }
            }
            capture(window, "phone-cold-" + QString::number(initialSize.width()));
        }
    }
    void resizingPreservesViewsAndState()
    {
        MainWindow window(nullptr, true);
        auto *touch = window.findChild<TouchPlayerLayout *>();
        auto *tabs = window.findChild<QTabWidget *>("compactNavigation");
        QVERIFY(touch && tabs);
        auto *songs = qobject_cast<SonglistWidget *>(tabs->widget(0));
        QVERIFY(songs);
        for (int i = 0; i < 40; ++i)
            songs->AddSong("Synthetic song " + std::to_string(i), static_cast<uint16_t>(i));
        songs->listWidget.setCurrentRow(12);
        auto *track = window.findChildren<TrackWidget *>().first();
        track->setMuted(true, true);
        window.show();
        const QList<QSize> sizes{{320, 640}, {360, 740}, {412, 840}, {740, 360}, {840, 700},
                                 {1024, 768}, {1280, 800}, {600, 900}, {320, 480}};
        for (int repeat = 0; repeat < 3; ++repeat) {
            for (auto size : sizes) {
                window.resize(size);
                settle();
                QCOMPARE(window.size(), size);
                QCOMPARE(touch->mode(), TouchPlayerLayout::modeForSize(touch->size()));
                checkControls(window);
                QCOMPARE(songs->listWidget.count(), 40);
                QCOMPARE(songs->listWidget.currentRow(), 12);
                QVERIFY(track->isMuted());
                if (touch->mode() == TouchPlayerLayout::Mode::Compact)
                    QCOMPARE(tabs->count(), 5);
            }
        }
        window.resize(360, 740);
        settle();
        capture(window, "phone-songs");
        tabs->setCurrentIndex(2);
        track->show();
        window.findChild<StatusWidget *>()->songWidget.titleLabel.setText("A very long synthetic song name that wraps across lines without widening the phone window");
        settle();
        auto *scroll = tabs->widget(2)->findChild<QScrollArea *>();
        QVERIFY(scroll);
        QVERIFY(scroll->widget()->width() <= scroll->viewport()->width());
        for (auto *button : track->findChildren<QPushButton *>()) {
            QVERIFY(button->isVisible());
            QVERIFY(track->rect().contains(QRect(button->mapTo(track, QPoint{}), button->size())));
        }
        capture(window, "phone-tracks");
        window.resize(740, 360);
        settle();
        checkControls(window);
        capture(window, "phone-landscape");
        window.resize(1024, 768);
        settle();
        QCOMPARE(touch->mode(), TouchPlayerLayout::Mode::TwoPane);
        capture(window, "tablet-two-pane");
        window.resize(1280, 800);
        settle();
        QCOMPARE(touch->mode(), TouchPlayerLayout::Mode::ThreePane);
        capture(window, "tablet-three-pane");
    }
    void hiddenTabDoesNotChangeTrackAudibility()
    {
        // Hiding an ancestor (tab switch) must not make active tracks vanish
        // from mute/solo calculations.
        StatusWidget status;
        auto tracks = status.findChildren<TrackWidget *>();
        tracks[0]->show();
        tracks[1]->show();
        QSignalSpy mute(&status, &StatusWidget::audibilityChanged);
        tracks[0]->setSolo(true);
        QVERIFY(mute.count() >= 2);
        QCOMPARE(mute.at(mute.count() - 2).at(1).toBool(), true);
        QCOMPARE(mute.last().at(1).toBool(), false);
    }
};

QTEST_MAIN(TestTouchUi)
#include "TestTouchUi.moc"
