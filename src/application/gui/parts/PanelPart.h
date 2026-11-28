#pragma once

#include "Part.h"

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

// Panel 部件（VSCode `src/vs/workbench/browser/parts/panel/panelPart.ts`）。
//
// 底部面板与右侧辅助栏的整组显隐，以及按区域查询已停靠页签的口径
// （标题/内容顺序/当前页）都在这里；具体停靠动作仍由 ShellBinder 完成。
class PanelPart : public Part
{
public:
    // region -> 该区主 dock。
    using DockLookup = std::function<ads::CDockWidget *(nextpilot::plotscope::registry::Region region)>;
    // region -> 该区全部页签（按放置顺序）。
    using DocksLookup = std::function<QList<ads::CDockWidget *>(nextpilot::plotscope::registry::Region region)>;

    PanelPart();

    void attach(DockLookup dockOf, DocksLookup docksOf);

    // 底部 Panel（Ctrl+J）/ 右侧辅助栏（Ctrl+Alt+B）。
    bool bottomPanelVisible() const;
    bool toggleBottomPanel();
    bool auxZoneVisible() const;
    bool toggleAuxZone();

    int panelCount() const { return m_panelCount ? m_panelCount() : 0; }
    void setPanelCount(std::function<int()> count) { m_panelCount = std::move(count); }

    QWidget *panelWidget(nextpilot::plotscope::registry::Region region) const;
    QList<QWidget *> panelWidgets(nextpilot::plotscope::registry::Region region) const;
    QString panelTitle(nextpilot::plotscope::registry::Region region) const;
    QStringList panelTitles(nextpilot::plotscope::registry::Region region) const;
    QString activePanelTitle(nextpilot::plotscope::registry::Region region) const;
    QString visiblePanelTitle(nextpilot::plotscope::registry::Region region) const;
    QStringList contentsOrder(nextpilot::plotscope::registry::Region region) const;

protected:
    QWidget *createContent(QWidget *parent) override;

private:
    DockLookup m_dockOf;
    DocksLookup m_docksOf;
    std::function<int()> m_panelCount;
};

} // namespace nextpilot::plotscope::gui
