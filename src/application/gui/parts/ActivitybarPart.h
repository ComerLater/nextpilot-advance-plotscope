#pragma once

#include "Part.h"

#include <QHash>
#include <QStringList>

#include <functional>

class QWidget;

namespace nextpilot::plotscope::registry {
struct ActivitySpec;
}

namespace nextpilot::plotscope::workbench {
class IViewsService;
}

namespace nextpilot::plotscope::gui {

class ActivityBar;

// Activitybar 部件（VSCode `src/vs/workbench/browser/parts/activitybar/activitybarPart.ts`）。
//
// 持 Activity Bar 控件、壳框架、可见/顺序/徽标读数，以及视图归属与激活态；
// dock 同步仍由 ShellBinder 完成——本部件不认识 dock 体系。
class ActivitybarPart : public Part
{
public:
    using ActivityActivatedHandler = std::function<void(const nextpilot::plotscope::registry::ActivitySpec &)>;

    ActivitybarPart();

    // 接管宿主的 ActivityBar（借用，由壳层创建与销毁）。
    void attach(ActivityBar *bar);
    ActivityBar *bar() const { return m_bar; }

    // 壳框架：ActivityBar | DockManager 的横向容器（借用，归属 QMainWindow）。
    void setShellFrame(QWidget *frame) { m_shellFrame = frame; }
    QWidget *shellFrame() const { return m_shellFrame; }

    // 视图读回服务（借用；裸壳为 nullptr）。Activity 声明从 containersIn("activitybar") 读。
    void setViewsService(nextpilot::plotscope::workbench::IViewsService *service) { m_views = service; }
    nextpilot::plotscope::workbench::IViewsService *viewsService() const { return m_views; }

    void setBadge(const QString &activityId, int count);
    int badge(const QString &activityId) const;
    QStringList activityIds() const;

    // 用户设置的 Activity 顺序（空 = 按声明顺序）。
    void setOrder(const QStringList &order) { m_order = order; }
    const QStringList &order() const { return m_order; }

    void setBarVisible(bool visible);
    bool barVisible() const { return m_visible; }

    // 视图归属：pageKey -> 用户拖到 Activity Bar 后的 activityId。
    void setViewActivity(const QString &pageKey, const QString &activityId)
    {
        m_viewActivity.insert(pageKey, activityId);
    }
    bool hasViewActivity(const QString &pageKey) const { return m_viewActivity.contains(pageKey); }
    QString viewActivity(const QString &pageKey) const { return m_viewActivity.value(pageKey); }

    const QString &activeActivity() const { return m_activeActivity; }
    void setActiveActivityId(const QString &activityId) { m_activeActivity = activityId; }

    void setActivatedHandler(ActivityActivatedHandler handler) { m_activatedHandler = std::move(handler); }
    // Activity 点击后的声明回传（绑定器不认识具体面板类型，只把声明交回外壳）。
    void notifyActivated(const nextpilot::plotscope::registry::ActivitySpec &spec) const;

protected:
    QWidget *createContent(QWidget *parent) override;

private:
    ActivityBar *m_bar = nullptr;
    QWidget *m_shellFrame = nullptr;
    nextpilot::plotscope::workbench::IViewsService *m_views = nullptr; // 借用
    QStringList m_order;
    QHash<QString, QString> m_viewActivity;
    QString m_activeActivity;
    ActivityActivatedHandler m_activatedHandler;
    bool m_visible = true;
};

} // namespace nextpilot::plotscope::gui
