#include "ExtensionService.h"
#include "IWorkbenchContribution.h"

#include "command/CommandsRegistry.h"
#include "command/CommandService.h"
#include "Intent.h"

#include <QPointer>

using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::command;
using namespace nextpilot::plotscope::registry;

namespace nextpilot::plotscope::assembly {
namespace wb = nextpilot::plotscope::workbench;
namespace registry = nextpilot::plotscope::registry;

// 单一执行 handler：认领所有当前仍挂载的贡献命令，记录触发次数并返回成功。
// 常驻注册一次即可；unmount 后命令从扩展表消失，canHandle 自然返回 false，无悬空。
class ExtensionService::ContributedCommandHandler final : public QObject, public IIntentHandler
{
public:
    explicit ContributedCommandHandler(const registry::ExtensionRegistry &extensions, QObject *parent = nullptr)
        : QObject(parent), m_extensions(extensions)
    {
    }

    bool canHandle(const Intent &intent) const override
    {
        if (intent.op.isEmpty())
            return false;
        // workbench.* 保留前缀归工作台内置 handler（内置命令
        // workbench.action.openSettings 等有自己的执行体），扩展 handler 不抢。
        if (intent.op.startsWith(QLatin1String("workbench.")))
            return false;
        return m_extensions.findCommand(intent.op) != nullptr;
    }

    Result<IntentResult> execute(const Intent &intent) override
    {
        m_triggered[intent.op] += 1;
        IntentResult out;
        out.payload = intent.op; // 回显命令 id，e2e 可断言
        return Result<IntentResult>(out);
    }

    int triggeredCount(const QString &id) const { return m_triggered.value(id, 0); }

private:
    const registry::ExtensionRegistry &m_extensions;
    QHash<QString, int> m_triggered;
};

ExtensionService::ExtensionService(ctx::AppContext &appCtx, command::CommandService &commands,
                                   nextpilot::plotscope::instantiation::InstantiationService &services,
                                   wb::IContributionsInstaller *installer)
    : m_appCtx(appCtx), m_commands(commands), m_services(services), m_installer(installer),
      m_registry(
          *static_cast<registry::ExtensionRegistry *>(appCtx.registryTree().find(registry::RegistryKind::Extension)))
{
}

void ExtensionService::attachViewRegistry()
{
    // App 构造先注册 views.registry 再 bind（时序由组合根保证）；取不到保持 nullptr。
    m_views = m_services.getService<registry::ViewRegistry>(QStringLiteral("views.registry"));
}

void ExtensionService::bindCommands(CommandsRegistry *commands)
{
    if (!commands)
        return;

    attachViewRegistry();
    // 扩展包的 viewsContainers/views：mount 时登记在唯一扩展注册表，
    // 这里按 owner 灌进 ViewRegistry——扩展 contributes 与扩展包 manifest 汇同一张表。
    if (m_views)
    {
        for (const auto &owned : m_registry.contributedViewContainers())
        {
            registry::ContributesDoc doc;
            doc.owner = owned.owner;
            doc.viewContainers.append(owned.item);
            m_views->declare(doc);
        }
        for (const auto &owned : m_registry.contributedViews())
        {
            registry::ContributesDoc doc;
            doc.owner = owned.owner;
            doc.views.append(owned.item);
            m_views->declare(doc);
        }
        for (const auto &owned : m_registry.contributedCustomEditors())
        {
            registry::ContributesDoc doc;
            doc.owner = owned.owner;
            doc.customEditors.append(owned.item);
            m_views->declare(doc);
        }
    }

    if (!m_handler)
    {
        // QObject 父给本服务；handler 只借用扩展表引用，两者都在 App 生命周期内存活。
        m_handler = new ContributedCommandHandler(m_registry, this);
        m_commands.registerSystemHandler(QStringLiteral("workbench.contributions"), m_handler,
                                         {QStringLiteral("contributes.commands")});
    }

    for (const auto &c : m_registry.contributedCommands())
    {
        if (m_bound.contains(c.command))
            continue; // 幂等
        CommandDescriptor d;
        d.id = c.command;
        d.title = c.title.isEmpty() ? c.command : c.title;
        d.category = c.category;
        d.icon = c.icon;
        d.provider = m_registry.ownerOfCommand(c.command);
        d.intentOp = c.command; // 执行走 CommandService → 本服务 handler
        if (commands->add(d))
            m_bound.insert(c.command, d.provider);
    }
}

void ExtensionService::unbind(const QString &owner, CommandsRegistry *commands)
{
    if (!commands)
        return;
    // 用 bind 时记录的 owner 归属撤，不依赖扩展表当前状态（可能已 unmount）。
    if (m_views)
        m_views->undeclare(owner);
    for (auto it = m_bound.begin(); it != m_bound.end();)
    {
        if (it.value() == owner)
        {
            commands->remove(it.key());
            it = m_bound.erase(it);
        }
        else
            ++it;
    }
}

int ExtensionService::triggeredCount(const QString &commandId) const
{
    return m_handler ? m_handler->triggeredCount(commandId) : 0;
}

// ---- ContributionsInstaller：灌回唯一实现。
// 声明式灌回：与原 IUiContext::installContributions 落点完全一致，只是写入方
// 从壳换成装配层（写 ViewRegistry/MenuRegistry/StatusHub，壳只渲染）。owner 用扩展 identifier。
void ContributionsInstaller::install(const registry::ContributesDoc &c)
{
    // contributes.viewsContainers / views / customEditors 与 activity 入口：声明灌 ViewRegistry，
    // 壳经 IViewsService 读回。F2：Activity Bar 入口按 viewsContainers[area=activitybar] 承接，
    // 专属字段（panelTypes/centralEditor/centralRef/ribbonPage）沿用 ActivitySpec 原名。
    registry::ContributesDoc doc;
    doc.owner = c.owner.isEmpty() ? QStringLiteral("extension") : c.owner;
    doc.viewContainers = c.viewContainers;
    doc.views = c.views;
    doc.customEditors = c.customEditors;
    for (const ActivitySpec &a : c.activities)
    {
        registry::ContributedViewContainer container;
        container.id = a.id;
        container.title = a.label;
        container.area = QStringLiteral("activitybar");
        container.icon = a.icon;
        container.order = a.order;
        container.panelTypes = a.panelTypes;
        container.centralEditor = a.centralEditor;
        container.centralRef = a.centralRef;
        container.ribbonPage = a.ribbonPage;
        doc.viewContainers.append(container);
    }
    if (!doc.viewContainers.isEmpty() || !doc.views.isEmpty() || !doc.customEditors.isEmpty())
        m_views.declare(doc);

    const QString owner = c.owner.isEmpty() ? QStringLiteral("extension") : c.owner;

    // VSCode 形态：菜单项登记进 MenuRegistry（MenuId = menubar/<pageId>，IMenuItem.group = 组 id）。
    QHash<QString, QString> groupPage; // group id -> page id
    for (const RibbonGroupDecl &g : c.ribbonGroups)

        groupPage.insert(g.id, g.pageId); // 组可能由别的 owner 声明（如壳骨架的 home/file/view 页组、模块页组），
    // 动作归属按注册表全量解析，不只认本 doc。
    auto &extRegistry =
        *static_cast<registry::ExtensionRegistry *>(m_appCtx.registryTree().find(registry::RegistryKind::Extension));
    for (const RibbonPageDecl &p : extRegistry.ribbonPages())
        for (const RibbonGroupDecl &g : extRegistry.ribbonGroupsOf(p.id))
            groupPage.insert(g.id, g.pageId);
    if (!groupPage.isEmpty())
    {
        for (const UiAction &action : c.actions)
        {
            const QString pageId = groupPage.value(action.slotId);
            if (pageId.isEmpty())
                continue;
            m_menus.appendMenuItem(registry::MenuId::menubar(pageId),
                                   registry::IMenuItem{action, action.slotId, owner});
        }
    }
    // 视图标题栏动作（VSCode view/title）：写既有 MenuRegistry 菜单面，group 保持 VSCode
    // 分组语义（不当 viewId 用）；viewId 过滤由壳求值 when 作用域（ctx 注入 view=当前 viewId）。
    for (const ContributedMenuItem &mi : c.menuItems)
    {
        if (mi.place != MenuId::viewTitle())
            continue;
        UiAction action;
        action.id = mi.command;
        action.label = mi.label;
        action.icon = mi.icon;
        action.when = mi.when;
        action.order = mi.order;
        m_menus.appendMenuItem(MenuId::viewTitle(), IMenuItem{action, mi.group, owner});
    }
    for (const StatusSpec &s : c.statuses)
        m_appCtx.status().registerSpec(s);

    // 动态动作提供器：provider 对象注册，运行时条目懒求值，
    // 变化由 provider 的订阅通知外壳只重建该 slot（不经壳写壳读的隐藏通道）。
    for (const ActionProviderDecl &p : c.actionProviders)
    {
        if (!p.provider)
            continue;
        // 登记进 ViewRegistry（声明面）：IRibbonService 的 dynamicActionsIn 从这里读回。
        m_views.registerActionProvider(p.owner.isEmpty() ? owner : p.owner, p.slotId, p.provider);
    }
}

// ---- ExtensionService 激活/反激活链路 ----

QHash<QString, QString> ExtensionService::activateByEvent(const QString &activationEvent,
                                                          const QStringList &disabledIds,
                                                          nextpilot::plotscope::host::IUiContext *ui)
{
    Q_UNUSED(activationEvent);
    QHash<QString, QString> errors;

    const auto entries = registry::ExtensionRegistry::builtinExtensions();
    QHash<QString, QPair<registry::ExtensionDescription, registry::ExtensionRegistry::ExtensionFactory>> byId;
    for (const auto &entry : entries)
        if (!disabledIds.contains(entry.first.identifier))
            byId.insert(entry.first.identifier, entry);

    // Kahn 拓扑排序：节点是扩展 identifier，边由 dependencies 产生。
    QHash<QString, int> indeg;
    QHash<QString, QStringList> consumers; // provider -> consumers
    for (auto it = byId.constBegin(); it != byId.constEnd(); ++it)
    {
        indeg[it.key()] = 0;
        for (const QString &dep : it.value().first.dependencies)
        {
            if (!byId.contains(dep))
            {
                errors.insert(it.key(), QStringLiteral("缺少依赖: %1").arg(dep));
                continue;
            }
            consumers[dep].append(it.key());
            indeg[it.key()] = indeg.value(it.key()) + 1;
        }
    }

    QList<QString> queue;
    for (auto it = indeg.constBegin(); it != indeg.constEnd(); ++it)
        if (it.value() == 0)
            queue.append(it.key());

    QList<QString> order;
    while (!queue.isEmpty())
    {
        const QString cur = queue.takeFirst();
        order.append(cur);
        for (const QString &next : consumers.value(cur))
        {
            indeg[next] = indeg.value(next) - 1;
            if (indeg[next] == 0)
                queue.append(next);
        }
    }

    // 剩余的即有环或依赖被错误扩展阻塞。
    for (auto it = indeg.constBegin(); it != indeg.constEnd(); ++it)
        if (it.value() > 0 && !errors.contains(it.key()))
            errors.insert(it.key(), QStringLiteral("依赖成环或依赖缺失"));

    for (const QString &id : order)
    {
        // 幂等：已激活扩展不重建。否则 deactivate 单个后再 activate 会把其余扩展
        // 全部实例化一遍，重复注册动作/贡献（IKYDH2 生命周期对称性测试暴露）。
        if (m_active.contains(id))
            continue;

        const auto entry = byId.value(id);
        nextpilot::plotscope::host::IWorkbenchContribution *instance = entry.second();
        if (!instance)
        {
            errors.insert(id, QStringLiteral("工厂返回空"));
            continue;
        }
        // 所有权归本服务：deactivate 时 delete 整棵子树回收。
        instance->setParent(this);
        // 扩展只收服务容器，命令注册走 ICommands（commandId 即命令身份，无归属洗牌）。
        instance->initialize(m_services);

        ActiveExtension ax;
        ax.description = entry.first;
        ax.instance = instance;
        m_active.insert(id, std::move(ax));

        // 收集声明式 contributes（initialize 后数据已就绪）。owner 用扩展 identifier。
        registry::ContributesDoc doc = instance->contributes();
        doc.owner = id;
        m_registry.mountContribution(doc);
        // 装配层 installer 灌回（slots/status/views/menus）；headless（ui 空）不建 UI。
        if (m_installer)
            m_installer->install(doc);
        if (ui)
            instance->onAttach(*ui);
    }

    return errors;
}

bool ExtensionService::deactivateExtension(const QString &extensionId)
{
    auto it = m_active.find(extensionId);
    if (it == m_active.end() || !it->instance)
        return false;

    it->instance->onDetach();
    delete it->instance; // QObject 子树自动销毁其创建的 UI 与信号连接
    m_registry.unmountContribution(extensionId);
    m_active.erase(it);
    return true;
}

bool ExtensionService::isActivated(const QString &extensionId) const { return m_active.contains(extensionId); }

QStringList ExtensionService::getActivatedExtensions() const { return m_active.keys(); }

QList<registry::ExtensionDescription> ExtensionService::getExtensions() const
{
    QList<registry::ExtensionDescription> out;
    for (const auto &entry : registry::ExtensionRegistry::builtinExtensions())
        out.append(entry.first);
    return out;
}

} // namespace nextpilot::plotscope::assembly
