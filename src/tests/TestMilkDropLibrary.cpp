#include "MilkDropLibrary.hpp"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestMilkDropLibrary : public QObject
{
    Q_OBJECT
private slots:
    void cachedCollectionWorksOffline()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir().mkpath(dir.path() + "/collections-v1/cream/a"));
        QFile marker(dir.path() + "/collections-v1/complete");
        QVERIFY(marker.open(QIODevice::WriteOnly)); marker.write("v1\n"); marker.close();
        QFile preset(dir.path() + "/collections-v1/cream/a/test.milk");
        QVERIFY(preset.open(QIODevice::WriteOnly)); preset.write("[preset00]\n"); preset.close();
        MilkDropLibrary library;
        QSignalSpy ready(&library, &MilkDropLibrary::ready);
        library.Start(dir.path());
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 5000);
        const auto result = ready.takeFirst();
        QCOMPARE(result[0].toStringList(), QStringList{preset.fileName()});
        QCOMPARE(result[1].toString(), dir.path() + "/collections-v1/textures");
    }
    void officialDownloads()
    {
        if (!qEnvironmentVariableIsSet("AGBPLAY_TEST_MILKDROP_DOWNLOAD")) QSKIP("Network integration test is opt-in");
        QTemporaryDir dir;
        MilkDropLibrary library;
        QSignalSpy ready(&library, &MilkDropLibrary::ready);
        QSignalSpy status(&library, &MilkDropLibrary::status);
        library.Start(dir.path());
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty() || (!status.isEmpty() && status.last()[0].toString().startsWith("MilkDrop:")), 240000);
        if (ready.isEmpty()) QFAIL(qPrintable(status.last()[0].toString()));
        QCOMPARE(ready[0][0].toStringList().size(), 10347);
        QVERIFY(QFile::exists(ready[0][1].toString() + "/prayerwheel.jpg"));
    }
};
QTEST_GUILESS_MAIN(TestMilkDropLibrary)
#include "TestMilkDropLibrary.moc"
