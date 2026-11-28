#include "ViewPane.h"

#include "MenuId.h"
#include "MenuRegistry.h"
#include "actions/ActionBar.h"
#include "command/CommandRegistry.h"

#include <QAction>
#include <QHash>
#include <QIcon>
#include <QStyle>
#include <QVariant>
#include <QWidget>

namespace nextpilot::plotscope::gui {

QList<QAction *> ViewPane::titleActions(QWidget *parent, const QString &viewId)
{
    m_titleActions.remove(viewId);
    if (!m_menus || !m_commands || viewId.isEmpty())
        return {};

    QList<QAction *> actions;
    const QList<registry::IMenuItem> items = m_menus->getMenuItems(registry::MenuId::viewTitle());
    for (const registry::IMenuItem &item : items)
    {
        const registry::UiAction &ua = item.action;
        if (ua.id.isEmpty())
            continue;

        // VSCode 视图作用域：when 必须显式命中本视图（`view` 键 = 当前 viewId，由本部件
        // 注入求值上下文）；空 when 的条目不挂到任何视图。
        if (ua.when.isEmpty())
            continue;
        QHash<QString, QVariant> ctx = m_commands ? m_commands->contextValues() : QHash<QString, QVariant>{};
        ctx.insert(QStringLiteral("view"), viewId);
        if (!command::CommandRegistry::evaluateWhen(ua.when, ctx))
            continue;

        // 标签/图标取命令描述（同一个命令在 Ribbon、标题栏、命令面板显示一致）；
        // 命令未登记时退回动作声明的文案/图标。
        const command::CommandDescriptor *cmd = m_commands ? m_commands->find(ua.id) : nullptr;
        auto *action = gui::ActionBar::create(parent, gui::IAction{});
        const QString text = cmd ? cmd->title : ua.label;
        action->setText(text);
        action->setToolTip(text);
        if (cmd && !cmd->icon.isEmpty())
            action->setIcon(QIcon(QStringLiteral(":/icons/%1.svg").arg(cmd->icon)));
        else if (!ua.icon.isEmpty())
            action->setIcon(QIcon(QStringLiteral(":/icons/%1.svg").arg(ua.icon)));
        else
            action->setIcon(parent ? parent->style()->standardIcon(QStyle::SP_BrowserReload) : QIcon());

        // 点击走同一个命令执行路径（与按钮/快捷键同一条）。
        const QString commandId = ua.id;
        if (m_commands)
            QObject::connect(action, &QAction::triggered, parent, [this, commandId]() {
                QString error;
                m_commands->execute(commandId, &error);
            });
        actions << action;
    }

    if (!actions.isEmpty())
        m_titleActions.insert(viewId, actions);
    return actions;
}

} // namespace nextpilot::plotscope::gui
