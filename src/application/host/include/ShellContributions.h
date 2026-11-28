#pragma once

// 壳级 Ribbon 贡献声明（契约 D13）。
//
// 解散原 SkeletonSlots 后的分工：
//   - 区域（ActivityBar / PrimarySidebar / SecondarySidebar / Panel / Editor）
//     是壳的代码常量，见 `registry::Region`，**不进** slot 注册表；
//   - slot 注册表只承载 Ribbon 页签与分组这两类有真实消费者的容器；
//   - 壳保留自己的页与分组：开始（产品级聚合页，暂留壳，等出现独立 home 模块再迁）、
//     文件、视图；其余业务页（数据/绘图/脚本/协议/分析/报告）由各模块在 `onAttach`
//     的页/组由各模块经 contributes() 声明式贡献。
//
// 本文件是 app 骨架的配置（app 长什么样），不是 core 机制；core 只提供
// 插槽注册表与页/组声明能力。

#include "ContributionParser.h"
#include "UiSlotHub.h"

namespace nextpilot::plotscope::workbench
{
class ISlotsService;
}

namespace nextpilot::plotscope::registry {
class MenuRegistry;
}

namespace nextpilot::plotscope::host
{

void declareShellContributions(nextpilot::plotscope::registry::UiSlotHub &registry);

// A8b-M2 裁定 52442006 Step 4：壳（gui 目录）不再持 UiSlotHub，骨架声明经
// ISlotsService 通道（同一份声明清单，与 UiSlotHub 版完全等价）。
void declareShellContributions(nextpilot::plotscope::workbench::ISlotsService &slots);

// 壳级标准动作：骨架自带的 Ribbon 入口（文件/视图/导出/截图/帮助），直接落
// MenuRegistry（MenuId = menubar/<pageId>）；页/组归属与上方声明同源。
void declareShellActions(nextpilot::plotscope::registry::MenuRegistry &menus);

// 壳骨架的 contributes 文档（owner = app.skeleton）：页/组与 declareShellActions 的
// 动作归属同源，经 ExtensionRegistry::mountContribution 进 contributes 数据面；
// 壳从此不再独占一条插槽通道。
nextpilot::plotscope::registry::ContributesDoc shellContributes();

} // namespace nextpilot::plotscope::host
