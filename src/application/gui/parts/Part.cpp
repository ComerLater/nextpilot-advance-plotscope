#include "Part.h"

#include <QWidget>

namespace nextpilot::plotscope::gui {

Part::Part(const QString &id) : m_id(id) {}

Part::~Part() = default;

void Part::create(QWidget *parent)
{
    if (!m_container)
        m_container = createContent(parent);
    else if (parent && m_container->parentWidget() != parent)
        m_container->setParent(parent);
    if (m_container)
        m_container->setVisible(m_visible);
}

void Part::layout(const Dimension &) {}

void Part::setVisible(bool visible)
{
    m_visible = visible;
    if (m_container)
        m_container->setVisible(visible);
}

} // namespace nextpilot::plotscope::gui
