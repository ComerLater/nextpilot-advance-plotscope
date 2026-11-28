#include "ActivitybarPart.h"

#include "ActivityBar.h"

#include "ProductSpec.h"

#include <QWidget>

namespace nextpilot::plotscope::gui {

ActivitybarPart::ActivitybarPart() : Part(QStringLiteral("workbench.activitybar")) {}

void ActivitybarPart::notifyActivated(const nextpilot::plotscope::registry::ActivitySpec &spec) const
{
    if (m_activatedHandler)
        m_activatedHandler(spec);
}

void ActivitybarPart::attach(ActivityBar *bar)
{
    if (bar)
        m_bar = bar;
}

void ActivitybarPart::setBadge(const QString &activityId, int count)
{
    if (m_bar)
        m_bar->setBadge(activityId, count);
}

int ActivitybarPart::badge(const QString &activityId) const { return m_bar ? m_bar->badge(activityId) : 0; }

QStringList ActivitybarPart::activityIds() const { return m_bar ? m_bar->activityIds() : QStringList(); }

void ActivitybarPart::setBarVisible(bool visible)
{
    m_visible = visible;
    if (m_bar)
        m_bar->setVisible(visible);
}

QWidget *ActivitybarPart::createContent(QWidget *parent)
{
    Q_UNUSED(parent);
    return m_bar;
}

} // namespace nextpilot::plotscope::gui
