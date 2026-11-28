#pragma once

// A8b-M2 裁定 52441074 §3：壳读回贡献数据必须经 libs/workbench 的服务接口，
// 不许在 src/application/gui 里出现 ExtensionRegistry / toolbox()（§9 e) grep → 0）。
// 本接口只暴露**纯数据**（页/组声明与动态条目），不暴露任何 Hub/容器类；实现放 assembly，
// 内部从唯一扩展注册表的 contributes 面读回。静态条目由壳直接读 MenuRegistry
// （MenuId = menubar/<pageId>，条目 group = 组 id），不另经服务面。

#include "ContributionParser.h" // RibbonPageDecl / RibbonGroupDecl（纯数据）
#include "Contribution.h" // UiAction（纯数据）

#include <QList>
#include <QString>

namespace nextpilot::plotscope::workbench
{

class IRibbonService
{
public:
    virtual ~IRibbonService() = default;

    // 该 slot 的动态条目（来自 IActionProvider，运行期变化；静态条目在前）。
    // 实现方负责把 provider 的 actionsFor(slotId) 合并进来；headless 下可查。
    virtual QList<nextpilot::plotscope::registry::UiAction>
    dynamicActionsIn(const QString &slotId) const = 0;

    // 贡献声明的 Ribbon 页（数据源 contributes.ribbonPages，纯数据；按 order 升序）。
    // 名字对位 contributes 既有字段 ribbonPages；VSCode 无原生 Ribbon 概念，本仓自有。
    virtual QList<nextpilot::plotscope::registry::RibbonPageDecl> ribbonPages() const = 0;

    // 某 Ribbon 页下的组（数据源 contributes.ribbonGroups，按 pageId 过滤；按 order 升序）。
    // 名字对位 contributes 既有字段 ribbonGroups；VSCode 无原生 Ribbon 概念，本仓自有。
    virtual QList<nextpilot::plotscope::registry::RibbonGroupDecl> ribbonGroupsOf(const QString &pageId) const = 0;
};

} // namespace nextpilot::plotscope::workbench
