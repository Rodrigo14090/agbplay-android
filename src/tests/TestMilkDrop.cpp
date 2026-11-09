#include "MilkDropPanel.hpp"
#include <QApplication>
#include <QGuiApplication>
#include <QImage>
#include <QTest>
#include <QDir>
#include <cmath>

class TestMilkDrop : public QObject
{
    Q_OBJECT
private slots:
    void nativeRenderingAndLifecycle()
    {
        MilkDropPanel panel;
        panel.resize(480, 640);
        bool enabled = false;
        uint64_t samples = 0;
        panel.SetSource([&](std::span<float> pcm) {
            for (size_t f=0; f<pcm.size()/2; ++f) {
                pcm[2*f] = float(0.5 * std::sin(double(samples++) * 6.28318530718 * 440 / 48000));
                pcm[2*f+1] = -pcm[2*f];
            }
            return VisualizationPcmTap::Result{pcm.size()/2, 0};
        }, [&](bool value) { enabled = value; });
        panel.show();
        panel.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        QVERIFY(QTest::qWaitForWindowActive(&panel));
        QTRY_VERIFY_WITH_TIMEOUT(panel.Diagnostics()["frames"].toInt() > 10, 20000);
        QTest::qWait(2500); // obtain an actual FPS/submit-time measurement
        QVERIFY(panel.Diagnostics()["fps"].toDouble() > 5);
        QVERIFY(enabled);
        QVERIFY(!panel.Diagnostics()["failed"].toBool());
        auto checkSize = [&] {
            const auto d = panel.Diagnostics();
            return d["width"].toInt() == qRound(d["logicalWidth"].toInt() * d["dpr"].toDouble()) &&
                   d["height"].toInt() == qRound(d["logicalHeight"].toInt() * d["dpr"].toDouble());
        };
        QTRY_VERIFY(checkSize());
        MilkDropWindow *surface = nullptr;
        for (auto *window : QGuiApplication::allWindows())
            if (window->objectName() == "milkDropSurface") surface = qobject_cast<MilkDropWindow *>(window);
        QVERIFY(surface);
        for (int preset=0; preset<3; ++preset) {
            panel.SelectPreset(preset);
            const auto before=panel.Diagnostics()["frames"].toInt();
            QTRY_VERIFY(panel.Diagnostics()["frames"].toInt() > before+5);
            const auto image=surface->grabFramebuffer();
            QVERIFY(!image.isNull());
            QCOMPARE(image.size(), QSize(panel.Diagnostics()["width"].toInt(), panel.Diagnostics()["height"].toInt()));
            bool nonBlack=false;
            for (int y=0; y<image.height() && !nonBlack; y+=2)
                for (int x=0; x<image.width(); x+=2)
                    if ((image.pixel(x,y) & 0xffffff) != 0) { nonBlack=true; break; }
            QVERIFY(nonBlack);
            const auto captures = qEnvironmentVariable("AGBPLAY_MILKDROP_CAPTURE_DIR");
            if (!captures.isEmpty()) {
                QDir().mkpath(captures);
                QVERIFY(image.save(captures + QString("/preset-%1.png").arg(preset)));
            }
            QCOMPARE(panel.Diagnostics()["presetErrors"].toInt(), 0);
        }
        panel.SetFullscreen(true);
        QTRY_VERIFY(panel.Diagnostics()["fullscreen"].toBool());
        QTest::qWait(1000); QTRY_VERIFY(checkSize());
        panel.SetFullscreen(false);
        QTRY_VERIFY(!panel.Diagnostics()["fullscreen"].toBool());
        QTRY_COMPARE(panel.size(), QSize(480,640));
        panel.hide(); QTest::qWait(300);
        const auto stopped=panel.Diagnostics()["frames"].toInt();
        QVERIFY(!enabled);
        QTest::qWait(500);
        QCOMPARE(panel.Diagnostics()["frames"].toInt(), stopped);
        panel.show();
        panel.activateWindow();
        QTRY_VERIFY(panel.Diagnostics()["frames"].toInt() > stopped+5);
        QVERIFY(enabled);
        qInfo() << "MilkDrop verified diagnostics:" << panel.Diagnostics();
        panel.SetSource({}, {});
    }
};
QTEST_MAIN(TestMilkDrop)
#include "TestMilkDrop.moc"
