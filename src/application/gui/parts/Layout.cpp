#include "Layout.h"

#include "Part.h"

#include "Region.h"

namespace nextpilot::plotscope::gui {

namespace {
// 初始区域尺寸：与拆壳前 ShellBinder 的默认值一致（左栏 256，其余 0=由 ADS 决定）。
Dimension defaultRegionSize(nextpilot::plotscope::registry::Region region)
{
    switch (region)
    {
        case nextpilot::plotscope::registry::Region::PrimarySidebar: return Dimension{256, 0};
        default: return Dimension{0, 0};
    }
}
} // namespace

Layout::Layout() = default;
Layout::~Layout() = default;

void Layout::registerPart(Part *part)
{
    if (!part || m_parts.contains(part))
        return;
    m_parts.append(part);
}

bool Layout::unregisterPart(const QString &id)
{
    for (int i = 0; i < m_parts.size(); ++i)
    {
        if (m_parts.at(i)->id() == id)
        {
            m_parts.removeAt(i);
            return true;
        }
    }
    return false;
}

Part *Layout::part(const QString &id) const
{
    for (Part *p : m_parts)
        if (p->id() == id)
            return p;
    return nullptr;
}

Dimension Layout::regionSize(nextpilot::plotscope::registry::Region region) const
{
    return m_regionSizes.value(int(region), defaultRegionSize(region));
}

void Layout::setRegionSize(nextpilot::plotscope::registry::Region region, const Dimension &size)
{
    m_regionSizes.insert(int(region), size);
}

void Layout::layout(const Dimension &dimension)
{
    m_size = dimension;
    for (Part *p : m_parts)
        p->layout(dimension);
}

} // namespace nextpilot::plotscope::gui
