#pragma once

#include <QIcon>
#include <QList>
#include <QString>

#include <functional>

class QAction;
class QObject;

namespace nextpilot::plotscope::gui {

// VSCode `IAction`：动作条目的纯数据描述（id/文本/图标/勾选/快捷键/触发）。
struct IAction
{
    QString id;
    QString text;
    QString toolTip;
    QIcon icon;
    bool checkable = false;
    QString shortcut;
    std::function<void()> trigger;
};

// 动作栏（VSCode `src/vs/base/browser/ui/actionbar/actionbar.ts` 的创建/更新面）。
//
// 全仓唯一创建 `QAction` 的地方；生命周期接管在 RibbonPart / ViewPane 两笔。
class ActionBar
{
public:
    // 创建并绑定一个 QAction；parent 决定归属（窗口/RibbonPage/视图标题栏）。
    static QAction *create(QObject *parent, const IAction &item);

    // 就地更新：文字/图标/勾选态/快捷键/触发（次级/下拉在对应 Part 那笔补齐）。
    static void update(QAction *action, const IAction &item);

    // 撤销：从 parent 摘除并 deleteLater。
    static void dispose(QAction *action);
};

} // namespace nextpilot::plotscope::gui
