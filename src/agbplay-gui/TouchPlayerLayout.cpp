#include "TouchPlayerLayout.hpp"

#include <QResizeEvent>
#include <algorithm>
#include <QTabBar>

TouchPlayerLayout::TouchPlayerLayout(const std::array<QWidget *, 5> &pages, QWidget *controls, QWidget *parent) :
    QWidget(parent), pages(pages)
{
    setObjectName("touchPlayerLayout");
    layout.setSizeConstraint(QLayout::SetNoConstraint);
    layout.setContentsMargins(6, 4, 6, 4);
    layout.setSpacing(4);
    layout.addWidget(controls);
    layout.addWidget(&navigation, 1);
    layout.addWidget(&panels, 1);
    panels.setChildrenCollapsible(false);
    lists.setChildrenCollapsible(false);
    panels.setHandleWidth(12);
    lists.setHandleWidth(12);
    navigation.setObjectName("compactNavigation");
    details.setObjectName("detailNavigation");
    for (auto *tabs : {&navigation, &details}) {
        tabs->setDocumentMode(true);
        tabs->setStyleSheet("QTabBar::tab { min-height: 48px; padding: 0px 4px; }");
        tabs->tabBar()->setExpanding(true);
        tabs->setUsesScrollButtons(true);
    }
    arrange(modeForSize(size()));
}

TouchPlayerLayout::Mode TouchPlayerLayout::modeForSize(QSize size)
{
    // Qt widget coordinates are density independent. Short landscape windows keep
    // tabs so both lists and 48px controls retain usable vertical space.
    if (size.width() < 840 || size.height() < 480)
        return Mode::Compact;
    return size.width() < 1200 ? Mode::TwoPane : Mode::ThreePane;
}

void TouchPlayerLayout::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    const auto mode = modeForSize(event->size());
    if (!initialized || mode != currentMode)
        arrange(mode);
}

void TouchPlayerLayout::arrange(Mode mode)
{
    const std::array<QString, 5> titles{"Canciones", "Playlist", "Pistas", "ROM", "Log"};
    if (initialized) {
        if (currentMode == Mode::Compact) {
            compactPage = navigation.currentIndex();
            detailPage = compactPage >= 2 ? compactPage : 2;
        }
        else if (details.currentWidget()) {
            for (int i = 2; i < 5; ++i)
                if (details.currentWidget() == pages[static_cast<size_t>(i)])
                    detailPage = i;
        }
    }
    // Detach before clearing tabs: the same widgets and their contents survive.
    for (auto *page : pages) {
        page->hide();
        page->setParent(this);
    }
    navigation.clear();
    details.clear();
    lists.setParent(this);
    details.setParent(this);
    navigation.setVisible(mode == Mode::Compact);
    panels.setVisible(mode != Mode::Compact);
    lists.hide();
    details.hide();

    if (mode == Mode::Compact) {
        for (size_t i = 0; i < pages.size(); ++i)
            navigation.addTab(pages[i], titles[i]);
        navigation.setCurrentIndex(compactPage);
    } else {
        panels.addWidget(&lists);
        lists.addWidget(pages[0]);
        lists.addWidget(pages[1]);
        lists.show();
        if (mode == Mode::ThreePane) {
            panels.addWidget(pages[2]);
            pages[2]->show();
        } else {
            details.addTab(pages[2], titles[2]);
        }
        details.addTab(pages[3], titles[3]);
        details.addTab(pages[4], titles[4]);
        panels.addWidget(&details);
        details.setCurrentWidget(pages[static_cast<size_t>(mode == Mode::TwoPane ? detailPage : std::max(3, detailPage))]);
        details.show();
        pages[0]->show();
        pages[1]->show();
        panels.setSizes(mode == Mode::ThreePane ? QList<int>{260, 650, 280} : QList<int>{260, 650});
        panels.setStretchFactor(0, 0);
        panels.setStretchFactor(1, 1);
        if (mode == Mode::ThreePane)
            panels.setStretchFactor(2, 0);
    }
    currentMode = mode;
    initialized = true;
}

namespace {
int controlColumns(int width) { return width >= 1000 ? 3 : width >= 600 ? 2 : 1; }
}

TouchControlRows::TouchControlRows(QWidget *parent) : QWidget(parent)
{
    layout.setContentsMargins(0, 0, 0, 0);
    layout.setSpacing(4);
    layout.setSizeConstraint(QLayout::SetNoConstraint);
    playback.setContentsMargins(0, 0, 0, 0);
    playback.setSpacing(4);
    auto policy = QSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

void TouchControlRows::setRows(const std::array<QWidget *, 3> &newRows)
{
    rows = newRows;
    layout.addWidget(rows[0]);
    layout.addLayout(&playback);
    playback.addWidget(rows[1], 1);
    playback.addWidget(rows[2], 1);
    for (auto *row : rows)
        row->show();
    arrange(controlColumns(width()));
}

int TouchControlRows::heightForWidth(int width) const
{
    const int count = controlColumns(width);
    std::array<int, 3> heights{48, 48, 48};
    for (size_t i = 0; i < rows.size(); ++i) {
        if (const auto *row = rows[i])
            heights[i] = std::max({48, row->minimumHeight(), row->minimumSizeHint().height(), row->sizeHint().height()});
    }
    if (count == 3)
        return std::max({heights[0], heights[1], heights[2]});
    if (count == 2)
        return heights[0] + layout.spacing() + std::max(heights[1], heights[2]);
    return heights[0] + heights[1] + heights[2] + layout.spacing() + playback.spacing();
}

QSize TouchControlRows::minimumSizeHint() const
{
    return {280, heightForWidth(width())};
}

QSize TouchControlRows::sizeHint() const
{
    return {layout.sizeHint().width(), heightForWidth(width())};
}

void TouchControlRows::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (rows[0] && columns != controlColumns(event->size().width()))
        arrange(controlColumns(event->size().width()));
}

void TouchControlRows::arrange(int count)
{
    layout.setDirection(count == 3 ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
    playback.setDirection(count == 1 ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    layout.setStretch(0, count == 3 ? 1 : 0);
    layout.setStretch(1, count == 3 ? 2 : 0);
    columns = count;
    updateGeometry();
}
