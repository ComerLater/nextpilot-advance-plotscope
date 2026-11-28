#pragma once

#include "Dimension.h"
#include "ProductSpec.h"
#include "Region.h"

#include <QHash>
#include <QList>
#include <QString>

class QWidget;

namespace ads {
class CDockAreaWidget;
class CDockManager;
class CDockWidget;
}

namespace nextpilot::plotscope::gui {

class Part;

// 布局（VSCode `src/vs/workbench/browser/layout.ts`）。
//
// 持 ADS 停靠体系（它就是本仓的 Grid）、各区域登记表与页签定位表；
// 职责：Part 登记 + 整体 Dimension 分发 + 区域尺寸记录。
class Layout
{
public:
    using AreaMap = QHash<nextpilot::plotscope::registry::Region, ads::CDockAreaWidget *>;
    using PanelMap = QHash<nextpilot::plotscope::registry::Region, ads::CDockWidget *>;
    using DockListMap = QHash<nextpilot::plotscope::registry::Region, QList<ads::CDockWidget *>>;

    Layout();
    ~Layout();

    void registerPart(Part *part);
    bool unregisterPart(const QString &id);
    int partCount() const { return int(m_parts.size()); }
    Part *part(const QString &id) const;

    // 区域尺寸记录：默认值 + 显式设置（各 Part 读取）。
    Dimension regionSize(nextpilot::plotscope::registry::Region region) const;
    void setRegionSize(nextpilot::plotscope::registry::Region region, const Dimension &size);

    // 整体尺寸变化：记录后逐 Part 分发。
    void layout(const Dimension &dimension);
    Dimension size() const { return m_size; }

    // --- 停靠体系（ADS）：壳内唯一持有者，Part/绑定器只借用 ---
    ads::CDockManager *dock() const { return m_dock; }
    void setDock(ads::CDockManager *dock) { m_dock = dock; }
    QString adsBaseQss() const { return m_adsBaseQss; }
    void setAdsBaseQss(const QString &qss) { m_adsBaseQss = qss; }
    QString dockQss() const { return m_dockQss; }
    void setDockQss(const QString &qss) { m_dockQss = qss; }

    AreaMap &lastArea() { return m_lastArea; }
    PanelMap &panels() { return m_panels; }
    DockListMap &docksByArea() { return m_docksByArea; }
    QHash<QString, ads::CDockWidget *> &dockByKey() { return m_dockByKey; }
    QHash<QWidget *, ads::CDockWidget *> &dockByWidget() { return m_dockByWidget; }
    int &dockSeq() { return m_dockSeq; }
    // 首帧按比例收窄一次就够：是否已做过归布局，不归绑定器。
    bool &proportionsApplied() { return m_proportionsApplied; }
    // 产品默认面板放置表（区域 + 视图 id）：区域登记表的一部分。
    QList<nextpilot::plotscope::registry::ProductPanelPlacement> &placements() { return m_placements; }
    void setPlacements(const QList<nextpilot::plotscope::registry::ProductPanelPlacement> &placements)
    {
        m_placements = placements;
    }

private:
    QList<Part *> m_parts;
    QHash<int, Dimension> m_regionSizes;
    Dimension m_size;

    ads::CDockManager *m_dock = nullptr;
    // ADS 构造时装上的内建样式表：换肤时以它为底再叠停靠区样式。
    QString m_adsBaseQss;
    // ThemeManager 产出的停靠区页签样式（早于 CDockManager 创建时要缓存）。
    QString m_dockQss;
    AreaMap m_lastArea;        // region -> 该侧最后一个 DockArea
    PanelMap m_panels;         // region -> 该侧主 dock
    DockListMap m_docksByArea; // region -> 全部页签（按放置顺序）
    // pageKey（面板类型 id）-> dock 页签：视图页逐页开关按钮的定位表。
    QHash<QString, ads::CDockWidget *> m_dockByKey;
    // 面板内容 widget -> 持有它的 dock 页签（raisePanel 置前用）。
    QHash<QWidget *, ads::CDockWidget *> m_dockByWidget;
    int m_dockSeq = 0; // dock objectName 消歧序号
    bool m_proportionsApplied = false;
    QList<nextpilot::plotscope::registry::ProductPanelPlacement> m_placements;
};

} // namespace nextpilot::plotscope::gui
