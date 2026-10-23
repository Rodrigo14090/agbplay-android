#include "AndroidPlayback.hpp"
#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QMutex>
#include <QMutexLocker>
#include <atomic>
#include "Debug.hpp"
namespace {
QMutex bridgeMutex;
AndroidPlayback *bridge = nullptr;
std::atomic<qint64> nextGeneration{0};
constexpr auto service = "org/agbplay/android/PlaybackService";
void NativeCommand(JNIEnv *, jclass, jint value, jlong generation) {
    QMutexLocker lock(&bridgeMutex);
    if (bridge) QMetaObject::invokeMethod(bridge, [target = bridge, value, generation] { target->dispatch(value, generation); }, Qt::QueuedConnection);
}
QJniObject context() { return QNativeInterface::QAndroidApplication::context(); }
}
AndroidPlayback::AndroidPlayback(QObject *parent) : QObject(parent) {
    QJniEnvironment env;
    const auto clazz = env.findClass(service);
    JNINativeMethod method{const_cast<char *>("nativeCommand"), const_cast<char *>("(IJ)V"), reinterpret_cast<void *>(NativeCommand)};
    if (!clazz || env->RegisterNatives(clazz, &method, 1) != JNI_OK) {
        checkException();
        return;
    }
    { QMutexLocker lock(&bridgeMutex); bridge = this; }
    QJniObject::callStaticMethod<void>(service, "setNativeReady", "(Z)V", jboolean(true));
    ready = true;
}
AndroidPlayback::~AndroidPlayback() {
    stop();
    QJniObject::callStaticMethod<void>(service, "setNativeReady", "(Z)V", jboolean(false));
    QMutexLocker lock(&bridgeMutex);
    bridge = nullptr;
}
bool AndroidPlayback::checkException() {
    QJniEnvironment env;
    if (!env->ExceptionCheck()) return false;
    auto exception = env->ExceptionOccurred(); env->ExceptionClear();
    const auto detail = QJniObject::fromLocalRef(exception).callObjectMethod("toString", "()Ljava/lang/String;").toString();
    emit error(detail);
    return true;
}
void AndroidPlayback::play(const QString &title) {
    generation = ++nextGeneration;
    if (!ready) { emit error("Cannot initialize the Android playback service"); return; }
    // Raw JNI preserves startup exceptions (including background-start restrictions).
    QJniEnvironment env;
    auto clazz = env.findClass(service);
    if (checkException() || !clazz) return;
    auto method = env->GetStaticMethodID(clazz, "play", "(Landroid/content/Context;Ljava/lang/String;J)V");
    if (checkException() || !method) return;
    auto text = QJniObject::fromString(title);
    env->CallStaticVoidMethod(clazz, method, context().object(), text.object<jstring>(), static_cast<jlong>(generation));
    checkException();
}
void AndroidPlayback::pause() {
    generation = ++nextGeneration;
    if (ready) QJniObject::callStaticMethod<void>(service, "pause", "(J)V", static_cast<jlong>(generation));
}
void AndroidPlayback::stop() {
    generation = ++nextGeneration;
    if (ready) QJniObject::callStaticMethod<void>(service, "stop", "(Landroid/content/Context;J)V", context().object(), static_cast<jlong>(generation));
}
void AndroidPlayback::dispatch(int value, qint64 received) {
    if (received != generation) {
        Debug::print("Android transport: discard stale command {} (generation {} != {})", value, received, generation);
        return;
    }
    emit command(value);
}
void AndroidPlayback::title(const QString &title) {
    if (!ready) return;
    QJniObject::callStaticMethod<void>(service, "title", "(Ljava/lang/String;)V", QJniObject::fromString(title).object<jstring>());
}
#endif
