#include "App.h"
#include "Intent.h"

#include "DatasetRegistry.h"
#include "ErrorCodes.h"
#include "ExtensionRegistry.h"
#include "RegistryNode.h"
#include "RegistryTree.h"
#include "ShellContributions.h"
#include "SourceRegistry.h"
#include "StaticRegistryNodes.h"
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

namespace {
// A0 §八.2：AppContext 不再对外开 registry 访问器；按 kind 从注册树定位。
inline DatasetRegistry &datasetHub(AppContext &c)
{
    return *static_cast<DatasetRegistry *>(c.registryTree().find(RegistryKind::Dataset));
}
inline SourceRegistry &sourceHub(AppContext &c)
{
    return *static_cast<SourceRegistry *>(c.registryTree().find(RegistryKind::Source));
}
inline ExtensionRegistry &extensionHub(AppContext &c)
{
    return *static_cast<ExtensionRegistry *>(c.registryTree().find(RegistryKind::Extension));
}
} // namespace

App::~App()
{
    // 先停扩展：deactivate 幂等（instance 置空后直接返回），模块 onDetach 里注销自己的
    // 命令表项。服务层成员随后析构，扩展命令不会打在已析构的服务上。
    stop();
}

App::App(const QString &settingsPath, QObject *parent)
    : QObject(parent), m_ctx(settingsPath, this),
      // UiSlotHub 构造即上树（kind=UiSlot）；所有注册表归 AppContext 的注册树。
      m_slots(m_ctx.registryTree()), m_commands(),
      // 摄取管道：读字节 → 扇出 → 落库，实例归组合根，服务面经 IngestAdapter 读。
      m_ingest(datasetHub(m_ctx)),
      // data.load 走统一摄取管道：格式驱动只声明 parserId，解析实现由 SPI 解析器提供。
      m_dataLoadHandler(m_ctx, extensionHub(m_ctx), datasetHub(m_ctx), sourceHub(m_ctx), &m_ingest, &m_ctx.settings()),
      // data 派生能力（reload/export/recent/clearCache…）与 load 同属系统命令，归口一处。
      m_dataCommandsHandler(m_ctx, m_commands, m_ingest),
      // AI/MCP 工具层：ai.* 系统命令，暴露工具目录与调用入口，供后续 MCP bridge 接。
      m_aiToolsHandler(m_ctx, m_commands, [this]() { return m_commands.commandIds(); }), m_recorder(),
      m_workbench(m_ctx, m_recorder, m_commands, m_slots, m_ingest, this),
      m_extensions(m_ctx, m_commands, m_workbench.services(), m_workbench.contributionsInstaller())
{
    // 系统命令注册：不归属任何可卸载扩展，扩展装卸不影响。
    m_commands.registerSystemHandler(QStringLiteral("data.load"), &m_dataLoadHandler, {QStringLiteral("data.load")});
    m_commands.registerSystemHandler(
        QStringLiteral("data.commands"), &m_dataCommandsHandler,
        {QStringLiteral("data.reload"), QStringLiteral("data.export"), QStringLiteral("data.exportSnapshot"),
         QStringLiteral("data.params"), QStringLiteral("data.recent"), QStringLiteral("data.clearCache"),
         QStringLiteral("data.cache.stats"), QStringLiteral("data.remove"), QStringLiteral("data.pause"),
         QStringLiteral("data.resume"), QStringLiteral("data.derive"), QStringLiteral("data.filter"),
         QStringLiteral("data.marker"), QStringLiteral("data.state"), QStringLiteral("data.series"),
         QStringLiteral("data.workspace.save"), QStringLiteral("data.workspace.list"),
         QStringLiteral("data.workspace.restore")});
    m_commands.registerSystemHandler(
        QStringLiteral("ai.tools"), &m_aiToolsHandler,
        {QStringLiteral("ai.tools.list"), QStringLiteral("ai.tool.call"), QStringLiteral("ai.status")});

    // 壳级 Ribbon 页/组在**唯一一处**声明：ShellContributions（host 层）。
    // 业务页/组由各扩展 onAttach 自声明；区域是壳代码常量（Region 枚举），不进注册表。
    // headless 下这些槽也是纯数据，不创建 QWidget。
    declareShellContributions(m_slots);

    // 存量静态表全量上树（diag.registries 无盲区）；内置扩展节点并入 Extension 节点。
    new ParserNode(m_ctx.registryTree(), this);
    new RecordingFormatNode(m_ctx.registryTree(), this);
    new RecordingBackendNode(m_ctx.registryTree(), this);

    // diag.registries：全系统注册树的全景 dump（每节点一项）。
    m_ctx.interactions().registerViewAction(QStringLiteral("diag.registries"),
                                            [this](const QVariantMap &) -> Result<QVariant> {
                                                QVariantList list;
                                                for (RegistryNode *node : m_ctx.registryTree().nodes())
                                                {
                                                    QVariantMap item;
                                                    item.insert(QStringLiteral("kind"), kindName(node->kind()));
                                                    item.insert(QStringLiteral("name"), node->name());
                                                    item.insert(QStringLiteral("count"), node->count());
                                                    item.insert(QStringLiteral("keys"), node->keysAsStrings());
                                                    list.append(item);
                                                }
                                                return QVariant(list);
                                            });

    // 工具箱（extensions）：业务能力包，不参与扩展生命周期。
    // 路径一——内置扩展包：各自静态自注册，装载时统一收进工具箱。
    nextpilot::plotscope::ext::registerBuiltinPacks(extensionHub(m_ctx));
    // 路径二——外部插件：扫描目录、校验后注册进同一个工具箱（本轮仅预留入口）。
    nextpilot::plotscope::ext::scanExternalPlugins(extensionHub(m_ctx),
                                                   QCoreApplication::applicationDirPath() + QStringLiteral("/plugins"));

    // A8b-M1：内置扩展目录扫描（vscode contributes 契约）。只登记 contributes、
    // 不执行扩展代码；每个 owner 的声明作为一条 Extension 贡献 mount 进同一注册表。
    const QString extRoot = QCoreApplication::applicationDirPath() + QStringLiteral("/extensions");
    const auto extReport = nextpilot::plotscope::registry::scanExtensions(extRoot);
    for (const auto &doc : extReport.docs)
        extensionHub(m_ctx).mountContribution(doc);
    for (const auto &issue : extReport.issues)
        extensionHub(m_ctx).addContributionDiagnostic(issue.keyPath, issue.keyPath, issue.message);

    // 录制：格式 × 后端两个独立注册表。正交组合使"同时录多种格式、多种后端"成为自然结果。
    nextpilot::plotscope::rec::RecordingFormatHub::installBuiltinFormats();
    nextpilot::plotscope::rec::RecordingBackendHub::installBuiltinBackends();
}

void App::stop()
{
    const QStringList ids = m_extensions.getActivatedExtensions();
    for (const QString &id : ids)
        m_extensions.deactivateExtension(id);
}

QHash<QString, QString> App::startup(bool headless, const QStringList &disabled)
{
    // headless 时 ui 为空 -> 没有任何扩展创建 QWidget。
    return m_extensions.activateByEvent(QStringLiteral("onStartupFinished"), disabled, nullptr);
}

} // namespace nextpilot::plotscope::app
