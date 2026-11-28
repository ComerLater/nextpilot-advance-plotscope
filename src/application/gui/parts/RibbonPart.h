#pragma once

#include "Part.h"

#include <QHash>
#include <QStringList>

#include <functional>

class QAction;
class QMenu;
class SARibbonBar;
class QWidget;

namespace nextpilot::plotscope::workbench {
class IRibbonService;
}

namespace nextpilot::plotscope::registry {
class MenuRegistry;
}

namespace nextpilot::plotscope::gui {

// Ribbon 部件（VSCode 对位 TitlebarPart：窗口顶部 chrome，本仓承载是 SARibbon）。
//
// SARibbonBar 由 QMainWindow 构造并通过 `attach()` 交给本部件接管；本部件不 delete 它
// （所有权仍在宿主窗口），只负责页签状态、可见页集合与最小/常规模式的壳内口径。
// Ribbon 页/组/动作的构建体全部归本部件私有实现：绑定器只转发既有访问器，不持有
// 构建逻辑。
class RibbonPart : public Part
{
public:
    using IntentDispatcher = std::function<void(const QString &actionId, const QString &intentOp, const QString &data)>;

    RibbonPart();

    // 接管宿主的 ribbon 控件；重复 attach 同一实例为空操作。
    void attach(SARibbonBar *bar);
    SARibbonBar *bar() const { return m_bar; }

    // 页签下标口径（状态快照与 applyState 恢复共用）。
    int currentPage() const { return m_currentPage; }
    void setCurrentPage(int index) { m_currentPage = index; }
    int pendingPage() const { return m_pendingPage; }
    void setPendingPage(int index) { m_pendingPage = index; }

    // 上一轮实际建出来的页标签集合：applyWhenVisibility 靠它判断"要不要重建"。
    const QStringList &lastVisiblePages() const { return m_lastVisiblePages; }
    void setLastVisiblePages(QStringList pages) { m_lastVisiblePages = std::move(pages); }

    // when 求值器（由外壳注入；本部件不解析表达式，空 when 视为真）。
    void setWhenEvaluator(std::function<bool(const QString &)> evaluator) { m_whenEval = std::move(evaluator); }
    bool whenVisible(const QString &when) const;

    // Ribbon 读回服务（借用；未装配的裸壳为 nullptr）。页/组来自它的 ribbonPages/ribbonGroupsOf。
    void setRibbonService(nextpilot::plotscope::workbench::IRibbonService *service) { m_ribbon = service; }
    nextpilot::plotscope::workbench::IRibbonService *ribbonService() const { return m_ribbon; }

    // 菜单注册表（借用；menubar/<pageId> 条目来源）。
    void setMenuRegistry(nextpilot::plotscope::registry::MenuRegistry *menus) { m_menus = menus; }
    // 意图调度回调（点击动作 → 外壳执行器）。
    void setIntentDispatcher(IntentDispatcher dispatcher) { m_dispatch = std::move(dispatcher); }

    // 显式重建（装配完成点 / 菜单或贡献变化后由 Part 自触发）。
    void refresh();
    // 上下文变化后重算可见页：只有可见集合真的变了才重建 ribbon（避免抖动）。
    void refreshWhenVisible();

    // --- 动作生命周期（VSCode `ActionBar` 接管面）---
    // 页内按钮的 QAction 由本部件持有并登记：就地改图标/文字、按 id 触发、
    // 触发计数（测试口径）、下拉菜单来源都归这里，绑定器只做转发。
    using ActionMenuProvider = std::function<QMenu *(const QString &actionId)>;

    void setActionMenuProvider(ActionMenuProvider provider) { m_actionMenuProvider = std::move(provider); }
    QMenu *actionMenu(const QString &actionId) const
    {
        return m_actionMenuProvider ? m_actionMenuProvider(actionId) : nullptr;
    }

    void registerAction(const QString &actionId, QAction *action) { m_actionById.insert(actionId, action); }
    QAction *action(const QString &actionId) const { return m_actionById.value(actionId, nullptr); }
    void clearActions() { m_actionById.clear(); }
    void bumpTrigger(const QString &actionId) { m_actionTriggers[actionId]++; }
    int triggerCount(const QString &actionId) const { return m_actionTriggers.value(actionId, 0); }

protected:
    QWidget *createContent(QWidget *parent) override;

private:
    void rebuild();
    QStringList visibleLabelsNow() const;

    SARibbonBar *m_bar = nullptr; // 借用，宿主 QMainWindow 拥有
    int m_currentPage = 0;
    int m_pendingPage = -1;
    QStringList m_lastVisiblePages;
    std::function<bool(const QString &)> m_whenEval;
    nextpilot::plotscope::workbench::IRibbonService *m_ribbon = nullptr; // 借用
    nextpilot::plotscope::registry::MenuRegistry *m_menus = nullptr;     // 借用
    IntentDispatcher m_dispatch;
    QHash<QString, QAction *> m_actionById;  // actionId -> 已建动作
    QHash<QString, int> m_actionTriggers;    // actionId -> 触发次数
    ActionMenuProvider m_actionMenuProvider; // 下拉菜单来源（可为空）
};

} // namespace nextpilot::plotscope::gui
