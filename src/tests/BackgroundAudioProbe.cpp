// Emulator-only end-to-end probe: real MainWindow, service, synthesis and QAudioSink.
#include "MainWindow.hpp"
#include "PlaybackEngine.hpp"
#include "SyntheticGsf.hpp"
#include "SettingsWindow.hpp"
#include "Settings.hpp"
#include "Gsf.hpp"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFile>
#include <QJniObject>
#include <bit>
#include <QApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <fstream>

class BackgroundAudioProbe {
    static inline int uiTaskRuns = 0;
    static inline bool wavActive = false, gsfActive = false;
public:
    static void start(MainWindow &window) {
        const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(directory);
        const auto file = (directory + "/background-fixture.gba").toStdString();
        auto rom = Fixture::ScannableRom(false);
        rom[0x50a] = 0xb2; // positive-time musical loop, not a busy GOTO
        Fixture::Put32(rom, 0x50b, 0x08000500);
        std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char *>(rom.data()), rom.size());
        const QJniObject context = QNativeInterface::QAndroidApplication::context();
        const auto intent = context.callObjectMethod("getIntent", "()Landroid/content/Intent;");
        const auto key = QJniObject::fromString("outputMode");
        const auto mode = intent.callObjectMethod("getStringExtra", "(Ljava/lang/String;)Ljava/lang/String;", key.object<jstring>()).toString();
        window.settings->androidAudioOutputMode = mode == "audiotrack" ? AudioOutputMode::MediaCompatibility : AudioOutputMode::Automatic;
        window.settings->playbackLoopIndefinitely = true;
        window.OpenDocuments({file});
        // Known 1 s float WAV for the exact same PCM transport and output route.
        std::vector<uint8_t> wav(44 + 48000 * 8);
        std::copy_n("RIFF", 4, wav.data()); Fixture::Put32(wav, 4, wav.size() - 8);
        std::copy_n("WAVEfmt ", 8, wav.data() + 8); Fixture::Put32(wav, 16, 16);
        wav[20] = 3; wav[22] = 2; Fixture::Put32(wav, 24, 48000); Fixture::Put32(wav, 28, 48000 * 8);
        wav[32] = 8; wav[34] = 32; std::copy_n("data", 4, wav.data() + 36); Fixture::Put32(wav, 40, 48000 * 8);
        for (size_t i = 0; i < 48000; ++i) {
            const float value = float(i % 48) / 96 - 0.25f;
            Fixture::Put32(wav, 44 + i * 8, std::bit_cast<uint32_t>(value));
            Fixture::Put32(wav, 48 + i * 8, std::bit_cast<uint32_t>(-value));
        }
        std::ofstream(directory.toStdString() + "/diagnostic.wav", std::ios::binary).write(reinterpret_cast<const char *>(wav.data()), wav.size());
        auto gsfRom = Fixture::ScannableRom();
        gsfRom[0x50a] = 0xb2; Fixture::Put32(gsfRom, 0x50b, 0x08000500);
        for (const auto &[name, bytes] : std::vector<std::pair<std::string, std::vector<uint8_t>>>{
            {"fixture.gsflib", Fixture::Psf(gsfRom)},
            {"01.minigsf", Fixture::Psf({0}, "title=Synthetic one\n_lib=fixture.gsflib\n")},
            {"02.minigsf", Fixture::Psf({1}, "title=Synthetic two\n_lib=fixture.gsflib\n")}})
            std::ofstream(directory.toStdString() + "/" + name, std::ios::binary).write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
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
                QFile command(directory + "/output-command.txt");
                if (command.open(QIODevice::ReadOnly)) {
                    const auto value = command.readAll().trimmed(); command.close(); command.remove();
                    if (value == "qt" || value == "audiotrack") {
                        const auto previous = window.settings->androidAudioOutputMode;
                        SettingsWindow options(&window, *window.settings);
                        auto *combo = options.findChild<QComboBox *>("androidAudioOutputMode");
                        if (!combo || combo->count() != 2) qFatal("Missing functional output selector");
                        combo->setCurrentIndex(value == "qt" ? 0 : 1);
                        options.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
                        window.ApplyAudioPreferences(previous);
                        Settings roundTrip; roundTrip.Load();
                        if (roundTrip.androidAudioOutputMode != window.settings->androidAudioOutputMode) qFatal("Output setting not persisted");
                    } else if (value == "wav") {
                        window.Stop();
                        window.playbackEngine->LoadDiagnosticWav(std::make_unique<DiagnosticWav>((directory + "/diagnostic.wav").toStdString(), 48000));
                        window.Play(); wavActive = true;
                    } else if (value == "gsf") {
                        const auto root = std::filesystem::path(directory.toStdString());
                        const auto songs = Gsf::ReadPlaylist({root / "01.minigsf", root / "02.minigsf"}, root / "fixture.gsflib", 4);
                        window.OpenDocuments({root / "fixture.gsflib"}, &songs);
                        window.LoadSong("Synthetic GSF song two", 1); window.Play(); gsfActive = true; wavActive = false;
                    } else if (value == "song") {
                        window.LoadSong("Synthetic background music", 0); window.Play(); wavActive = false;
                    } else if (value == "mute") window.playbackEngine->Mute(0, true);
                    else if (value == "unmute") window.playbackEngine->Mute(0, false);
                }
                double rms = 0;
                std::string error;
                if (window.playbackEngine) {
                    window.playbackEngine->PollOutputDiagnostics();
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
                result["diagnosticWav"] = wavActive; result["gsf"] = gsfActive;
                result["backend"] = window.playbackEngine && window.playbackEngine->OutputMode() == AudioOutputMode::MediaCompatibility ? "audiotrack" : "qt";
                const auto play = window.playButton.mapToGlobal(window.playButton.rect().center());
                result["playX"] = qRound(play.x() * window.devicePixelRatioF());
                result["playY"] = qRound(play.y() * window.devicePixelRatioF());
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
