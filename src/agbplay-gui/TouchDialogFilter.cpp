#include "TouchDialogFilter.hpp"
#include <QDialog>
#include <QEvent>
#include <QFileDialog>
#include <QLayout>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScroller>
#include <QVBoxLayout>

bool TouchDialogFilter::eventFilter(QObject *object, QEvent *event)
{
#ifdef Q_OS_ANDROID
    if (event->type() == QEvent::Show) {
        auto *dialog = qobject_cast<QDialog *>(object);
        if (dialog && !qobject_cast<QFileDialog *>(dialog) && dialog->layout() &&
            !dialog->property("agbplayTouchForm").toBool()) {
            dialog->setProperty("agbplayTouchForm", true);
            for (auto *button : dialog->findChildren<QPushButton *>())
                button->setMinimumHeight(44);
            auto *page = new QWidget;
            page->setLayout(dialog->layout()); // QWidget transfers the old layout and its children
            auto *scroll = new QScrollArea(dialog);
            scroll->setWidgetResizable(true);
            scroll->setWidget(page);
            QScroller::grabGesture(scroll->viewport(), QScroller::TouchGesture);
            auto *layout = new QVBoxLayout(dialog);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->addWidget(scroll);
            dialog->setMinimumSize(0, 0);
            dialog->resize(dialog->screen()->availableGeometry().size());
        }
    }
#endif
    return QObject::eventFilter(object, event);
}
