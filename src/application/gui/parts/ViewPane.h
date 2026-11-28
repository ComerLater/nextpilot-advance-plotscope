#pragma once

#include <QHash>
#include <QList>
#include <QString>

class QAction;
class QWidget;

namespace nextpilot::plotscope::registry {
class MenuRegistry;
}
namespace nextpilot::plotscope::command {
class CommandRegistry;
}

namespace nextpilot::plotscope::gui {

// 视图面板（VSCode `src/vs/workbench/browser/parts/views/viewPane.ts`）。
//
// 持某视图的标题栏动作（VSCode `MenuId.ViewTitle`，条目来自 MenuRegistry 既有登记）
// 与已建动作表。dock 本体归 ADS，本类只回答"这个视图标题栏上有哪些动作"。
class ViewPane
{
public:
    // 菜单注册表借用（VSCode MenuRegistry）；命令注册表借用（标题/图标/执行路径）。
    void setMenuRegistry(registry::MenuRegistry *registry) { m_menus = registry; }
    void setCommandRegistry(command::CommandRegistry *commands) { m_commands = commands; }

    // 为某视图建标题栏动作；parent 是该视图的 dock。重复调用会重建（与旧口径一致）。
    QList<QAction *> titleActions(QWidget *parent, const QString &viewId);
    int titleActionCount(const QString &viewId) const { return m_titleActions.value(viewId).size(); }

private:
    registry::MenuRegistry *m_menus = nullptr;
    command::CommandRegistry *m_commands = nullptr;
    QHash<QString, QList<QAction *>> m_titleActions;
};

} // namespace nextpilot::plotscope::gui
