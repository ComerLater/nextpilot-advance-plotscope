#include "MenuRegistry.h"

namespace nextpilot::plotscope::registry {

MenuRegistry::MenuRegistry(QObject *parent) : QObject(parent) {}

void MenuRegistry::appendMenuItem(const QString &menuId, const IMenuItem &item)
{
    if (menuId.isEmpty() || item.action.id.isEmpty())
        return;
    for (const IMenuItem &e : m_items.value(menuId))
        if (e.action.id == item.action.id)
            return; // 先到者为准（VSCode 同 menu 同 id 不重复出项）
    m_items[menuId].append(item);
    emit menuChanged(menuId);
}

int MenuRegistry::removeMenuItems(const QString &menuId, const QString &owner)
{
    int removed = 0;
    QList<IMenuItem> &list = m_items[menuId];
    for (int i = list.size() - 1; i >= 0; --i)
    {
        if (list.at(i).owner == owner)
        {
            list.removeAt(i);
            ++removed;
        }
    }
    if (removed > 0)
        emit menuChanged(menuId);
    return removed;
}

QList<IMenuItem> MenuRegistry::getMenuItems(const QString &menuId) const { return m_items.value(menuId); }

QMetaObject::Connection MenuRegistry::onDidChangeMenu(const QString &menuId, std::function<void()> listener)
{
    return connect(this, &MenuRegistry::menuChanged, this, [menuId, listener](const QString &changed) {
        if (changed == menuId && listener)
            listener();
    });
}

} // namespace nextpilot::plotscope::registry
