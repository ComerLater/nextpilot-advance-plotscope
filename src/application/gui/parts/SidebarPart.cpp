#include "SidebarPart.h"

// ADS（vendored）：用相对路径引入，避开与壳目录同名文件的歧义。
#include "../../../3rdparty/ads/src/DockAreaWidget.h"
#include "../../../3rdparty/ads/src/DockWidget.h"

#include "Layout.h"

#include "Region.h"

#include <QWidget>

namespace nextpilot::plotscope::gui {

namespace {
namespace reg = nextpilot::plotscope::registry;
}

SidebarPart::SidebarPart() : Part(QStringLiteral("workbench.sidebar")) {}

void SidebarPart::attach(Layout *layout, DockLookup dockByKey, ActivityLookup activityFor)
{
    if (layout)
        m_layout = layout;
    m_dockByKey = std::move(dockByKey);
    m_activityFor = std::move(activityFor);
}

void SidebarPart::registerPage(const QString &pageKey) { m_model.addView(pageKey); }

QList<ads::CDockWidget *> SidebarPart::docks() const
{
    QList<ads::CDockWidget *> out;
    for (const QString &id : m_model.views())
        if (ads::CDockWidget *dock = m_dockByKey ? m_dockByKey(id) : nullptr)
            out.append(dock);
    return out;
}

void SidebarPart::setVisible(bool visible)
{
    m_visible = visible;
    // 整栏显隐：逐 section 切显隐，section 不单独浮动。
    for (ads::CDockWidget *dock : docks())
        dock->toggleView(visible);
}

bool SidebarPart::toggle()
{
    setVisible(!m_visible);
    return m_visible;
}

int SidebarPart::width() const
{
    for (ads::CDockWidget *dock : docks())
        if (dock->dockAreaWidget())
            return dock->dockAreaWidget()->width();
    return m_layout ? m_layout->regionSize(reg::Region::PrimarySidebar).width : 256;
}

void SidebarPart::setWidth(int width)
{
    if (m_layout)
        m_layout->setRegionSize(reg::Region::PrimarySidebar, Dimension{qMax(170, width), 0});
}

QWidget *SidebarPart::sectionWidget(const QString &id) const
{
    ads::CDockWidget *dock = m_dockByKey ? m_dockByKey(id) : nullptr;
    return dock ? dock->widget() : nullptr;
}

QString SidebarPart::sectionActivity(const QString &id) const
{
    return isLeftPage(id) && m_activityFor ? m_activityFor(id) : QString();
}

void SidebarPart::setCollapsed(const QString &id, bool collapsed) { m_model.setCollapsed(id, collapsed); }

bool SidebarPart::setSectionExpanded(const QString &id, bool expanded)
{
    ads::CDockWidget *dock = m_dockByKey ? m_dockByKey(id) : nullptr;
    if (!dock || !isLeftPage(id))
        return false;
    setCollapsed(id, !expanded);
    dock->toggleView(expanded);
    return true;
}

bool SidebarPart::isSectionExpanded(const QString &id) const
{
    return bool(m_dockByKey) && m_dockByKey(id) != nullptr && !m_model.isCollapsed(id);
}

QWidget *SidebarPart::createContent(QWidget *parent)
{
    Q_UNUSED(parent);
    return nullptr;
}

} // namespace nextpilot::plotscope::gui
