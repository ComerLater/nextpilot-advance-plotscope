#pragma once

#include "AppContext.h"
#include "AIToolsHandler.h"
#include "command/CommandService.h"
#include "DataCommandsHandler.h"
#include "DataLoadHandler.h"
#include "ExtensionService.h"
#include "IDatasetAccess.h"
#include "IngestPipeline.h"
#include "InstantiationService.h"
#include "JobRecorder.h"
#include "MenuRegistry.h"
#include "UiSlotHub.h"
#include "WorkbenchServices.h"

#include <QObject>

namespace nextpilot::plotscope::app {

// 组合根：**app 的装配点**（VSCode 对位 Workbench 的装配职责）。
//
// 它做三件事，且只做这三件：
//  1. 构造 AppContext（进程级运行态的唯一持有者）；
//  2. 构造服务层（CommandService / UiSlotHub / 摄取管道 / 系统命令 handler），
//     并把已自注册的模块工厂与扩展包装进扩展服务；
//  3. 驱动启动（headless / GUI 两种模式）。
//
// 它**不持有**业务状态——数据集、设置、工具箱都在 AppContext 里。
// 也不再有"宿主"这一层：命令执行归 CommandService、扩展生命周期归 ExtensionService，
// 两者都是可独立替换的服务，不再由一个运行时对象转手。
//
// GUI 与 CLI 共用这一个类；区别只在是否传入 IUiContext。
class App : public QObject
{
    Q_OBJECT
public:
    // settingsPath 为空时设置存内存（测试/headless）；否则读写该 JSON 文件。
    explicit App(const QString &settingsPath = QString(), QObject *parent = nullptr);
    // 析构序：先停扩展（对称撤销，模块自己的命令随 onDetach 注销），再拆服务层。
    ~App() override;

    // 进程级运行态。
    nextpilot::plotscope::context::AppContext &context() { return m_ctx; }
    const nextpilot::plotscope::context::AppContext &context() const { return m_ctx; }

    // 服务层（按 VSCode 名字/职责取用）。
    nextpilot::plotscope::command::CommandService &commands() { return m_commands; }
    nextpilot::plotscope::assembly::ExtensionService &extensions() { return m_extensions; }
    nextpilot::plotscope::assembly::WorkbenchServices &workbench() { return m_workbench; }
    nextpilot::plotscope::instantiation::InstantiationService &services() { return m_workbench.services(); }
    nextpilot::plotscope::registry::MenuRegistry &menuRegistry() { return m_workbench.menuRegistry(); }
    nextpilot::plotscope::registry::UiSlotHub &slotHub() { return m_slots; }
    nextpilot::plotscope::context::IngestPipeline &ingest() { return m_ingest; }
    nextpilot::plotscope::intent::JobRecorder &recorder() { return m_recorder; }

    // 便捷转发（内部使用）。
    // vscode.workspace 对位：业务读面统一从服务面取，不经 AppContext 的 registry。
    nextpilot::plotscope::workbench::IWorkspace &workspace() { return m_ctx.workspace(); }
    nextpilot::plotscope::dataset::IDatasetAccess &data() { return m_ctx.workspace().dataset(); }

    // 停掉全部扩展（对称撤销，幂等）：退出路径与测试收尾的唯一入口。
    void stop();

    // headless=true 时不建 UI 上下文，模块的 onAttach 不被调用。
    QHash<QString, QString> startup(bool headless, const QStringList &disabled = {});

private:
    nextpilot::plotscope::context::AppContext m_ctx;
    // 注册表成员：构造即上树（AppContext 的注册树是唯一归属）。
    nextpilot::plotscope::registry::UiSlotHub m_slots;
    nextpilot::plotscope::command::CommandService m_commands;
    nextpilot::plotscope::context::IngestPipeline m_ingest;
    // 系统命令 handler：不归属任何可卸载扩展，注册进 CommandService 的系统命令位。
    DataLoadHandler m_dataLoadHandler;
    DataCommandsHandler m_dataCommandsHandler;
    AIToolsHandler m_aiToolsHandler;
    nextpilot::plotscope::intent::JobRecorder m_recorder;
    nextpilot::plotscope::assembly::WorkbenchServices m_workbench;
    nextpilot::plotscope::assembly::ExtensionService m_extensions;
};

} // namespace nextpilot::plotscope::app
