#include "DocumentAccess.hpp"
#include "Xcept.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

std::filesystem::path DocumentAccess::LocalCopy(const QString &document)
{
    const QUrl url(document);
    if (url.scheme() != "content")
        return (url.isLocalFile() ? url.toLocalFile() : document).toStdString();
    QFile input(document);
    if (!input.open(QIODevice::ReadOnly))
        throw Xcept("Cannot read selected document: {}", input.errorString().toStdString());
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/imports";
    if (!QDir().mkpath(directory))
        throw Xcept("Cannot create import directory");
    const auto key = QCryptographicHash::hash(document.toUtf8(), QCryptographicHash::Sha256).toHex();
    const auto suffix = QFileInfo(input).suffix();
    const QString destination = directory + '/' + QString::fromLatin1(key) + '.' + suffix;
    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly))
        throw Xcept("Cannot create private document copy");
    // ZIP/GSF may exceed raw ROM size; cap imports to 64 MiB before any parser allocates.
    constexpr qint64 limit = 64 * 1024 * 1024;
    qint64 total = 0;
    while (!input.atEnd()) {
        const auto chunk = input.read(65536);
        if (chunk.isEmpty() && input.error() != QFile::NoError)
            throw Xcept("Document read failed");
        total += chunk.size();
        if (total > limit)
            throw Xcept("Document exceeds 64 MiB import limit");
        if (output.write(chunk) != chunk.size())
            throw Xcept("Document copy failed (storage full?)");
    }
    if (!output.commit())
        throw Xcept("Cannot finish document import");
    return destination.toStdString();
}

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QCoreApplication>

#endif

bool DocumentAccess::ShareFile(const QString &path)
{
#ifdef Q_OS_ANDROID
    const auto context = QNativeInterface::QAndroidApplication::context();
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
