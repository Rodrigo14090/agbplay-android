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
