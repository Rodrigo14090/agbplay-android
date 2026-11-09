#pragma once
#include "MilkDropWindow.hpp"
#include "MilkDropLibrary.hpp"
#include <QWidget>
#include <QVBoxLayout>
#include <QComboBox>
#include <QPointer>
#include <memory>

class MilkDropPanel : public QWidget
{
    Q_OBJECT
public:
    explicit MilkDropPanel(QWidget *parent = nullptr);
    ~MilkDropPanel() override;
    void SetSource(MilkDropWindow::Source source, std::function<void(bool)> enable);
    void SetFullscreen(bool value);
    void PrepareForShutdown();
    bool IsFullscreen() const { return fullscreen != nullptr; }
    QJsonObject Diagnostics() const;
    void SelectPreset(int index);
protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
private:
    QWidget *MakeControls(bool fullscreenControls);
    void ImportPreset();
    void SetOrientation(bool landscape);
    void UpdateEnabled();
    QVBoxLayout layout{this};
    MilkDropWindow *surface;
    QWidget *viewport;
    std::unique_ptr<QWidget> fullscreen;
    QList<QPointer<QWidget>> retiredPresentations;
    QPointer<QWidget> previousWindow;
    bool previousVisible = false;
    QVBoxLayout *fullscreenLayout = nullptr;
    std::function<void(bool)> enablePcm;
    QStringList presets;
    QStringList names;
    QString textures;
    MilkDropLibrary *library = nullptr;
    QList<QComboBox *> selectors;
    int selected = 0;
    bool closing = false;
};
