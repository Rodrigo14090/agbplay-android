#pragma once

#include <array>
#include <QSplitter>
#include <QGridLayout>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

// Reparents the existing views; resizing never reconstructs their models or player.
class TouchPlayerLayout : public QWidget
{
    Q_OBJECT
public:
    enum class Mode { Compact, TwoPane, ThreePane };
    TouchPlayerLayout(const std::array<QWidget *, 5> &pages, QWidget *controls, QWidget *parent = nullptr);
    QSize minimumSizeHint() const override { return {280, 240}; }
    Mode mode() const { return currentMode; }
    static Mode modeForSize(QSize size);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void arrange(Mode mode);
    std::array<QWidget *, 5> pages;
    QVBoxLayout layout{this};
    QSplitter panels{Qt::Horizontal, this};
    QSplitter lists{Qt::Vertical, &panels};
    QTabWidget navigation{this};
    QTabWidget details{this};
    Mode currentMode = Mode::Compact;
    int compactPage = 0;
    int detailPage = 2;
    bool initialized = false;
};

// Reflows the three control groups without hiding or duplicating actions.
class TouchControlRows : public QWidget
{
public:
    explicit TouchControlRows(QWidget *parent = nullptr);
    void setRows(const std::array<QWidget *, 3> &rows);
    int heightForWidth(int width) const override;
    QSize minimumSizeHint() const override { return {280, 48}; }
protected:
    void resizeEvent(QResizeEvent *event) override;
private:
    void arrange(int columns);
    QGridLayout grid{this};
    std::array<QWidget *, 3> rows{};
    int columns = 0;
};
