#include "MainWindow.hpp"
#include "TouchPlayerLayout.hpp"

#include <QAction>
#include <QDir>
#include <QMenu>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

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
    void openActionAndPlaylistStayDistinct()
    {
        MainWindow window(nullptr, true);
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
        QCOMPARE(menu->menu()->actions().size(), 5); // original four menus + Jump
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
