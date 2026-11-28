#pragma once

// 壳级 Ribbon 贡献声明（契约 D13）。
//
// 分工：
//   - 区域（ActivityBar / PrimarySidebar / SecondarySidebar / Panel / Editor）
//     是壳的代码常量，见 `registry::Region`，**不进** slot 注册表；
//   - 页签与分组只是 contributes 数据（ribbonPages/ribbonGroups），不再另立槽位表；
//   - 壳保留自己的页与分组：开始（产品级聚合页，暂留壳，等出现独立 home 模块再迁）、
//     文件、视图；其余业务页（数据/绘图/脚本/协议/分析/报告）由各模块在 `onAttach`
//     的页/组由各模块经 contributes() 声明式贡献。
//
// 本文件是 app 骨架的配置（app 长什么样），不是 core 机制；声明面即
// contributes 数据面（VSCode 扩展点口径）。

#include "ContributionParser.h"

namespace nextpilot::plotscope::registry {
class MenuRegistry;
}

namespace nextpilot::plotscope::host
{

// 壳级标准动作：骨架自带的 Ribbon 入口（文件/视图/导出/截图/帮助），直接落
// MenuRegistry（MenuId = menubar/<pageId>）；页/组归属与上方声明同源。
void declareShellActions(nextpilot::plotscope::registry::MenuRegistry &menus);

// 壳骨架的 contributes 文档（owner = app.skeleton）：页/组与 declareShellActions 的
// 动作归属同源，经 ExtensionRegistry::mountContribution 进 contributes 数据面；
// 壳从此不再独占一条插槽通道。
nextpilot::plotscope::registry::ContributesDoc shellContributes();

} // namespace nextpilot::plotscope::host
