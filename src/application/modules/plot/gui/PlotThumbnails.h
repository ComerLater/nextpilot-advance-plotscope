#pragma once

#include <QColor>
#include <QIcon>
#include <QPair>
#include <QString>
#include <QVector>

// 图库缩略图：按 kind 画一张小图，用于 ribbon gallery 的图型/格式项。
//
// A2c 呈现侧三件之一：本文件从 gui/theme 归位 modules/plot/gui（owner "plot"——
// 缩略图是绘图呈现语义，壳不再持有绘制器）；kind 值域统一 "plot/<kind>"（如
// "plot/timeseries"、"plot/layout.2x1"），装配时把 QIcon 注册进 IconRegistry，
// 壳只 `IconRegistry::instance().icon(id)`，不 include 本文件。
//
// 这些绘制逻辑抄自老 PlottingRibbon::plotThumb 的 UI 体验（12 种图型一眼可辨），
// 只依赖 Qt 的 QPainter 与 ThemeManager 配色，是纯展示工具。
//
// 配色走 ThemeManager::adaptColor，深浅主题都可读。
namespace nextpilot::plotscope::gui {

class PlotThumbnails
{
public:
    // kind: timeseries/xy/scatter/bar/histogram/fft/traj3d/map/surface3d/horizon/compass/gauge
    static QIcon plot(const QString &kind);

    // 文件格式/实时源的"文件卡"：左色块 + 标签，标签与配色由扩展贡献的驱动元数据给出。
    static QIcon fileCard(const QString &tag, const QColor &color);

    // 布局缩略图（"布局"组的大按钮）：按 rows×cols 画分格，抄 main 的 layoutThumb。
    // 与图型缩略图同一尺寸与配色，视觉上属于同一套 ribbon 缩略图。
    static QIcon layout(int rows, int cols);

    // 把全量缩略图注册进 IconRegistry（幂等，装配重入安全）：
    //   "plot/<kind>"     —— plot() 支持的全部 kind；
    //   "plot/layout.RxC" —— layout() 的 1..8 全组合（与「网格」选格弹层 GridPickPopup
    //                        上限一致；动态回写如 figure.grid 回写 2x3 只查表不画）。
    // 注册时机：plot 呈现层装配（installPlotViewFactories），对位 VSCode "扩展
    // 激活时注册图标"；headless（CLI）不装配呈现层，自然不注册。
    // driverMetas：扩展贡献的驱动 {id, displayName}；文件卡按它生成（不留格式常量表）。
    static void registerIcons(const QVector<QPair<QString, QString>> &driverMetas = {});
};

} // namespace nextpilot::plotscope::gui
