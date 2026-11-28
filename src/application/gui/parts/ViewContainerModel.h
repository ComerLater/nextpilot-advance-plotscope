#pragma once

#include <QString>
#include <QStringList>

namespace nextpilot::plotscope::gui {

// 视图容器模型（VSCode `src/vs/workbench/browser/parts/views/viewContainerModel.ts`）。
//
// 一个容器（Activity 图标下的侧栏组）里的视图清单、可见/收起态与当前视图的唯一归属；
// 容器本体是哪个 Part（SidebarPart）不归这里管，模型只回答"这个容器里有哪些视图、
// 哪个收起、哪个当前"。
class ViewContainerModel
{
public:
    void setContainerId(const QString &containerId) { m_containerId = containerId; }
    QString containerId() const { return m_containerId; }

    // 视图清单：登记幂等，顺序即声明顺序。
    void addView(const QString &viewId);
    void removeView(const QString &viewId);
    QStringList views() const { return m_views; }
    const QStringList &viewsRef() const { return m_views; }
    bool contains(const QString &viewId) const { return m_views.contains(viewId); }
    int viewCount() const { return int(m_views.size()); }

    // 收起态（用户把某视图折叠）：只记口径，不动 dock 显隐。
    void setCollapsed(const QString &viewId, bool collapsed);
    bool isCollapsed(const QString &viewId) const { return m_collapsed.contains(viewId); }

    void setActiveView(const QString &viewId);
    QString activeView() const { return m_activeView; }

private:
    QString m_containerId;
    QStringList m_views;
    QStringList m_collapsed; // 收起清单（少而小，QList 语义）
    QString m_activeView;
};

} // namespace nextpilot::plotscope::gui
