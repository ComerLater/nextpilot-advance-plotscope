#pragma once

#include "CompositeBar.h"

#include "Contribution.h"

#include <QPointer>
#include <QStringList>
#include <QWidget>

class QVBoxLayout;
class QToolButton;

namespace ads
{
class CDockWidget;
}

namespace nextpilot::plotscope::gui
{

// VS Code 式 Activity Bar（VSCode `activitybarPart.ts` 的控件本体）。
//
// 条目容器归 CompositeBar（VSCode `compositebar.ts`）：本类只负责竖条外形、
// 设置入口与"把 dock 页签拖到图标上"的落点判定；切换侧栏/ribbon 由 ShellBinder 编排。
class ActivityBar final : public QWidget
{
    Q_OBJECT
public:
    explicit ActivityBar(QWidget *parent = nullptr);

    void setActivities(const QList<nextpilot::plotscope::registry::ActivitySpec> &activities);
    void setActiveActivity(const QString &id);
    QString activeActivity() const { return m_activeId; }
    QStringList activityIds() const;

    void setBadge(const QString &id, int count);
    int badge(const QString &id) const;
    QString activityAt(const QPoint &globalPos) const;

signals:
    void activityActivated(const QString &id);
    void settingsRequested();
    // 用户把视图页签拖到 Activity Bar：activityId 为空表示落在空白处，
    // 由外壳新建一个 Activity Bar 图标。
    void dockDropped(ads::CDockWidget *dock, const QString &activityId);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    QVBoxLayout *m_layout = nullptr;
    CompositeBar *m_bar = nullptr; // 条目容器（本类拥有）
    QToolButton *m_settings = nullptr;
    QString m_activeId;
    QPointer<ads::CDockWidget> m_dragDock;
    QPoint m_pressPos;
    bool m_dragging = false;
};

} // namespace nextpilot::plotscope::gui
