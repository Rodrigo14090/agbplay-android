#include "DocumentAccess.hpp"
#include "Xcept.hpp"
#include <QStandardPaths>
#include <QUrl>
#include <QDir>

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

std::vector<DocumentAccess::FolderDocument> DocumentAccess::ListGsfFolder(const QString &directory)
{
    std::vector<FolderDocument> documents;
    const QUrl url(directory);
    if (url.scheme() != "content") {
        const QDir folder(url.isLocalFile() ? url.toLocalFile() : directory);
        if (!folder.exists()) throw Xcept("The selected GSF folder no longer exists");
        for (const auto &file : folder.entryInfoList(QDir::Files | QDir::Readable, QDir::Name)) {
            const auto ext = file.suffix().toLower();
            if (ext == "gsflib" || ext == "gsf" || ext == "minigsf")
                documents.push_back({QUrl::fromLocalFile(file.absoluteFilePath()).toString(), file.fileName()});
            if (documents.size() > 65536) throw Xcept("Too many GSF files in the selected folder");
        }
        return documents;
    }
#ifdef Q_OS_ANDROID
    QJniEnvironment env;
    const auto importer = env.findClass("org/agbplay/android/DocumentImporter");
    CheckJavaException(env);
    if (!importer) throw Xcept("Android DocumentImporter class not found");
    const auto method = env->GetStaticMethodID(importer, "listGsfDocuments",
        "(Landroid/content/Context;Ljava/lang/String;)[Ljava/lang/String;");
    CheckJavaException(env);
    if (!method) throw Xcept("Android directory importer method not found");
    const auto context = QNativeInterface::QAndroidApplication::context();
    const auto tree = QJniObject::fromString(directory);
    const auto result = static_cast<jobjectArray>(env->CallStaticObjectMethod(importer, method, context.object(), tree.object<jstring>()));
    CheckJavaException(env);
    const auto array = QJniObject::fromLocalRef(result);
    if (!array.isValid()) throw Xcept("Android directory provider returned no file list");
    const auto length = env->GetArrayLength(array.object<jobjectArray>());
    if (length % 2 != 0) throw Xcept("Invalid Android directory file list");
    for (jsize i = 0; i < length; i += 2) {
        const auto uri = QJniObject::fromLocalRef(env->GetObjectArrayElement(array.object<jobjectArray>(), i));
        const auto name = QJniObject::fromLocalRef(env->GetObjectArrayElement(array.object<jobjectArray>(), i + 1));
        CheckJavaException(env);
        documents.push_back({uri.toString(), name.toString()});
    }
    return documents;
#else
    throw Xcept("Android tree URIs require the Android document resolver");
#endif
}

std::filesystem::path DocumentAccess::LocalCopy(const QString &document, const QString &displayName)
{
    const QUrl url(document);
    if (url.scheme() != "content")
        return (url.isLocalFile() ? url.toLocalFile() : document).toStdString();
#ifdef Q_OS_ANDROID
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/imports";
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    const auto uri = QJniObject::fromString(document);
    const auto root = QJniObject::fromString(directory);
    const auto name = QJniObject::fromString(displayName);
    // Use raw JNI for the call so Java's actual exception reaches the error dialog
    // instead of being cleared by QJniObject and replaced with "Unknown error".
    QJniEnvironment env;
    const auto importer = env.findClass("org/agbplay/android/DocumentImporter");
    CheckJavaException(env);
    if (!importer) throw Xcept("Android DocumentImporter class not found");
    const bool knownName = !displayName.isEmpty();
    const auto method = env->GetStaticMethodID(importer, knownName ? "importDocumentWithName" : "importDocument",
        knownName ? "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"
                  : "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    CheckJavaException(env);
    if (!method) throw Xcept("Android DocumentImporter method not found");
    const auto result = knownName
        ? env->CallStaticObjectMethod(importer, method, context.object(), uri.object<jstring>(), root.object<jstring>(), name.object<jstring>())
        : env->CallStaticObjectMethod(importer, method, context.object(), uri.object<jstring>(), root.object<jstring>());
    CheckJavaException(env);
    const auto path = QJniObject::fromLocalRef(result).toString();
    if (path.isEmpty()) throw Xcept("Android document provider returned an empty import path");
    return path.toStdString();
#else
    (void)displayName;
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
