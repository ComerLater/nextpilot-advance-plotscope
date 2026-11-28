#pragma once

namespace nextpilot::plotscope::gui {

// 区域尺寸（VSCode `Dimension`，`src/vs/base/browser/ui/grid/grid.ts`）。
// 纯数据：壳内所有 Part 的 layout() 只接受它，不直接摸 QWidget 几何。
struct Dimension
{
    int width = 0;
    int height = 0;
};

} // namespace nextpilot::plotscope::gui
