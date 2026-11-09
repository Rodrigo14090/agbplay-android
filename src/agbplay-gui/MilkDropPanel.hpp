#pragma once
#include "MilkDropWindow.hpp"
#include <QWidget>
#include <QVBoxLayout>
#include <QComboBox>
#include <memory>

class MilkDropPanel : public QWidget
{
    Q_OBJECT
public:
    explicit MilkDropPanel(QWidget *parent = nullptr);
    ~MilkDropPanel() override;
    void SetSource(MilkDropWindow::Source source, std::function<void(bool)> enable);
    void SetFullscreen(bool value);
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
    QVBoxLayout *fullscreenLayout = nullptr;
    std::function<void(bool)> enablePcm;
    QList<QByteArray> presets;
    QStringList names;
    QList<QComboBox *> selectors;
    int selected = 0;
};
