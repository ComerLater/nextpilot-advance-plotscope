#pragma once

#include "Dimension.h"

#include <QString>

class QWidget;

namespace nextpilot::plotscope::gui {

// Part 基类（VSCode `src/vs/workbench/browser/part.ts`）。
//
// 只做三件事：创建容器、按 Dimension 布局、记录可见性。
// **不持有视图/业务状态**——状态归各 Part 自己（M4 后续步骤按 VSCode 搬）。
class Part
{
public:
    explicit Part(const QString &id);
    virtual ~Part();

    // 幂等：内容只创建一次；重复调用只改 parent。
    void create(QWidget *parent);
    virtual void layout(const Dimension &dimension);
    virtual int minimumWidth() const { return 0; }
    virtual int minimumHeight() const { return 0; }

    void setVisible(bool visible);
    bool isVisible() const { return m_visible; }

    QWidget *container() const { return m_container; }
    const QString &id() const { return m_id; }

protected:
    // 由派生类提供：本 Part 的根控件（parent 已由 create() 给出）。
    virtual QWidget *createContent(QWidget *parent) = 0;

private:
    QString m_id;
    QWidget *m_container = nullptr;
    bool m_visible = true;
};

} // namespace nextpilot::plotscope::gui
