#pragma once

#include "Contribution.h"

#include <QHash>
#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QString>

#include <functional>

namespace nextpilot::plotscope::registry {

// 菜单注册表（VSCode `src/vs/platform/actions/common/actions.ts` 的 `MenuRegistry`）。
//
// VSCode 形态：贡献在激活时静态登记菜单项（`appendMenuItem(menuId, item)`），
// 读取用 `getMenuItems(menuId)`，变更发 `onDidChangeMenu` 事件。
// 菜单不是动态数据源：没有 per-menu provider、没有壳侧注册。
// 本类不创建 QAction、不渲染——那是 gui 层 ActionBar / Part 的事。
// VSCode `IMenuItem`：动作本身 + 可选 group（同一菜单内的分组，VSCode 原字段）。
// SARibbon 的「组」只是渲染容器，读回时按 group 聚合，不另造层级类型。
struct IMenuItem
{
    UiAction action;
    QString group; // VSCode `IMenuItem.group`
    QString owner; // 本仓卸载归属（VSCode 用 IDisposable）
};

class MenuRegistry : public QObject
{
    Q_OBJECT

public:
    explicit MenuRegistry(QObject *parent = nullptr);

    // `MenuRegistry.appendMenuItem(menuId, item)`：VSCode 原签名；item.owner 用于卸载时整批撤销。
    // 同 menuId 同 id 先到者为准。
    void appendMenuItem(const QString &menuId, const IMenuItem &item);

    // 撤销某 owner 在 menuId 下的登记（扩展卸载路径）。
    int removeMenuItems(const QString &menuId, const QString &owner);

    // `MenuRegistry.getMenuItems`：登记顺序，同 id 只出第一条。
    QList<IMenuItem> getMenuItems(const QString &menuId) const;

    // `onDidChangeMenu`：只在该 menuId 变更时回调。
    QMetaObject::Connection onDidChangeMenu(const QString &menuId, std::function<void()> listener);

signals:
    void menuChanged(const QString &menuId);

private:
    QHash<QString, QList<IMenuItem>> m_items;
};

} // namespace nextpilot::plotscope::registry
