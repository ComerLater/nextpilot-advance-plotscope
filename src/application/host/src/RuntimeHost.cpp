#include "RuntimeHost.h"
#include "IWorkspace.h"
#include "ErrorCodes.h"
#include "RegistryNode.h"
#include "RegistryTree.h"
#include "ShellContributions.h"
#include "StaticRegistryNodes.h"

#include <QSet>

namespace nextpilot::plotscope::host {
namespace err = nextpilot::plotscope::foundation::err;

namespace {
// A0 §八.2：AppContext 不再对外开 registry 访问器；装配层按 kind 从注册树定位。
inline nextpilot::plotscope::dataset::DatasetRegistry &datasetHub(nextpilot::plotscope::context::AppContext &c)
{
    return *static_cast<nextpilot::plotscope::dataset::DatasetRegistry *>(
        c.registryTree().find(nextpilot::plotscope::registry::RegistryKind::Dataset));
}
inline nextpilot::plotscope::dataset::SourceRegistry &sourceHub(nextpilot::plotscope::context::AppContext &c)
{
    return *static_cast<nextpilot::plotscope::dataset::SourceRegistry *>(
        c.registryTree().find(nextpilot::plotscope::registry::RegistryKind::Source));
}
} // namespace

RuntimeHost::RuntimeHost(nextpilot::plotscope::context::AppContext &appCtx, QObject *parent)
    : QObject(parent), m_appCtx(appCtx),
      // UI 插槽注册表构造即上树。
      m_uiSlotHub(appCtx.registryTree()), m_ingest(datasetHub(appCtx)),
      // data.load 走统一摄取管道（读字节 -> 扇出 -> 落库）：
      // 格式驱动只声明 parserId，解析实现由 SPI 解析器提供。
      m_dataLoadHandler(appCtx,
                        (*static_cast<nextpilot::plotscope::registry::ExtensionRegistry *>(
                            appCtx.registryTree().find(nextpilot::plotscope::registry::RegistryKind::Extension))),
                        datasetHub(appCtx), sourceHub(appCtx), &m_ingest, &appCtx.settings()),
      m_dataCommandsHandler(appCtx, *this, m_ingest),
      m_aiToolsHandler(appCtx, *this, [this]() { return m_handlers.keys(); })
{
    // 系统级 data.load 入口：不归属任何可卸载扩展，任何扩展装卸都不影响它。
    // 用一个保留 key 注册，避免与扩展 handler 的 "%id/%index" 冲突。
    m_handlers.insert(QStringLiteral("@system/data.load"), &m_dataLoadHandler);

    // data 派生能力（reload/export/recent/clearCache）与 load 同属系统级，
    // 归口一处——所有 data.* 意图都在宿主，扩展装卸不影响，也不建第二条路。
    m_handlers.insert(QStringLiteral("@system/data.commands"), &m_dataCommandsHandler);

    // AI/MCP 工具层：ai.* 系统级 handler，暴露工具目录与调用入口，供后续 MCP bridge 接。
    m_handlers.insert(QStringLiteral("@system/ai.tools"), &m_aiToolsHandler);

    // 壳级 Ribbon 页/组在**唯一一处**声明：ShellContributions（host 层）。
    // 业务页/组由各扩展 onAttach 自声明；区域是壳代码常量（Region 枚举），不进注册表。
    // headless 下这些槽也是纯数据，不创建 QWidget。
    declareShellContributions(m_uiSlotHub);

    // M2.8 遗留 4 收口：Parser / RecordingFormat / RecordingBackend 三张存量静态表
    // 全量上树（diag.registries 无盲区）。内置扩展节点并入 Extension 节点，不另立 kind。
    new ParserNode(m_appCtx.registryTree(), this);
    new RecordingFormatNode(m_appCtx.registryTree(), this);
    new RecordingBackendNode(m_appCtx.registryTree(), this);

    // M2.8 diag.registries：全系统注册树的全景 dump（每节点一项）。
    m_appCtx.interactions().registerViewAction(
        QStringLiteral("diag.registries"),
        [this](const QVariantMap &) -> nextpilot::plotscope::foundation::Result<QVariant> {
            QVariantList list;
            for (nextpilot::plotscope::registry::RegistryNode *node : m_appCtx.registryTree().nodes())
            {
                QVariantMap item;
                item.insert(QStringLiteral("kind"), nextpilot::plotscope::registry::kindName(node->kind()));
                item.insert(QStringLiteral("name"), node->name());
                item.insert(QStringLiteral("count"), node->count());
                item.insert(QStringLiteral("keys"), node->keysAsStrings());
                list.append(item);
            }
            return QVariant(list);
        });
}

RuntimeHost::~RuntimeHost() { deactivateAll(); }

void RuntimeHost::registerIntentHandler(nextpilot::plotscope::intent::IIntentHandler *handler)
{
    if (!handler || m_initializing.isEmpty())
        return;
    // 归属到当前正在初始化的扩展；一个 handler 可处理多个 op，
    // 执行时遍历所有 handler 询问 canHandle，无需按 op 建索引。
    const QString key = QStringLiteral("%1/%2").arg(m_initializing).arg(m_handlers.size());
    m_handlers.insert(key, handler);
}

void RuntimeHost::registerSystemHandler(const QString &key, nextpilot::plotscope::intent::IIntentHandler *handler)
{
    // 系统级：不经 m_initializing、不计入任何扩展的 handlers，扩展装卸不移除它。
    if (!handler || key.isEmpty())
        return;
    m_handlers.insert(key, handler);
}

void RuntimeHost::unregisterHandler(nextpilot::plotscope::intent::IIntentHandler *handler)
{
    for (auto it = m_handlers.begin(); it != m_handlers.end(); ++it)
        if (it.value() == handler)
        {
            m_handlers.erase(it);
            return;
        }
}

QList<nextpilot::plotscope::intent::IIntentHandler *> RuntimeHost::initExtensionHandlers() const
{
    QList<nextpilot::plotscope::intent::IIntentHandler *> out;
    if (m_initializing.isEmpty())
        return out;
    const QString prefix = m_initializing + QLatin1Char('/');
    for (auto it = m_handlers.constBegin(); it != m_handlers.constEnd(); ++it)
        if (it.key().startsWith(prefix))
            out << it.value();
    return out;
}

QHash<QString, QString> RuntimeHost::activate(const QStringList &disabledIds, IUiContext *ui)
{
    if (!m_extensionService)
        return {};
    return m_extensionService->activateByEvent(QStringLiteral("onStartupFinished"), disabledIds, ui);
}

bool RuntimeHost::deactivate(const QString &extensionId)
{
    if (!m_extensionService)
        return false;
    return m_extensionService->deactivateExtension(extensionId);
}

void RuntimeHost::deactivateAll()
{
    if (!m_extensionService)
        return;
    const QStringList ids = m_extensionService->getActivatedExtensions();
    for (const QString &id : ids)
        m_extensionService->deactivateExtension(id);
}

bool RuntimeHost::isActive(const QString &extensionId) const
{
    return m_extensionService && m_extensionService->isActivated(extensionId);
}

QStringList RuntimeHost::activeIds() const
{
    return m_extensionService ? m_extensionService->getActivatedExtensions() : QStringList{};
}

QList<nextpilot::plotscope::registry::ExtensionDescription> RuntimeHost::availableExtensions() const
{
    return m_extensionService ? m_extensionService->getExtensions()
                              : QList<nextpilot::plotscope::registry::ExtensionDescription>{};
}

nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult>
RuntimeHost::execute(const nextpilot::plotscope::intent::Intent &intent)
{
    for (auto it = m_handlers.constBegin(); it != m_handlers.constEnd(); ++it)
    {
        nextpilot::plotscope::intent::IIntentHandler *handler = it.value();
        if (handler && handler->canHandle(intent))
            return handler->execute(intent);
    }
    return nextpilot::plotscope::foundation::fail(err::Unsupported,
                                                  QStringLiteral("没有扩展能处理意图: %1").arg(intent.op));
}

const nextpilot::plotscope::registry::UiSlotHub &RuntimeHost::uiSlots()
{
    // 每次访问都从当前工具箱重新绑定，装卸扩展包后立即反映，无需手动同步。
    m_uiSlotHub.refreshFrom((*static_cast<nextpilot::plotscope::registry::ExtensionRegistry *>(
        m_appCtx.registryTree().find(nextpilot::plotscope::registry::RegistryKind::Extension))));
    return m_uiSlotHub;
}

nextpilot::plotscope::registry::UiSlotHub &RuntimeHost::uiSlotsMutable()
{
    m_uiSlotHub.refreshFrom((*static_cast<nextpilot::plotscope::registry::ExtensionRegistry *>(
        m_appCtx.registryTree().find(nextpilot::plotscope::registry::RegistryKind::Extension))));
    return m_uiSlotHub;
}

} // namespace nextpilot::plotscope::host
