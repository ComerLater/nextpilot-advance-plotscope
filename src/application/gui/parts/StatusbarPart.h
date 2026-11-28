#pragma once

#include "Part.h"

#include <QRect>
#include <QString>

class QStatusBar;
class QWidget;

namespace nextpilot::plotscope::gui {

// Statusbar 部件（VSCode `src/vs/workbench/browser/parts/statusbar/statusbarPart.ts`）。
//
// 状态栏本体由 QMainWindow 持有，本部件接管读数与写入面（VSCode `IStatusbarService`
// 的壳内实现）：提示文本、左右条目挂载都在这里，壳外不再直接摸 QStatusBar。
class StatusbarPart : public Part
{
public:
    StatusbarPart();

    void attach(QStatusBar *bar);
    QStatusBar *bar() const { return m_bar; }

    bool visible() const;
    // 窗口坐标系下的矩形（无状态栏时为空矩形）。
    QRect rectIn(QWidget *window) const;

    // --- IStatusbarService（壳内实现面）---
    void showMessage(const QString &text, int timeoutMs = 0);
    void clearMessage();
    void addWidget(QWidget *widget, int stretch = 0);
    void addPermanentWidget(QWidget *widget, int stretch = 0);
    void removeWidget(QWidget *widget);

protected:
    QWidget *createContent(QWidget *parent) override;

private:
    QStatusBar *m_bar = nullptr; // 借用，QMainWindow 拥有
};

} // namespace nextpilot::plotscope::gui
