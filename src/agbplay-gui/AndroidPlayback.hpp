#pragma once
#include <QObject>
#ifdef Q_OS_ANDROID
// Android control plane only. JNI never runs on the PCM callback or synthesis thread.
class AndroidPlayback final : public QObject {
    Q_OBJECT
public:
    enum Command { Play = 1, Pause, Stop, Next, Previous, Failed };
    explicit AndroidPlayback(QObject *parent);
    ~AndroidPlayback() override;
    void play(const QString &title);
    void pause();
    void stop();
    void title(const QString &title);
    void dispatch(int value, qint64 generation);
signals:
    void command(int value);
    void error(const QString &message);
private:
    bool checkException();
    bool ready = false;
    qint64 generation = 0;
};
#endif
