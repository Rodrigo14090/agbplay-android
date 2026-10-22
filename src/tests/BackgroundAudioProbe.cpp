// Emulator-only end-to-end probe: real MainWindow, service, synthesis and QAudioSink.
#include "MainWindow.hpp"
#include "PlaybackEngine.hpp"
#include "SyntheticGsf.hpp"
#include <QApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <fstream>

class BackgroundAudioProbe {
    static inline int uiTaskRuns = 0;
public:
    static void start(MainWindow &window) {
        const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(directory);
        const auto file = (directory + "/background-fixture.gba").toStdString();
        auto rom = Fixture::ScannableRom(false);
        rom[0x50a] = 0xb2; // positive-time musical loop, not a busy GOTO
        Fixture::Put32(rom, 0x50b, 0x08000500);
        std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char *>(rom.data()), rom.size());
        window.OpenDocuments({file});
        window.statusUpdateTimer.stop(); // probe consumes diagnostics, not the visualizer timer
        if (window.playbackEngine) {
            window.playbackEngine->SetMaxLoops(-1);
            window.LoadSong("Synthetic background music", 0);
            window.Play();
        }
        QObject::connect(qApp, &QGuiApplication::applicationStateChanged, &window,
            [&window, queued = false](Qt::ApplicationState state) mutable {
                if (state != Qt::ApplicationActive && !queued) {
                    queued = true;
                    window.RunWhenActive([] { ++uiTaskRuns; });
                }
            });
        auto *report = new QTimer(&window);
        QObject::connect(report, &QTimer::timeout, &window,
            [&window, directory, frames = uint64_t(0), callbacks = uint64_t(0), underruns = uint64_t(0), serial = 0]() mutable {
                double rms = 0;
                std::string error;
                if (window.playbackEngine) {
                    const auto d = window.playbackEngine->TakeAudioDiagnostics();
                    frames += d.requestedFrames;
                    callbacks += d.callbackCount;
                    underruns += d.queue.underrunCallbacks;
                    rms = d.outputRms;
                    error = window.playbackEngine->TakeError();
                }
                const QJsonObject state{{"sequence", ++serial}, {"engine", bool(window.playbackEngine)},
                    {"playing", window.playing}, {"applicationState", int(qApp->applicationState())},
                    {"frames", double(frames)}, {"callbacks", double(callbacks)}, {"underruns", double(underruns)},
                    {"rms", rms}, {"error", QString::fromStdString(error)}};
                QJsonObject result = state;
                result["uiTaskRuns"] = uiTaskRuns;
                QSaveFile file(directory + "/background-probe.json");
                if (file.open(QIODevice::WriteOnly)) { file.write(QJsonDocument(result).toJson()); file.commit(); }
            });
        report->start(500);
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("agbplay");
    QCoreApplication::setApplicationName("agbplay-background-probe");
    MainWindow window;
    window.show();
    QTimer::singleShot(1000, &window, [&] { BackgroundAudioProbe::start(window); });
    return app.exec();
}
