#include "MilkDropWindow.hpp"
#include "FrameCadence.hpp"
#include <QGuiApplication>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <QDebug>
#include <QResizeEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#ifdef Q_OS_ANDROID
#include <EGL/egl.h>
#endif

MilkDropWindow::MilkDropWindow() : QOpenGLWindow(NoPartialUpdate)
{
    QSurfaceFormat format;
#ifdef Q_OS_ANDROID
    format.setRenderableType(QSurfaceFormat::OpenGLES);
    format.setVersion(3, 0);
#else
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setVersion(3, 3);
#endif
    format.setDepthBufferSize(0);
    format.setStencilBufferSize(0);
    format.setSamples(0);
    format.setSwapInterval(1);
    setFormat(format);
    setObjectName("milkDropSurface");
    nextFrame.setSingleShot(true);
    nextFrame.setTimerType(Qt::PreciseTimer);
    connect(&nextFrame, &QTimer::timeout, this, [this] { if (running) update(); });
    connect(this, &QOpenGLWindow::frameSwapped, this, &MilkDropWindow::ScheduleFrame);
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this] { UpdateRunning(); });
    clock.start();
    measurement.start();
}
MilkDropWindow::~MilkDropWindow() { nextFrame.stop(); Cleanup(); }
void MilkDropWindow::SetEnabled(bool value) { enabled = value; UpdateRunning(); }
void MilkDropWindow::SetPreset(QByteArray data) { preset = std::move(data); presetPending = true; }
void MilkDropWindow::SetFrameLimit(int limit)
{
    frameLimit = limit == 30 ? 30 : limit == 60 ? 60 : 0;
    targetFps = frameLimit ? frameLimit : 60;
    deadline = 0;
}
void MilkDropWindow::UpdateRunning()
{
    const bool value = enabled && isExposed() && qGuiApp->applicationState() == Qt::ApplicationActive && !failed;
    if (running == value) return;
    running = value;
    deadline = 0;
    lastSubmitNs = 0;
    intervalFrames = 0; renderNs = maxRenderNs = 0;
    measurement.restart();
    nextFrame.stop();
    emit runningChanged(running);
    if (running) update();
}
void MilkDropWindow::exposeEvent(QExposeEvent *event)
{
    UpdateRunning();
    if (running && EnsureContext()) QOpenGLWindow::exposeEvent(event);
}
void MilkDropWindow::resizeEvent(QResizeEvent *)
{
    // QOpenGLWindow's default resize handler eagerly initializes GL, even for
    // hidden tab pages. Defer it until an exposed frame; paintGL sizes projectM.
    if (running) update();
}
bool MilkDropWindow::EnsureContext()
{
    if (!initialized && !failed) {
        // This protected Qt handler initializes the actual window context
        // without beginPaint's unconditional GL calls. Reject a failed context
        // before Qt's painter can dereference its unavailable function table.
        QResizeEvent initial(size(), size());
        QOpenGLWindow::resizeEvent(&initial);
    }
    return initialized && !failed && context() && context()->isValid();
}
bool MilkDropWindow::event(QEvent *event)
{
    if (event->type() == QEvent::UpdateRequest || event->type() == QEvent::Paint) {
        UpdateRunning();
        if (!running || !EnsureContext()) return true;
        // Native expose/paint requests must respect the same budget as our
        // timer. Otherwise they could bypass the yield after a slow frame.
        const auto remaining = deadline - clock.nsecsElapsed();
        if (remaining > 0) {
            if (!nextFrame.isActive()) nextFrame.start(static_cast<int>((remaining + 999999) / 1000000));
            return true;
        }
    }
    return QOpenGLWindow::event(event);
}
void MilkDropWindow::initializeGL()
{
    try {
        if (!context() || !context()->isValid() || QOpenGLContext::currentContext() != context())
            throw std::runtime_error("MilkDrop: no usable OpenGL context; audio remains available");
        const auto actual = context()->format();
        if (actual.majorVersion() < 3 || (!context()->isOpenGLES() && actual.majorVersion() == 3 && actual.minorVersion() < 3))
            throw std::runtime_error("MilkDrop requires GLES 3.0 / OpenGL 3.3");
        if (defaultFramebufferObject() != 0) throw std::runtime_error("projectM requires a window framebuffer");
        auto *gl = context()->functions();
        qInfo() << "MilkDrop projectM 4.1.8:" << reinterpret_cast<const char *>(gl->glGetString(GL_VENDOR))
                << reinterpret_cast<const char *>(gl->glGetString(GL_RENDERER))
                << reinterpret_cast<const char *>(gl->glGetString(GL_VERSION));
        renderer.Create();
        ++contexts;
        initialized = true;
        presetPending = true;
        connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &MilkDropWindow::Cleanup, Qt::DirectConnection);
    } catch (const std::exception &error) {
        failed = true;
        qWarning() << "MilkDrop initialization:" << error.what();
        emit statisticsChanged(QString::fromUtf8(error.what()));
        UpdateRunning();
    }
}
void MilkDropWindow::Cleanup()
{
    if (!initialized) return;
    // Android may already have destroyed the window's EGLSurface. A tiny
    // offscreen surface lets the same context delete its GL objects without
    // waiting for that hidden/destroyed native window to reappear.
    QOffscreenSurface cleanupSurface(context()->screen());
    cleanupSurface.setFormat(context()->format());
    cleanupSurface.create();
    if (!context()->makeCurrent(&cleanupSurface) && isExposed()) makeCurrent();
    renderer.Destroy();
    context()->doneCurrent();
    initialized = false;
    disconnect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &MilkDropWindow::Cleanup);
}
void MilkDropWindow::paintGL()
{
    if (!initialized || !running) return;
    QElapsedTimer work; work.start();
    try {
        QSize nextSize{qRound(width() * devicePixelRatio()), qRound(height() * devicePixelRatio())};
#ifdef Q_OS_ANDROID
        EGLint nativeWidth = 0, nativeHeight = 0;
        const auto display = eglGetCurrentDisplay();
        const auto draw = eglGetCurrentSurface(EGL_DRAW);
        if (display != EGL_NO_DISPLAY && draw != EGL_NO_SURFACE &&
            eglQuerySurface(display, draw, EGL_WIDTH, &nativeWidth) &&
            eglQuerySurface(display, draw, EGL_HEIGHT, &nativeHeight) && nativeWidth > 0 && nativeHeight > 0) {
            nextSize = {nativeWidth, nativeHeight};
            nativeSizeQueried = true;
        }
#endif
        const bool resized = framebuffer != nextSize;
        framebuffer = nextSize;
        if (presetPending) { renderer.Load(preset.toStdString()); presetPending = false; }
        std::array<float, VisualizationPcmTap::Frames * 2> pcm{};
        const auto result = readPcm ? readPcm(pcm) : VisualizationPcmTap::Result{};
        pcmFrames += result.frames;
        skippedFrames += result.skippedFrames;
        // No publication during pause: feed silence instead of repeating stale PCM.
        renderer.Render(pcm, framebuffer.width(), framebuffer.height(), targetFps);
        if (resized || frames == 0) {
            GLint actual[4]{};
            context()->functions()->glGetIntegerv(GL_VIEWPORT, actual);
            viewport = {actual[2], actual[3]};
            qInfo() << "MilkDrop native framebuffer:" << framebuffer << "GL viewport:" << viewport
                    << "DPR:" << devicePixelRatio() << "FBO:" << defaultFramebufferObject();
        }
        if (auto error = renderer.TakeError(); !error.empty()) {
            ++presetErrors;
            qWarning() << "MilkDrop preset:" << QString::fromStdString(error);
            emit statisticsChanged(QString::fromStdString(error));
        }
    } catch (const std::exception &error) {
        failed = true;
        qWarning() << "MilkDrop render:" << error.what();
        emit statisticsChanged(QString::fromUtf8(error.what()));
        UpdateRunning();
        return;
    }
    ++frames; ++intervalFrames;
    const auto elapsed = work.nsecsElapsed();
    lastSubmitNs = elapsed;
    renderNs += elapsed; maxRenderNs = std::max(maxRenderNs, elapsed);
    if (measurement.elapsed() >= 2000) {
        actualFps = double(intervalFrames) * 1e9 / double(measurement.nsecsElapsed());
        const double averageMs = double(renderNs) / double(intervalFrames) / 1e6;
        if (!frameLimit && (actualFps < targetFps * 0.7 || averageMs > 1000.0 / targetFps * 0.85))
            targetFps = FrameCadence::LowerRate(targetFps);
        const auto text = QString("%1×%2 · %3 FPS / %4 · CPU submit %5 ms · visual drop %6")
            .arg(framebuffer.width()).arg(framebuffer.height()).arg(actualFps, 0, 'f', 1)
            .arg(targetFps).arg(averageMs, 0, 'f', 1).arg(skippedFrames);
        qInfo().noquote() << "MilkDrop:" << text << "max submit ms=" << double(maxRenderNs) / 1e6;
        emit statisticsChanged(text);
        measurement.restart(); intervalFrames = 0; renderNs = maxRenderNs = 0;
    }
}
void MilkDropWindow::ScheduleFrame()
{
    if (!running) return;
    const auto now = clock.nsecsElapsed();
    deadline = FrameCadence::NextDeadline(deadline, now, targetFps, lastSubmitNs);
    nextFrame.start(static_cast<int>(std::max<qint64>(0, (deadline - now + 999999) / 1000000)));
}
QJsonObject MilkDropWindow::Diagnostics() const
{
    return {{"frames", double(frames)}, {"pcmFrames", double(pcmFrames)}, {"skippedVisualFrames", double(skippedFrames)},
        {"contexts", contexts}, {"presetErrors", presetErrors},
        {"eglSurfaceSize", nativeSizeQueried},
        {"running", running}, {"initialized", initialized}, {"failed", failed}, {"fps", actualFps},
        {"targetFps", targetFps}, {"width", framebuffer.width()}, {"height", framebuffer.height()},
        {"viewportWidth", viewport.width()}, {"viewportHeight", viewport.height()},
        {"logicalWidth", width()}, {"logicalHeight", height()}, {"dpr", devicePixelRatio()}};
}
