#pragma once

#include "Part.h"

#include <QIcon>
#include <QList>
#include <QString>
#include <QStringList>

class QWidget;

namespace ads {
class CDockManager;
class CDockWidget;
}

namespace nextpilot::plotscope::gui {

// Editor 部件（VSCode `src/vs/workbench/browser/parts/editor/editorPart.ts`）。
//
// 编辑器区是壳层固定主体：不接受其它视图停靠、页签不可拖出（Movable/Floatable=false）。
// 画布先到就占主位，之后到达的编辑器（脚本等）并成同区页签。
class EditorPart : public Part
{
public:
    EditorPart();

    // 接管 ADS 管理器（借用，生命周期归 ShellBinder）。
    void attach(ads::CDockManager *dock);
    ads::CDockWidget *editorDock() const { return m_editorDock; }

    // 装编辑器区（幂等）：内容未到时先摆空态占位，保证侧边 dock 排在它之后。
    void install();
    bool installed() const { return m_installed; }

    // 首个内容替换占位；之后的内容并成页签。返回承载它的页签（未装时 nullptr）。
    ads::CDockWidget *setContent(QWidget *content, const QString &title, const QIcon &icon);

    QWidget *content() const { return m_content; }
    QList<QWidget *> widgets() const;
    QStringList titles() const;
    int currentIndex() const;
    bool activateIndex(int index);
    QString currentTitle() const;
    bool closeCurrent();

protected:
    QWidget *createContent(QWidget *parent) override;

private:
    ads::CDockManager *m_dock = nullptr;
    ads::CDockWidget *m_editorDock = nullptr;
    QWidget *m_content = nullptr;
    bool m_installed = false;
    int m_seq = 0;
};

} // namespace nextpilot::plotscope::gui
