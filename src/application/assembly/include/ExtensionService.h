#pragma once

// 扩展服务（装配层唯一扩展服务）。对位 VSCode
// `src/vs/workbench/services/extensions/common/extensionService.ts` 的 ExtensionService；
// activationEvents 编排对位 `extensionsActivator.ts`（ExtensionsActivator）。
//
// 职责合并（旧 ExtensionActivationService 名称退场）：
//   1. 激活编排：内置扩展静态表 → 依赖拓扑排序 → 建实例 → initialize → mount contributes
//      → installer 灌回 → onAttach；反激活对称撤销；
//   2. contributes 灌写：经 ContributionsInstaller 写 StatusHub/ViewRegistry/MenuRegistry；
//   3. 命令绑定：把 contributes.commands 落地到 CommandRegistry，并注册单一执行 handler。
//
// 设计约束：src/application/gui 不获取扩展注册表；命令表由壳在 MainWindow.build 后传入。

#include "AppContext.h"
#include "ExtensionRegistry.h"
#include "IContributionsInstaller.h"
#include "IExtensionService.h"
#include "InstantiationService.h"
#include "MenuId.h"
#include "MenuRegistry.h"
#include "ViewRegistry.h"

#include <QHash>
#include <QObject>
#include <QString>

namespace nextpilot::plotscope::command {
class CommandRegistry;
}
namespace nextpilot::plotscope::host {
class IWorkbenchContribution;
}
namespace nextpilot::plotscope::command {
class CommandService;
}
namespace nextpilot::plotscope::intent {
class IIntentHandler;
}

namespace nextpilot::plotscope::assembly {
namespace wb = nextpilot::plotscope::workbench;
namespace registry = nextpilot::plotscope::registry;
namespace ctx = nextpilot::plotscope::context;

// ---- ContributionsInstaller：contributes 灌回实现（安装器协作者，不是第二个激活服务）。
// 对位 VSCode 扩展激活时 contributes 向各注册点的注册；写入 StatusHub/
// ViewRegistry/MenuRegistry，壳只渲染。
class ContributionsInstaller final : public wb::IContributionsInstaller
{
public:
    ContributionsInstaller(ctx::AppContext &appCtx, registry::ViewRegistry &views, registry::MenuRegistry &menus)
        : m_appCtx(appCtx), m_views(views), m_menus(menus)
    {
    }

    void install(const registry::ContributesDoc &c) override;

private:
    ctx::AppContext &m_appCtx;
    registry::ViewRegistry &m_views;
    registry::MenuRegistry &m_menus;
};

// ---- ExtensionService：唯一扩展服务，实现 workbench::IExtensionService。
class ExtensionService final : public QObject, public wb::IExtensionService
{
    Q_OBJECT
public:
    // 只收服务容器与命令服务：扩展的父对象是本服务，命令注册走 CommandService。
    ExtensionService(ctx::AppContext &appCtx, command::CommandService &commands,
                     nextpilot::plotscope::instantiation::InstantiationService &services,
                     wb::IContributionsInstaller *installer);

    // IExtensionService 对位方法。
    QHash<QString, QString> activateByEvent(const QString &activationEvent, const QStringList &disabledIds,
                                            nextpilot::plotscope::host::IUiContext *ui) override;
    bool deactivateExtension(const QString &extensionId) override;
    bool isActivated(const QString &extensionId) const override;
    QStringList getActivatedExtensions() const override;
    QList<registry::ExtensionDescription> getExtensions() const override;

    // 命令绑定：把当前挂载的贡献命令落地到命令表；handler 常驻注册一次。
    void bindCommands(command::CommandRegistry *commands);
    void unbind(const QString &owner, command::CommandRegistry *commands);
    int triggeredCount(const QString &commandId) const;
    int boundCommandCount() const { return m_bound.size(); }

private:
    class ContributedCommandHandler; // 单一贡献命令执行体（cpp 内定义）

    struct ActiveExtension
    {
        registry::ExtensionDescription description;
        nextpilot::plotscope::host::IWorkbenchContribution *instance = nullptr;
    };

    // 扩展包 contributes 的 viewsContainers/views 在 bind 时灌进 ViewRegistry（服务面取回）。
    void attachViewRegistry();

    ctx::AppContext &m_appCtx;
    command::CommandService &m_commands;
    nextpilot::plotscope::instantiation::InstantiationService &m_services;
    wb::IContributionsInstaller *m_installer;
    registry::ExtensionRegistry &m_registry;
    registry::ViewRegistry *m_views = nullptr;
    QHash<QString, ActiveExtension> m_active;
    QHash<QString, QString> m_bound; // command id -> owner
    class ContributedCommandHandler *m_handler = nullptr;
};

} // namespace nextpilot::plotscope::assembly
