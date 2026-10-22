#pragma once
#include <QObject>

// Makes the existing complex profile/preferences forms scrollable on phones.
class TouchDialogFilter final : public QObject
{
public:
    using QObject::QObject;
    bool eventFilter(QObject *object, QEvent *event) override;
};
