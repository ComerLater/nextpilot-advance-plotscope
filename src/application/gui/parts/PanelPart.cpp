#include "PanelPart.h"

// ADS（vendored）：用相对路径引入，避开与壳目录同名文件的歧义。
#include "../../../3rdparty/ads/src/DockAreaWidget.h"
#include "../../../3rdparty/ads/src/DockWidget.h"

#include "Region.h"

#include <QWidget>

namespace nextpilot::plotscope::gui {

namespace {
namespace reg = nextpilot::plotscope::registry;

bool areaVisible(ads::CDockWidget *dock)
{
    if (!dock)
        return false;
    if (ads::CDockAreaWidget *area = dock->dockAreaWidget())
        return area->isVisible();
    return dock->isVisible();
}

bool toggleArea(ads::CDockWidget *dock)
{
    if (!dock)
        return false;
    if (ads::CDockAreaWidget *area = dock->dockAreaWidget())
    {
        area->setVisible(!area->isVisible());
        return true;
    }
    return false;
}
} // namespace

PanelPart::PanelPart() : Part(QStringLiteral("workbench.panel")) {}

void PanelPart::attach(DockLookup dockOf, DocksLookup docksOf)
{
    m_dockOf = std::move(dockOf);
    m_docksOf = std::move(docksOf);
}

bool PanelPart::bottomPanelVisible() const { return areaVisible(m_dockOf ? m_dockOf(reg::Region::Panel) : nullptr); }

bool PanelPart::toggleBottomPanel() { return toggleArea(m_dockOf ? m_dockOf(reg::Region::Panel) : nullptr); }

bool PanelPart::auxZoneVisible() const
{
    return areaVisible(m_dockOf ? m_dockOf(reg::Region::SecondarySidebar) : nullptr);
}

bool PanelPart::toggleAuxZone() { return toggleArea(m_dockOf ? m_dockOf(reg::Region::SecondarySidebar) : nullptr); }

QWidget *PanelPart::panelWidget(reg::Region region) const
{
    ads::CDockWidget *dock = m_dockOf ? m_dockOf(region) : nullptr;
    return dock ? dock->widget() : nullptr;
}

QList<QWidget *> PanelPart::panelWidgets(reg::Region region) const
{
    ads::CDockWidget *first = m_dockOf ? m_dockOf(region) : nullptr;
    if (!first)
        return {};
    QList<QWidget *> out;
    ads::CDockAreaWidget *area = first->dockAreaWidget();
    if (!area)
    {
        out.append(first->widget());
        return out;
    }
    for (ads::CDockWidget *dw : area->dockWidgets())
        out.append(dw->widget());
    return out;
}

QString PanelPart::panelTitle(reg::Region region) const
{
    ads::CDockWidget *dock = m_dockOf ? m_dockOf(region) : nullptr;
    return dock ? dock->windowTitle() : QString();
}

QStringList PanelPart::panelTitles(reg::Region region) const
{
    QStringList out;
    if (!m_docksOf)
        return out;
    for (ads::CDockWidget *dw : m_docksOf(region))
        out << dw->windowTitle();
    return out;
}

QString PanelPart::activePanelTitle(reg::Region region) const
{
    if (!m_docksOf)
        return QString();
    const QList<ads::CDockWidget *> docks = m_docksOf(region);
    if (docks.isEmpty() || !docks.first())
        return QString();
    ads::CDockAreaWidget *area = docks.first()->dockAreaWidget();
    if (!area)
        return QString();
    ads::CDockWidget *cur = area->currentDockWidget();
    return cur ? cur->windowTitle() : QString();
}

QString PanelPart::visiblePanelTitle(reg::Region region) const
{
    if (!m_docksOf)
        return QString();
    for (ads::CDockWidget *dw : m_docksOf(region))
    {
        if (!dw)
            continue;
        QWidget *content = dw->widget();
        if (content && content->isVisible())
            return dw->windowTitle();
    }
    return QString();
}

QStringList PanelPart::contentsOrder(reg::Region region) const
{
    QStringList out;
    if (!m_docksOf)
        return out;
    const QList<ads::CDockWidget *> docks = m_docksOf(region);
    if (docks.isEmpty() || !docks.first())
        return out;
    ads::CDockAreaWidget *area = docks.first()->dockAreaWidget();
    if (!area)
        return out;
    for (int i = 0; i < area->dockWidgetsCount(); ++i)
    {
        ads::CDockWidget *dw = area->dockWidget(i);
        out << (dw ? dw->windowTitle() : QString());
    }
    return out;
}

QWidget *PanelPart::createContent(QWidget *parent)
{
    Q_UNUSED(parent);
    return nullptr;
}

} // namespace nextpilot::plotscope::gui
