#pragma once

#include <QString>

namespace nextpilot::plotscope::registry {

// 菜单标识（VSCode `src/vs/platform/actions/common/actions.ts` 的 `MenuId`）。
//
// 值域只允许本文件里的形态：`menubar/<pageId>`、`view/title` 等固定 id。
// 过去壳里拼的 RibbonPage 页路径字符串一律作废——RibbonPage 只是 RibbonPart 内部控件名。
struct MenuId
{
    // 菜单栏某一页：`menubar/<pageId>`，pageId 来自 contributes，与 `dock()` / openView 同源。
    static QString menubar(const QString &pageId);

    // 容器标题栏（VSCode `MenuId.ViewContainerTitle`）。
    static QString viewContainerTitle();

    // 视图标题栏（VSCode `MenuId.ViewTitle`）。
    static QString viewTitle();

    // 视图项右键（VSCode `MenuId.ViewItemContext`）。
    static QString viewItemContext();
};

} // namespace nextpilot::plotscope::registry
