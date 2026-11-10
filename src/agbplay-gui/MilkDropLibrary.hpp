#pragma once
#include <QObject>
#include <QStringList>
class QThread;

// Network, decompression, and directory traversal run away from GUI and audio.
class MilkDropLibrary : public QObject
{
    Q_OBJECT
public:
    explicit MilkDropLibrary(QObject *parent = nullptr);
    ~MilkDropLibrary() override;
    void Start(const QString &root);
signals:
    void status(const QString &message);
    void ready(const QStringList &paths, const QString &textures);
private:
    QThread *worker = nullptr;
};
