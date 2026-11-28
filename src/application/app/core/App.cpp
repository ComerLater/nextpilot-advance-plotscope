#include "App.h"
#include "Intent.h"

#include "Toolbox.h"
#include "PluginScanner.h"
#include "ExtensionScanner.h"
#include "RecordingBackend.h"
#include "RecordingFormat.h"

#include <QCoreApplication>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;
using namespace nextpilot::plotscope::rec;
using namespace nextpilot::plotscope::host;

namespace nextpilot::plotscope::app {

App::~App()
{
    // 宿主析构（deactivateAll）会晚于服务成员析构，先在这里停掉扩展，
    // deactivate 幂等（instance 置空后直接返回），宿主析构再调一次是空转。
    m_host.deactivateAll();
}

App::App(const QString &settingsPath, QObject *parent)
    : QObject(parent), m_ctx(settingsPath, this), m_host(m_ctx, this), m_recorder(),
      m_services(m_ctx, m_recorder, m_host, this),
      m_extensions(m_ctx, m_host, m_services.services(), m_services.contributionsInstaller())
{
    // A1b §1.4.4：服务容器构造完毕后注入宿主，initialize 用它初始化扩展。
    m_host.setServices(m_services.services());

    // A8b-M2 裁定 52442006 Step 3：灌回入口接线到装配层（ContributionsInstaller），
    // ExtensionService 收集 contributes 后调它——壳（GuiContext）不再灌回。
    m_host.setContributionsInstaller(m_services.contributionsInstaller());
    // 生命周期转发：宿主不持有扩展表/拓扑/装载语义，归 ExtensionService。
    m_host.setExtensionService(&m_extensions);

    // 工具箱（extensions）：业务能力包，不参与扩展生命周期。
    // 路径一——内置扩展包：各自静态自注册，装载时统一收进工具箱。
    nextpilot::plotscope::ext::registerBuiltinPacks((*static_cast<nextpilot::plotscope::registry::ExtensionRegistry *>(
        m_ctx.registryTree().find(nextpilot::plotscope::registry::RegistryKind::Extension))));
    // 路径二——外部插件：扫描目录、校验后注册进同一个工具箱（本轮仅预留入口）。
    // 目录可后续由配置/命令行注入；默认检查可执行文件旁的 plugins/ 目录。
    nextpilot::plotscope::ext::scanExternalPlugins(
        (*static_cast<nextpilot::plotscope::registry::ExtensionRegistry *>(
            m_ctx.registryTree().find(nextpilot::plotscope::registry::RegistryKind::Extension))),
        QCoreApplication::applicationDirPath() + QStringLiteral("/plugins"));

    // A8b-M1：内置扩展目录扫描（vscode contributes 契约）。只登记 contributes、
    // 不执行扩展代码；每个 owner 的声明作为一条 Extension 贡献 mount 进同一注册表。
    const QString extRoot = QCoreApplication::applicationDirPath() + QStringLiteral("/extensions");
    const auto extReport = nextpilot::plotscope::registry::scanExtensions(extRoot);
    for (const auto &doc : extReport.docs)
        (*static_cast<nextpilot::plotscope::registry::ExtensionRegistry *>(
             m_ctx.registryTree().find(nextpilot::plotscope::registry::RegistryKind::Extension)))
            .mountContribution(doc);
    for (const auto &issue : extReport.issues)
        (*static_cast<nextpilot::plotscope::registry::ExtensionRegistry *>(
             m_ctx.registryTree().find(nextpilot::plotscope::registry::RegistryKind::Extension)))
            .addContributionDiagnostic(issue.keyPath, issue.keyPath, issue.message);

    // 录制：格式 × 后端两个独立注册表。
    // 格式决定"字节长什么样"（raw/mcap），后端决定"字节送到哪"（文件/UDP）。
    // 正交组合使"同时录多种格式、多种后端"成为自然结果。
    // 二者都是 app 的机制选项，不含领域语义——不走扩展包贡献体系。
    nextpilot::plotscope::rec::RecordingFormatHub::installBuiltinFormats();
    nextpilot::plotscope::rec::RecordingBackendHub::installBuiltinBackends();
}

QHash<QString, QString> App::startup(bool headless, const QStringList &disabled)
{
    // 录制器挂到宿主：脚本执行时门面会把业务意图录进去。
    m_host.setRecorder(&m_recorder);
    // headless 时 ui 为空 -> 没有任何扩展创建 QWidget。
    return m_host.activate(disabled, nullptr);
}

} // namespace nextpilot::plotscope::app
