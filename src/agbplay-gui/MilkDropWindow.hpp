#pragma once
#include "ProjectMRenderer.hpp"
#include "VisualizationPcmTap.hpp"
#include <QElapsedTimer>
#include <QJsonObject>
#include <QOpenGLWindow>
#include <QTimer>
#include <QOpenGLFramebufferObject>
#include <memory>
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
    void SetFiles(QStringList paths, QString textures);
    void SelectFile(int index);
    void Navigate(bool previous);
    void SetReducedResolution(bool enabled) { reducedResolution = enabled; }
    void SetFrameLimit(int limit); // 0 = automatic 60 -> 30 -> 20 -> 15
    QJsonObject Diagnostics() const;
signals:
    void runningChanged(bool running);
    void statisticsChanged(const QString &text);
    void presetChanged(int index);
protected:
    bool event(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void initializeGL() override;
    void paintGL() override;
    void exposeEvent(QExposeEvent *event) override;
private:
    void UpdateRunning();
    void ScheduleFrame();
    void Cleanup();
    bool EnsureContext();
    ProjectMRenderer renderer;
    Source readPcm;
    QByteArray preset;
    QStringList paths;
    QString texturePath;
    bool filesPending = false;
    int selectedFile = -1;
    int navigation = 0;
    int lastPosition = -1;
    QTimer nextFrame;
    QElapsedTimer clock, measurement;
    qint64 deadline = 0, renderNs = 0, maxRenderNs = 0;
    qint64 lastSubmitNs = 0;
    qint64 retryPresetAt = 0;
    quint64 frames = 0, intervalFrames = 0, skippedFrames = 0, pcmFrames = 0;
    int contexts = 0, presetErrors = 0;
    int frameLimit = 0, targetFps = 60;
    std::unique_ptr<QOpenGLFramebufferObject> scaleBuffer;
    bool reducedResolution = false;
    QSize framebuffer;
    QSize viewport;
    QSize renderResolution;
    double actualFps = 0;
    bool enabled = false, running = false, initialized = false, failed = false, presetPending = false;
    bool nativeSizeQueried = false;
};
