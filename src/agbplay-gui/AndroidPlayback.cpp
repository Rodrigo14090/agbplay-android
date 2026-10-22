#include "AndroidPlayback.hpp"
#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QMutex>
#include <QMutexLocker>
namespace {
QMutex bridgeMutex;
AndroidPlayback *bridge = nullptr;
constexpr auto service = "org/agbplay/android/PlaybackService";
void NativeCommand(JNIEnv *, jclass, jint value) {
    QMutexLocker lock(&bridgeMutex);
    if (bridge) QMetaObject::invokeMethod(bridge, [target = bridge, value] { emit target->command(value); }, Qt::QueuedConnection);
}
QJniObject context() { return QNativeInterface::QAndroidApplication::context(); }
}
AndroidPlayback::AndroidPlayback(QObject *parent) : QObject(parent) {
    QJniEnvironment env;
    const auto clazz = env.findClass(service);
    JNINativeMethod method{const_cast<char *>("nativeCommand"), const_cast<char *>("(I)V"), reinterpret_cast<void *>(NativeCommand)};
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
    if (!ready) { emit error("Cannot initialize the Android playback service"); return; }
    // Raw JNI preserves startup exceptions (including background-start restrictions).
    QJniEnvironment env;
    auto clazz = env.findClass(service);
    if (checkException() || !clazz) return;
    auto method = env->GetStaticMethodID(clazz, "play", "(Landroid/content/Context;Ljava/lang/String;)V");
    if (checkException() || !method) return;
    auto text = QJniObject::fromString(title);
    env->CallStaticVoidMethod(clazz, method, context().object(), text.object<jstring>());
    checkException();
}
void AndroidPlayback::pause() { if (ready) QJniObject::callStaticMethod<void>(service, "pause", "()V"); }
void AndroidPlayback::stop() { if (ready) QJniObject::callStaticMethod<void>(service, "stop", "(Landroid/content/Context;)V", context().object()); }
void AndroidPlayback::title(const QString &title) {
    if (!ready) return;
    QJniObject::callStaticMethod<void>(service, "title", "(Ljava/lang/String;)V", QJniObject::fromString(title).object<jstring>());
}
#endif
