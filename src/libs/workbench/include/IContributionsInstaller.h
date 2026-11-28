#pragma once

// A8b-M2 裁定 52442006 Step 3：扩展声明灌回的唯一入口。原 IUiContext::installContributions
// 让"壳自己灌、壳自己读"，§9e 判据不可能自然归零；改为 ExtensionService 收集 contributes
// 后调装配层实现（assembly ContributionsInstaller，实现在 ExtensionService.cpp），
// 写入 StatusHub / ViewRegistry / MenuRegistry——写和读分开，壳只渲染。
// host 只持本接口指针（不 include registry / 不 include 装配层头）。

#include "ContributionParser.h" // ContributesDoc（纯数据）

namespace nextpilot::plotscope::workbench {

class IContributionsInstaller
{
public:
    virtual ~IContributionsInstaller() = default;

    // 灌一个 owner（扩展 identifier）的完整声明。owner = ContributesDoc::owner。
    // 幂等性由实现方保证（同 owner 重复灌以先到者为准 + 诊断，与 contributes 数据面语义一致）。
    virtual void install(const nextpilot::plotscope::registry::ContributesDoc &c) = 0;
};

} // namespace nextpilot::plotscope::workbench
