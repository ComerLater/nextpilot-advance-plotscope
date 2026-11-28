#pragma once

#include "Part.h"
#include "ViewContainerModel.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

class QWidget;

namespace ads {
class CDockWidget;
}

namespace nextpilot::plotscope::registry {
enum class Region;
}

namespace nextpilot::plotscope::gui {

class Layout;

// Sidebar 部件（VSCode `src/vs/workbench/browser/parts/sidebar/sidebarPart.ts`）。
//
// 左栏"整栏显隐 + 各 section 展开/收起"的唯一归属。section 本身是独立 dock
// 页签（可跨左/右/底拖动），部件只记壳内口径，不拥有它们。
class SidebarPart : public Part
{
public:
    // 按 pageKey 取该 section 的 dock 页签（多为空，未装配时安全退化为空清单）。
    using DockLookup = std::function<ads::CDockWidget *(const QString &pageKey)>;
    // pageKey -> 所属 Activity（用于 sectionActivity 与视图归属判断）。
    using ActivityLookup = std::function<QString(const QString &pageKey)>;

    SidebarPart();

    void attach(Layout *layout, DockLookup dockByKey, ActivityLookup activityFor);
    // 登记一个初始停靠在左槽的视图 pageKey（幂等）。
    void registerPage(const QString &pageKey);

    void setVisible(bool visible);
    bool visible() const { return m_visible; }
    bool toggle();

    // 宽度：优先读实际停靠区，未装配时退回 Layout 的区域尺寸记录。
    int width() const;
    void setWidth(int width);

    bool isLeftPage(const QString &pageKey) const { return m_model.contains(pageKey); }
    QList<ads::CDockWidget *> docks() const;

    // 容器模型（VSCode `ViewContainerModel`）：视图清单/收起态/当前视图的唯一归属。
    ViewContainerModel &model() { return m_model; }
    const ViewContainerModel &model() const { return m_model; }

    int sectionCount() const { return m_model.viewCount(); }
    QStringList sectionIds() const { return m_model.views(); }
    QWidget *sectionWidget(const QString &id) const;
    QString sectionActivity(const QString &id) const;
    bool setSectionExpanded(const QString &id, bool expanded);
    bool isSectionExpanded(const QString &id) const;
    bool isCollapsed(const QString &id) const { return m_model.isCollapsed(id); }
    // 只记口径、不动 dock 显隐（拖拽归位等场景由调用方决定何时切视图）。
    void setCollapsed(const QString &id, bool collapsed);

    const QStringList &pageKeys() const { return m_model.viewsRef(); }

protected:
    QWidget *createContent(QWidget *parent) override;

private:
    Layout *m_layout = nullptr;
    DockLookup m_dockByKey;
    ActivityLookup m_activityFor;
    ViewContainerModel m_model; // 左栏容器模型（视图清单/收起态/当前视图）
    bool m_visible = true;
};

} // namespace nextpilot::plotscope::gui
