#pragma once

#include "IWorkbenchContribution.h"
#include "IWorkspace.h"
#include "IIntentExecutor.h"
#include "JobRecorder.h"
#include "Result.h"
#include "AppContext.h"
#include "InteractionHub.h"
#include "StaticRegistryNodes.h"
#include "UiSlotHub.h"
#include "MenuRegistry.h"
#include "IngestPipeline.h"
#include "IContributionsInstaller.h"
#include "IExtensionService.h"
#include "DataLoadHandler.h"
#include "DataCommandsHandler.h"
#include "AIToolsHandler.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace nextpilot::plotscope::instantiation {
class InstantiationService; // 只前向声明：host 不 include 装配层服务容器头
}

namespace nextpilot::plotscope::host {

// 宿主：执行/服务/注册表面。扩展装载、拓扑、实例归属归装配层 ExtensionService
// （对位 AbstractExtensionService.activateExtension）；本类只持有进程级运行态、
// 意图分发与系统级处理器。
class RuntimeHost : public QObject, public nextpilot::plotscope::intent::IIntentExecutor
{
    Q_OBJECT
public:
    // 宿主持有应用上下文（进程级运行态的唯一入口）。
    explicit RuntimeHost(nextpilot::plotscope::context::AppContext &appCtx, QObject *parent = nullptr);
    ~RuntimeHost() override;

    // A1b §1.4.4：装配层在构造好服务容器后注入；initialize 用它初始化扩展。
    // 只存引用不拥有；只前向声明，host 不 include 装配层头。
    void setServices(nextpilot::plotscope::instantiation::InstantiationService &services) { m_services = &services; }

    // 壳读回服务容器（A8b-M2 裁定 52441633 §三）：MainWindow::build 经
    // getService<IPanelsService> 等取壳读回接口。bare 壳没有装配服务，返回 nullptr。
    nextpilot::plotscope::instantiation::InstantiationService *services() { return m_services; }

    // 装配层注入的贡献灌回入口（ContributionsInstaller）。
    void setContributionsInstaller(nextpilot::plotscope::workbench::IContributionsInstaller *installer)
    {
        m_contribInstaller = installer;
    }
    nextpilot::plotscope::workbench::IContributionsInstaller *contributionsInstaller() const
    {
        return m_contribInstaller;
    }

    // 装配层注入：壳经此读 MenuRegistry（VSCode 菜单服务）。
    void setMenuRegistry(nextpilot::plotscope::registry::MenuRegistry *menus) { m_menuRegistry = menus; }
    nextpilot::plotscope::registry::MenuRegistry *menuRegistry() const { return m_menuRegistry; }

    // 摄取管道：宿主持有；对外读面由装配层 IngestAdapter 转成 IIngest。
    nextpilot::plotscope::context::IngestPipeline &ingestPipeline() { return m_ingest; }

    // 生命周期转发给装配层 ExtensionService（宿主不持有扩展表/拓扑/装载语义）。
    void setExtensionService(nextpilot::plotscope::workbench::IExtensionService *extensionService)
    {
        m_extensionService = extensionService;
    }
    QHash<QString, QString> activate(const QStringList &disabledIds = {}, IUiContext *ui = nullptr);
    bool deactivate(const QString &extensionId);
    void deactivateAll();
    bool isActive(const QString &extensionId) const;
    QStringList activeIds() const;
    // 已注册的意图 op 列表（供 ai.tools.list 暴露给 AI/MCP 工具层）。
    QStringList registeredOps() const { return m_handlers.keys(); }
    QList<nextpilot::plotscope::registry::ExtensionDescription> availableExtensions() const;

    // 扩展在 initialize 期间注册的意图处理器，归属到当前初始化的扩展。
    void registerIntentHandler(nextpilot::plotscope::intent::IIntentHandler *handler);

    // 注册**系统级**意图处理器：不归属任何可卸载扩展，用保留 key 直接入池。
    void registerSystemHandler(const QString &key, nextpilot::plotscope::intent::IIntentHandler *handler);

    // 执行意图：分派给声明了对应能力的扩展。（实现 IIntentExecutor）
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult>
    execute(const nextpilot::plotscope::intent::Intent &intent) override;

    // 录制器：非空时 ScriptFacade 会把成功执行的业务意图录进去。
    void setRecorder(nextpilot::plotscope::intent::JobRecorder *recorder) { m_recorder = recorder; }
    nextpilot::plotscope::intent::JobRecorder *recorder() const { return m_recorder; }

    // 应用上下文：进程级运行态（数据集/设置/工具箱/数据源/摄取管道）。
    nextpilot::plotscope::context::AppContext &app() { return m_appCtx; }
    const nextpilot::plotscope::context::AppContext &app() const { return m_appCtx; }

    // 交互注册表：逻辑归 host 编排，实例由 AppContext owns。
    nextpilot::plotscope::context::InteractionHub &interactions() { return m_appCtx.interactions(); }
    const nextpilot::plotscope::context::InteractionHub &interactions() const { return m_appCtx.interactions(); }
    // 视图动作分发转发（壳/回放入口调 host，注册仍走 AppContext.interactions()）。
    bool dispatchViewAction(const QString &id, const QVariantMap &params,
                            nextpilot::plotscope::foundation::Result<QVariant> &out) const
    {
        return m_appCtx.interactions().dispatchViewAction(id, params, out);
    }
    // 派生序列注册表：独立派生字段（不写回 dataset），实例归 AppContext。
    nextpilot::plotscope::context::DerivedSeriesHub &derivedHub() { return m_appCtx.derivedHub(); }
    const nextpilot::plotscope::context::DerivedSeriesHub &derivedHub() const { return m_appCtx.derivedHub(); }

    // 便捷转发（内部使用；扩展请通过 host.app() 取）。
    nextpilot::plotscope::workbench::IWorkspace &workspace() { return m_appCtx.workspace(); }
    nextpilot::plotscope::context::SettingsStore &settings() { return m_appCtx.settings(); }

    // UI 插槽登记表：扩展/骨架声明插槽，工具箱的 UI 贡献填内容。
    const nextpilot::plotscope::registry::UiSlotHub &uiSlots();
    nextpilot::plotscope::registry::UiSlotHub &uiSlotsMutable();

    // 初始化归属区间：ExtensionService 在 initialize 前后调用，供 handler 归属。
    void beginInitExtension(const QString &id) { m_initializing = id; }
    void endInitExtension() { m_initializing.clear(); }
    // 当前初始化扩展注册的意图处理器（ExtensionService 反激活时摘除用）。
    QList<nextpilot::plotscope::intent::IIntentHandler *> initExtensionHandlers() const;

    // 反激活时摘除某扩展注册的意图处理器（ExtensionService 调用）。
    void unregisterHandler(nextpilot::plotscope::intent::IIntentHandler *handler);

private:
    nextpilot::plotscope::context::AppContext &m_appCtx;
    nextpilot::plotscope::instantiation::InstantiationService *m_services = nullptr;
    nextpilot::plotscope::workbench::IContributionsInstaller *m_contribInstaller = nullptr;
    nextpilot::plotscope::registry::MenuRegistry *m_menuRegistry = nullptr;
    nextpilot::plotscope::workbench::IExtensionService *m_extensionService = nullptr;
    nextpilot::plotscope::registry::UiSlotHub m_uiSlotHub;
    nextpilot::plotscope::context::IngestPipeline m_ingest;
    DataLoadHandler m_dataLoadHandler;
    DataCommandsHandler m_dataCommandsHandler;
    AIToolsHandler m_aiToolsHandler;
    QHash<QString, nextpilot::plotscope::intent::IIntentHandler *> m_handlers; // op/key -> handler
    QString m_initializing; // 当前正在 initialize 的扩展 id，用于 handler 归属
    nextpilot::plotscope::intent::JobRecorder *m_recorder = nullptr;
};

} // namespace nextpilot::plotscope::host
