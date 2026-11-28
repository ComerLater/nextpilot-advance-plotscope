#include "ViewContainerModel.h"

namespace nextpilot::plotscope::gui {

void ViewContainerModel::addView(const QString &viewId)
{
    if (!viewId.isEmpty() && !m_views.contains(viewId))
        m_views << viewId;
}

void ViewContainerModel::removeView(const QString &viewId)
{
    m_views.removeAll(viewId);
    m_collapsed.removeAll(viewId);
    if (m_activeView == viewId)
        m_activeView.clear();
}

void ViewContainerModel::setCollapsed(const QString &viewId, bool collapsed)
{
    if (viewId.isEmpty())
        return;
    if (collapsed)
    {
        if (!m_collapsed.contains(viewId))
            m_collapsed << viewId;
        return;
    }
    m_collapsed.removeAll(viewId);
}

void ViewContainerModel::setActiveView(const QString &viewId)
{
    if (m_views.contains(viewId))
        m_activeView = viewId;
}

} // namespace nextpilot::plotscope::gui
