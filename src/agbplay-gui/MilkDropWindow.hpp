#pragma once
#include "ProjectMRenderer.hpp"
#include "VisualizationPcmTap.hpp"
#include <QElapsedTimer>
#include <QJsonObject>
#include <QOpenGLWindow>
#include <QTimer>
#include <functional>

class MilkDropWindow : public QOpenGLWindow
{
    Q_OBJECT
public:
    MilkDropWindow();
    ~MilkDropWindow() override;
    using Source = std::function<VisualizationPcmTap::Result(std::span<float>)>;
    void SetSource(Source source) { readPcm = std::move(source); }
    void SetEnabled(bool value);
    void SetPreset(QByteArray data);
    void SetFrameLimit(int limit); // 0 = automatic 60 -> 30 -> 20 -> 15
    QJsonObject Diagnostics() const;
signals:
    void runningChanged(bool running);
    void statisticsChanged(const QString &text);
protected:
    void initializeGL() override;
    void paintGL() override;
    void exposeEvent(QExposeEvent *event) override;
private:
    void UpdateRunning();
    void ScheduleFrame();
    void Cleanup();
    ProjectMRenderer renderer;
    Source readPcm;
    QByteArray preset;
    QTimer nextFrame;
    QElapsedTimer clock, measurement;
    qint64 deadline = 0, renderNs = 0, maxRenderNs = 0;
    quint64 frames = 0, intervalFrames = 0, skippedFrames = 0, pcmFrames = 0;
    int contexts = 0, presetErrors = 0;
    int frameLimit = 0, targetFps = 60;
    QSize framebuffer;
    QSize viewport;
    double actualFps = 0;
    bool enabled = false, running = false, initialized = false, failed = false, presetPending = false;
};
