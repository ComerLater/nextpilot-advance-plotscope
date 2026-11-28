#include "RibbonPart.h"

#include "SARibbonBar.h"

namespace nextpilot::plotscope::gui {

RibbonPart::RibbonPart() : Part(QStringLiteral("workbench.ribbon")) {}

void RibbonPart::attach(SARibbonBar *bar)
{
    if (bar)
        m_bar = bar;
}

bool RibbonPart::whenVisible(const QString &when) const
{
    // 空表达式=恒真；没有求值器时一律可见（headless/测试环境不崩）。
    return when.isEmpty() || !m_whenEval || m_whenEval(when);
}

QWidget *RibbonPart::createContent(QWidget *)
{
    // 容器就是宿主的 SARibbonBar，本部件不新建控件。
    return m_bar;
}

} // namespace nextpilot::plotscope::gui
