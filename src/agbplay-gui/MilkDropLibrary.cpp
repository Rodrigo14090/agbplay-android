#include "MilkDropLibrary.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <zip.h>
#include <array>
#include <memory>
#include <stdexcept>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#endif

namespace {
struct Pack { const char *repo, *commit, *hash, *folder; bool texture; };
constexpr Pack packs[] = {
    {"presets-milkdrop-original", "e03b83e3338d8f1ed6cbcf908c719f249ef24288", "4479bbd10e4f4a005d636f7b0ed26004042541a62a925ca97e12ff13765b688d", "original", false},
    {"presets-cream-of-the-crop", "0180df21f5e0bd39b9060cc5de420ed2f1f9e509", "5600f974497d8f73295393fbdf866895ceb89efe82892f5e2d019358b2659202", "cream", false},
    {"presets-milkdrop-texture-pack", "6368812f27bc747b517218fbf89d21d59afce4d9", "38e93612f79a313830480d7458d8eb065d2b67992211a5dd0c92ef87b8607f6a", "textures", true},
};
constexpr qint64 maxArchive = 48 * 1024 * 1024;
constexpr quint64 maxExtracted = 160 * 1024 * 1024;
void CheckCancel() { if (QThread::currentThread()->isInterruptionRequested()) throw std::runtime_error("Cancelado"); }
void Download(const Pack &pack, const QString &destination)
{
    const QUrl url(QString("https://codeload.github.com/projectM-visualizer/%1/zip/%2").arg(pack.repo, pack.commit));
#ifdef Q_OS_ANDROID
    const auto address = QJniObject::fromString(url.toString());
    const auto path = QJniObject::fromString(destination);
    const auto result = QJniObject::callStaticObjectMethod("org/agbplay/android/MilkDropDownloads", "download",
        "(Ljava/lang/String;Ljava/lang/String;J)Ljava/lang/String;", address.object<jstring>(),
        path.object<jstring>(), static_cast<jlong>(maxArchive));
    QJniEnvironment env;
    if (env->ExceptionCheck()) { env->ExceptionClear(); throw std::runtime_error("Error JNI al descargar presets"); }
    CheckCancel();
    if (!result.isValid() || !result.toString().isEmpty()) throw std::runtime_error("Falló la descarga HTTPS de presets");
    QFile file(destination);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("No se puede leer la descarga");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file) || hash.result().toHex() != pack.hash)
        throw std::runtime_error("La descarga no supera la verificación SHA-256");
#else
    QNetworkAccessManager network;
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto *reply = network.get(request);
    QFile file(destination);
    if (!file.open(QIODevice::WriteOnly)) throw std::runtime_error("No se puede guardar la descarga");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 total = 0;
    bool bad = false;
    QObject::connect(reply, &QIODevice::readyRead, reply, [&] {
        const QByteArray bytes = reply->readAll();
        total += bytes.size();
        if (total > maxArchive || file.write(bytes) != bytes.size()) { bad = true; reply->abort(); return; }
        hash.addData(bytes);
    });
    QEventLoop loop;
    QTimer timer; timer.setInterval(1000);
    int seconds = 0;
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        if (++seconds > 180 || QThread::currentThread()->isInterruptionRequested()) reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(); loop.exec();
    CheckCancel();
    if (bad || reply->error() != QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
        throw std::runtime_error("Falló la descarga HTTPS de presets");
    if (hash.result().toHex() != pack.hash) throw std::runtime_error("La descarga no supera la verificación SHA-256");
#endif
}
void Extract(const Pack &pack, const QString &archive, const QString &root, quint64 &extracted)
{
    int error = 0;
    std::unique_ptr<zip_t, decltype(&zip_discard)> zip(zip_open(QFile::encodeName(archive).constData(), ZIP_RDONLY | ZIP_CHECKCONS, &error), zip_discard);
    if (!zip) throw std::runtime_error("Archivo ZIP inválido");
    const auto entries = zip_get_num_entries(zip.get(), 0);
    if (entries < 0 || entries > 12000) throw std::runtime_error("ZIP con demasiados archivos");
    for (zip_int64_t i = 0; i < entries; ++i) {
        CheckCancel();
        zip_stat_t stat; zip_stat_init(&stat);
        if (zip_stat_index(zip.get(), zip_uint64_t(i), 0, &stat) != 0 || !stat.name) throw std::runtime_error("Índice ZIP inválido");
        QString name = QString::fromUtf8(stat.name);
        if (name.contains('\\') || name.startsWith('/') || name.contains(QChar::Null)) throw std::runtime_error("Ruta ZIP inválida");
        const auto parts = name.split('/');
        for (const auto &part : parts) if (part == ".." || part == "." || part.contains(':')) throw std::runtime_error("Ruta ZIP insegura");
        if (parts.size() < 2 || name.endsWith('/')) continue;
        zip_uint8_t os = 0; zip_uint32_t attributes = 0;
        if (zip_file_get_external_attributes(zip.get(), zip_uint64_t(i), 0, &os, &attributes) != 0)
            throw std::runtime_error("Atributos ZIP inválidos");
        if (os == ZIP_OPSYS_UNIX && ((attributes >> 16) & 0170000) == 0120000) throw std::runtime_error("Enlace ZIP rechazado");
        const QString relative = parts.mid(1).join('/');
        const QString suffix = QFileInfo(relative).suffix().toLower();
        if ((!pack.texture && suffix != "milk") || (pack.texture && suffix != "jpg" && suffix != "png")) continue;
        if (relative.size() > 512 || stat.size > (pack.texture ? 2 * 1024 * 1024u : 256 * 1024u)) throw std::runtime_error("Archivo ZIP demasiado grande");
        extracted += stat.size;
        if (extracted > maxExtracted) throw std::runtime_error("ZIP descomprimido demasiado grande");
        QString out = root + '/' + pack.folder + '/' + relative;
        // The texture archive stores textures/ under its root; avoid a doubled directory.
        if (pack.texture && relative.startsWith("textures/")) out = root + '/' + relative;
        QDir().mkpath(QFileInfo(out).path());
        QFile file(out);
        if (file.exists() || !file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) throw std::runtime_error("Destino ZIP duplicado o inaccesible");
        std::unique_ptr<zip_file_t, decltype(&zip_fclose)> input(zip_fopen_index(zip.get(), zip_uint64_t(i), 0), zip_fclose);
        if (!input) throw std::runtime_error("Archivo ZIP ilegible");
        std::array<char, 65536> buffer{};
        quint64 written = 0;
        for (;;) {
            const auto n = zip_fread(input.get(), buffer.data(), buffer.size());
            if (n < 0) throw std::runtime_error("Error al descomprimir ZIP");
            if (!n) break;
            written += quint64(n);
            if (written > stat.size || file.write(buffer.data(), n) != n) throw std::runtime_error("Error al guardar ZIP");
            CheckCancel();
        }
        if (written != stat.size) throw std::runtime_error("Archivo ZIP truncado");
    }
}
}
MilkDropLibrary::MilkDropLibrary(QObject *parent) : QObject(parent) {}
MilkDropLibrary::~MilkDropLibrary() {
    if (worker) {
        worker->requestInterruption();
#ifdef Q_OS_ANDROID
        QJniObject::callStaticMethod<void>("org/agbplay/android/MilkDropDownloads", "cancel", "()V");
#endif
        worker->wait(); delete worker;
    }
}
void MilkDropLibrary::Start(const QString &root)
{
    if (worker) return;
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>("org/agbplay/android/MilkDropDownloads", "prepare", "()V");
#endif
    worker = QThread::create([this, root] {
        auto report = [this](QString text) { QMetaObject::invokeMethod(this, [this, text] { emit status(text); }); };
        try {
            QDir().mkpath(root);
            const QString cache = root + "/collections-v1";
            if (!QFile::exists(cache + "/complete")) {
                QTemporaryDir stage(root + "/download-XXXXXX");
                if (!stage.isValid()) throw std::runtime_error("No se puede crear el almacenamiento temporal");
                quint64 extracted = 0;
                for (const auto &pack : packs) {
                    CheckCancel();
                    report(QString("Descargando %1…").arg(pack.repo));
                    const auto archive = stage.path() + "/archive.zip";
                    Download(pack, archive);
                    report(QString("Instalando %1…").arg(pack.repo));
                    Extract(pack, archive, stage.path(), extracted);
                    QFile::remove(archive);
                }
                QFile marker(stage.path() + "/complete");
                if (!marker.open(QIODevice::WriteOnly | QIODevice::NewOnly) || marker.write("v1\n") != 3) throw std::runtime_error("No se pudo finalizar la instalación");
                marker.close();
                if (QFile::exists(cache)) QDir(cache).removeRecursively();
                if (!QDir().rename(stage.path(), cache)) throw std::runtime_error("No se pudo activar la colección");
                stage.setAutoRemove(false);
            }
            report("Indexando presets…");
            QStringList paths;
            QDirIterator it(cache, {"*.milk"}, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
            while (it.hasNext()) { CheckCancel(); paths.append(it.next()); }
            if (paths.isEmpty()) throw std::runtime_error("La colección no contiene presets");
            QMetaObject::invokeMethod(this, [this, paths, cache] { emit ready(paths, cache + "/textures"); });
            report(QString("%1 presets disponibles sin conexión").arg(paths.size()));
        } catch (const std::exception &e) { report(QString("MilkDrop: %1").arg(QString::fromUtf8(e.what()))); }
    });
    worker->start();
}
