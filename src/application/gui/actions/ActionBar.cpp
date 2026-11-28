#include "ActionBar.h"

#include <QAction>
#include <QIcon>
#include <QKeySequence>
#include <QObject>

namespace nextpilot::plotscope::gui {

QAction *ActionBar::create(QObject *parent, const IAction &item)
{
    auto *action = new QAction(parent);
    update(action, item);
    return action;
}

void ActionBar::update(QAction *action, const IAction &item)
{
    if (!action)
        return;
    action->setText(item.text);
    action->setToolTip(item.toolTip.isEmpty() ? item.text : item.toolTip);
    if (!item.icon.isNull())
        action->setIcon(item.icon);
    action->setCheckable(item.checkable);
    action->setShortcut(item.shortcut.isEmpty() ? QKeySequence() : QKeySequence(item.shortcut));
    if (!item.id.isEmpty())
        action->setData(item.id);
    if (item.trigger)
    {
        QObject::disconnect(action, &QAction::triggered, nullptr, nullptr);
        QObject::connect(action, &QAction::triggered, action, [fn = item.trigger]() {
            if (fn)
                fn();
        });
    }
}

void ActionBar::dispose(QAction *action)
{
    if (!action)
        return;
    QObject::disconnect(action, nullptr, nullptr, nullptr);
    action->deleteLater();
}

} // namespace nextpilot::plotscope::gui
