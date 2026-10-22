#include "DocumentAccess.hpp"
#include "Xcept.hpp"
#include <QStandardPaths>
#include <QUrl>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QCoreApplication>

namespace {
void CheckJavaException(QJniEnvironment &env)
{
    if (!env->ExceptionCheck()) return;
    const auto exception = env->ExceptionOccurred();
    env->ExceptionClear();
    const auto throwable = QJniObject::fromLocalRef(exception);
    const auto detail = throwable.callObjectMethod("toString", "()Ljava/lang/String;").toString();
    throw Xcept("Android document import failed: {}", detail.toStdString());
}
}
#endif

std::filesystem::path DocumentAccess::LocalCopy(const QString &document)
{
    const QUrl url(document);
    if (url.scheme() != "content")
        return (url.isLocalFile() ? url.toLocalFile() : document).toStdString();
#ifdef Q_OS_ANDROID
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/imports";
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    const auto uri = QJniObject::fromString(document);
    const auto root = QJniObject::fromString(directory);
    // Use raw JNI for the call so Java's actual exception reaches the error dialog
    // instead of being cleared by QJniObject and replaced with "Unknown error".
    QJniEnvironment env;
    const auto importer = env.findClass("org/agbplay/android/DocumentImporter");
    CheckJavaException(env);
    if (!importer) throw Xcept("Android DocumentImporter class not found");
    const auto method = env->GetStaticMethodID(importer, "importDocument",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    CheckJavaException(env);
    if (!method) throw Xcept("Android DocumentImporter method not found");
    const auto result = env->CallStaticObjectMethod(importer, method, context.object(),
                                                   uri.object<jstring>(), root.object<jstring>());
    CheckJavaException(env);
    const auto path = QJniObject::fromLocalRef(result).toString();
    if (path.isEmpty()) throw Xcept("Android document provider returned an empty import path");
    return path.toStdString();
#else
    throw Xcept("Android content URIs require the Android document resolver");
#endif
}

bool DocumentAccess::ShareFile(const QString &path)
{
#ifdef Q_OS_ANDROID
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    const auto authority = QJniObject::fromString("org.agbplay.android.qtprovider");
    const auto fileName = QJniObject::fromString(path);
    const QJniObject file("java/io/File", "(Ljava/lang/String;)V", fileName.object<jstring>());
    const auto uri = QJniObject::callStaticObjectMethod(
        "androidx/core/content/FileProvider", "getUriForFile",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/io/File;)Landroid/net/Uri;",
        context.object(), authority.object<jstring>(), file.object());
    if (!uri.isValid()) return false;
    const auto action = QJniObject::fromString("android.intent.action.SEND");
    QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V", action.object<jstring>());
    const auto type = QJniObject::fromString("audio/wav");
    intent.callObjectMethod("setType", "(Ljava/lang/String;)Landroid/content/Intent;", type.object<jstring>());
    const auto extra = QJniObject::fromString("android.intent.extra.STREAM");
    intent.callObjectMethod("putExtra", "(Ljava/lang/String;Landroid/os/Parcelable;)Landroid/content/Intent;",
                          extra.object<jstring>(), uri.object());
    intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", jint(1 | 0x10000000));
    const auto title = QJniObject::fromString("Share agbplay export");
    const auto chooser = QJniObject::callStaticObjectMethod("android/content/Intent", "createChooser",
        "(Landroid/content/Intent;Ljava/lang/CharSequence;)Landroid/content/Intent;", intent.object(), title.object());
    context.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", chooser.object());
    return true;
#else
    (void)path;
    return false;
#endif
}
