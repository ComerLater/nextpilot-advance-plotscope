#include "MenuId.h"
#include "MenuRegistry.h"
#include "actions/ActionBar.h"
#include "IActionProvider.h"
#include "ViewRegistry.h"

#include <QAction>
#include <QApplication>
#include <QStringList>

#include <cstdio>

/*
 * M4 第 2 笔验收：MenuId / MenuRegistry / ActionBar
 *   - MenuId 值域固定：menubar/<pageId>、view/title、viewContainer/title、view/item/context
 *   - MenuRegistry 按 MenuId 汇总多个 provider，同 id 先到者为准
 *   - onDidChangeMenu 只在目标菜单变化时回调
 *   - ActionBar 是唯一 QAction 出口：文本/勾选/快捷键/数据/触发均生效，update 就地改，dispose 移除
 */
static int g_fail = 0;
#define OK(cond, msg)                                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        if (cond)                                                                                                      \
            std::printf("[ok] %s\n", msg);                                                                             \
        else                                                                                                           \
        {                                                                                                              \
            std::printf("[FAIL] %s (%s:%d)\n", msg, __FILE__, __LINE__);                                               \
            ++g_fail;                                                                                                  \
        }                                                                                                              \
    } while (0)

using nextpilot::plotscope::gui::ActionBar;
using nextpilot::plotscope::gui::IAction;
using nextpilot::plotscope::registry::IActionProvider;
using nextpilot::plotscope::registry::IMenuItem;
using nextpilot::plotscope::registry::MenuId;
using nextpilot::plotscope::registry::MenuRegistry;
using nextpilot::plotscope::registry::UiAction;
using nextpilot::plotscope::registry::ViewRegistry;

namespace {
class FakeProvider final : public IActionProvider
{
public:
    QList<UiAction> items;
    QList<QString> changed;

    QList<UiAction> actionsFor(const QString &slotId) const override
    {
        QList<UiAction> out;
        for (const UiAction &a : items)
            if (a.slotId == slotId)
                out.append(a);
        return out;
    }

    QMetaObject::Connection onDidChangeActions(std::function<void(const QString &)> listener) override
    {
        m_listener = std::move(listener);
        return {};
    }

    void fire(const QString &slotId)
    {
        if (m_listener)
            m_listener(slotId);
    }

private:
    std::function<void(const QString &)> m_listener;
};

UiAction makeAction(const QString &id, const QString &slotId, const QString &label)
{
    UiAction a;
    a.id = id;
    a.slotId = slotId;
    a.label = label;
    return a;
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    // MenuId 值域
    OK(MenuId::menubar(QStringLiteral("plot")) == QStringLiteral("menubar/plot"), "menubar/<pageId> 形态");
    OK(MenuId::viewTitle() == QStringLiteral("view/title"), "view/title");
    OK(MenuId::viewContainerTitle() == QStringLiteral("viewContainer/title"), "viewContainer/title");
    OK(MenuId::viewItemContext() == QStringLiteral("view/item/context"), "view/item/context");

    // MenuRegistry：静态登记（appendMenuItem）+ 去重 + 卸载撤销 + 变更回调
    {
        MenuRegistry registry;
        const QString menu = MenuId::menubar(QStringLiteral("plot"));
        UiAction pitch = makeAction(QStringLiteral("ap.plot.pitch"), menu, QStringLiteral("俯仰"));
        UiAction roll = makeAction(QStringLiteral("ap.plot.roll"), menu, QStringLiteral("横滚"));
        registry.appendMenuItem(menu, IMenuItem{pitch, QStringLiteral("ribbon.group.plot"), QStringLiteral("p1")});
        registry.appendMenuItem(menu, IMenuItem{roll, QStringLiteral("ribbon.group.plot"), QStringLiteral("p1")});
        registry.appendMenuItem(menu,
                                IMenuItem{makeAction(QStringLiteral("ap.plot.pitch"), menu, QStringLiteral("覆盖")),
                                          QStringLiteral("ribbon.group.plot"), QStringLiteral("p2")});
        OK(registry.getMenuItems(menu).size() == 2, "同 menu 同 id 只出第一条");
        OK(registry.getMenuItems(menu).at(0).action.label == QStringLiteral("俯仰"), "先到者为准");

        int calls = 0;
        registry.onDidChangeMenu(menu, [&calls]() { ++calls; });
        registry.appendMenuItem(menu,
                                IMenuItem{makeAction(QStringLiteral("ap.data.open"), menu, QStringLiteral("打开")),
                                          QStringLiteral("ribbon.group.data"), QStringLiteral("p2")});
        OK(calls == 1, "目标 menu 变更触发回调");
        registry.appendMenuItem(MenuId::viewTitle(),
                                IMenuItem{makeAction(QStringLiteral("x"), MenuId::viewTitle(), QStringLiteral("x")),
                                          QString(), QStringLiteral("p1")});
        OK(calls == 1, "非目标 menu 变更不触发");

        OK(registry.removeMenuItems(menu, QStringLiteral("p2")) == 1, "按 owner 撤销");
        OK(registry.getMenuItems(menu).size() == 2, "撤销后剩 p1 两条");
        // VSCode IMenuItem.group：同一菜单内的分组字段，原样登记。
        registry.appendMenuItem(menu, IMenuItem{makeAction(QStringLiteral("grp.plot"), menu, QStringLiteral("绘图")),
                                                QStringLiteral("ribbon.group.plot"), QStringLiteral("p1")});
        const auto items = registry.getMenuItems(menu);
        OK(items.last().group == QStringLiteral("ribbon.group.plot"), "group 原样登记");
    }

    // ActionBar：创建、字段生效、触发、就地更新、撤销
    {
        IAction item;
        item.id = QStringLiteral("ap.test");
        item.text = QStringLiteral("测试");
        item.checkable = true;
        item.shortcut = QStringLiteral("Ctrl+T");
        int fired = 0;
        item.trigger = [&fired]() { ++fired; };
        QAction *action = ActionBar::create(nullptr, item);
        OK(action != nullptr, "创建 QAction");
        OK(action->text() == QStringLiteral("测试"), "文本生效");
        OK(action->isCheckable(), "勾选态生效");
        OK(action->shortcut() == QKeySequence(QStringLiteral("Ctrl+T")), "快捷键生效");
        OK(action->data().toString() == QStringLiteral("ap.test"), "action id 生效");
        action->trigger();
        OK(fired == 1, "触发回调生效");

        item.text = QStringLiteral("改名");
        item.checkable = false;
        ActionBar::update(action, item);
        OK(action->text() == QStringLiteral("改名"), "就地更新文本");
        OK(!action->isCheckable(), "就地更新勾选态");
        ActionBar::dispose(action);
    }

    std::printf(g_fail == 0 ? "全部通过\n" : "失败 %d 项\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
