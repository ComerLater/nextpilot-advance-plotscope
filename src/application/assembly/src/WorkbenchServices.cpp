#include "WorkbenchServices.h"
#include "IWorkspace.h"
#include "SettingsEditorWidget.h"
#include "ErrorCodes.h"
#include "ShellContributions.h"
#include "command/CommandService.h"
#include "UiSlotHub.h"

#include <QDebug>

namespace nextpilot::plotscope::assembly {
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

// ---- WorkbenchCommandHandler ----
WorkbenchCommandHandler::WorkbenchCommandHandler(wb::IEditorService &editors,
                                                 nextpilot::plotscope::context::AppContext &appCtx,
                                                 intent::IIntentExecutor &executor, QObject *parent)
    : QObject(parent), m_editors(editors), m_appCtx(appCtx), m_executor(executor)
{
}

bool WorkbenchCommandHandler::canHandle(const intent::Intent &intent) const
{
    // §3.0.9 内置命令最小集：本笔只交付 openSettings；其余 workbench.* 待后续笔逐个接入。
    return intent.op == QLatin1String("workbench.action.openSettings");
}

foundation::Result<intent::IntentResult> WorkbenchCommandHandler::execute(const intent::Intent &intent)
{
    // 执行体 = 打开内置 settings 编辑器（普通 editor 页签，无模态对话框）。
    registry::ResourceRef ref{QStringLiteral("settings:"), QStringLiteral("application/json"), QStringLiteral("设置")};
    QWidget *page = m_editors.openEditor(BuiltinSettingsEditorProvider::viewType(), ref, m_appCtx, m_executor, nullptr);

    // 编辑器不可用 → 返回错误码，不静默 success（Result 错误传播检测）：
    // 打不开时调用方必须能经 Result.ok() 感知失败，而不是拿到成功结果却无页签。
    if (!page)
        return foundation::fail(
            foundation::err::NoView,
            QStringLiteral("设置编辑器未能打开（viewType=%1）").arg(BuiltinSettingsEditorProvider::viewType()));

    m_lastOpened = page; // 页签能回答自己的 ResourceRef（e2e 断言）

    // section = ConfigurationSpec.id（既有入口契约，如 DataManagerPanel 的 "core.data"）：
    // 打开页签后按 id 精确聚焦到对应分组。空值 = 首屏。
    // 无命中时由 Settings editor 退回全屏并出诊断——不静默吞参数。
    m_sectionFocusDiagnostic.clear();
    const QString section = intent.params.value(QStringLiteral("section")).toString();
    if (!section.isEmpty())
    {
        if (auto *settings = qobject_cast<SettingsEditorWidget *>(page))
        {
            settings->focusSection(section);
            m_sectionFocusDiagnostic = settings->focusDiagnostic();
        }
        else
        {
            m_sectionFocusDiagnostic = QStringLiteral("未找到设置分组「%1」，已显示全部").arg(section);
        }
    }

    intent::IntentResult out;
    out.payload = intent.op;
    return foundation::Result<intent::IntentResult>(out);
}

WorkbenchServices::WorkbenchServices(nextpilot::plotscope::context::AppContext &appCtx,
                                     nextpilot::plotscope::intent::JobRecorder &recorder,
                                     command::CommandService &commands, registry::UiSlotHub &slotHub,
                                     nextpilot::plotscope::context::IngestPipeline &ingest, QObject *parent)
    : QObject(parent), m_datasetAccess(datasetHub(appCtx), this), m_sourceAccess(sourceHub(appCtx), this),
      m_ingestAccess(appCtx, ingest, this), m_recorderAccess(recorder), m_playbackAccess(),
      m_workspace(
          m_datasetAccess, m_sourceAccess, m_ingestAccess, m_recorderAccess, m_playbackAccess, appCtx.settings(),
          appCtx.derivedHub(),
          [&appCtx]() -> QVector<QPair<QString, QString>> {
              QVector<QPair<QString, QString>> out;
              for (auto *d : (*static_cast<nextpilot::plotscope::registry::ExtensionRegistry *>(
                                  appCtx.registryTree().find(nextpilot::plotscope::registry::RegistryKind::Extension)))
                                 .drivers())
                  if (d)
                      out.append({d->id(), d->displayName()});
              return out;
          },
          [&appCtx]() {
              return (*static_cast<nextpilot::plotscope::registry::ExtensionRegistry *>(
                          appCtx.registryTree().find(nextpilot::plotscope::registry::RegistryKind::Extension)))
                  .drivers();
          },
          [&appCtx]() {
              return (*static_cast<nextpilot::plotscope::registry::ExtensionRegistry *>(
                          appCtx.registryTree().find(nextpilot::plotscope::registry::RegistryKind::Extension)))
                  .parsers();
          },
          [&appCtx]() {
              return (*static_cast<nextpilot::plotscope::registry::ExtensionRegistry *>(
                          appCtx.registryTree().find(nextpilot::plotscope::registry::RegistryKind::Extension)))
                  .ids();
          },
          [&appCtx](const QString &viewId) {
              return (*static_cast<nextpilot::plotscope::registry::ExtensionRegistry *>(
                          appCtx.registryTree().find(nextpilot::plotscope::registry::RegistryKind::Extension)))
                  .itemContextsFor(viewId);
          },
          [&appCtx](const QString &id) -> const registry::ReportTemplate * {
              return (*static_cast<nextpilot::plotscope::registry::ExtensionRegistry *>(
                          appCtx.registryTree().find(nextpilot::plotscope::registry::RegistryKind::Extension)))
                  .reports.find(id);
          }),
      m_commands(commands),
      m_scriptCaps(appCtx.interactions(), appCtx, commands, &recorder, [&commands]() { return commands.commandIds(); }),
      m_panelsService(appCtx), m_formatHints(appCtx), m_themeService(appCtx, slotHub), m_slotsService(slotHub),
      m_ribbonService(appCtx, m_slotsService, m_viewRegistry), m_viewsService(m_viewRegistry),
      m_editorsService(m_viewRegistry),
      m_configurationService(appCtx.settings(),
                             (*static_cast<nextpilot::plotscope::registry::ExtensionRegistry *>(
                                 appCtx.registryTree().find(nextpilot::plotscope::registry::RegistryKind::Extension))),
                             {},
                             [&commands](const QString &key, const QJsonValue &value) {
                                 // 主题项：设置页写 theme（落盘 app/theme）后，转发 theme.set 让 ThemeManager 经
                                 // 既有 themeChanged 路径换肤；ThemeManager 仍读完整键 app/theme，不新增写入通道。
                                 if (key != QLatin1String("theme") || !value.isString())
                                     return;
                                 intent::Intent t;
                                 t.op = QStringLiteral("theme.set");
                                 t.params[QStringLiteral("theme")] = value.toString();
                                 const auto themeResult = commands.execute(t);
                                 if (!themeResult.ok())
                                     qWarning() << QStringLiteral("ValueChangedHook: theme.set 失败: %1 %2")
                                                       .arg(themeResult.error().code, themeResult.error().message);
                             }),
      m_settingsEditor(m_configurationService),
      m_contribInstaller(appCtx, m_slotsService, m_viewRegistry, m_menuRegistry)
{
    // vscode.workspace 注入：模块经 ctx.workspace() 读贡献查询面，不接触扩展注册表。
    appCtx.setWorkspace(&m_workspace);
    // 壳内置 text 最小编辑器：装配层声明 viewType="text"
    // 并注册 provider——零扩展也能打开/编辑/保存。owner="core.text" 是装配层自数据，
    // 不是"壳按 owner 分支"（壳不见 registry，也不解释业务 id）。
    {
        registry::ContributesDoc coreDoc;
        coreDoc.owner = QStringLiteral("core.text");
        registry::ContributedCustomEditor text;
        text.viewType = QStringLiteral("text");
        text.displayName = QStringLiteral("纯文本");
        text.priority = -1; // 扩展编辑器命中时优先于内置
        registry::ContributedEditorSelector sel;
        sel.mime = QStringLiteral("text/plain");
        text.selector.append(sel);
        coreDoc.customEditors.append(text);
        m_viewRegistry.declare(coreDoc);
        m_textEditorDisposable =
            m_viewRegistry.registerEditorProvider(QStringLiteral("text"), &m_textEditor, QStringLiteral("core.text"));
    }
    // 壳内置 Settings editor：owner=core.settings 经 registerEditorProvider 的
    // registerEditorProvider 注册（无第二注册面、壳不按 id 特判）。viewType 核过
    // VSCode 源码：SettingsEditor2.ID = 'workbench.editor.settings2'（
    // 'workbench.editor.settings' 字符串在源码中不存在，实际 ID 带 "2"）。
    // core.settings 自带「通用」配置组进 configurations()（零 owner 也能开设置，§5）。
    {
        registry::ContributesDoc settingsDoc;
        settingsDoc.owner = QStringLiteral("core.settings");
        registry::ContributedCustomEditor settingsDecl;
        settingsDecl.viewType = BuiltinSettingsEditorProvider::viewType();
        settingsDecl.displayName = QStringLiteral("设置");
        settingsDecl.priority = -10; // 扩展编辑器命中时优先于内置
        registry::ContributedEditorSelector jsonSel;
        jsonSel.mime = QStringLiteral("application/json");
        settingsDecl.selector.append(jsonSel);
        settingsDoc.customEditors.append(settingsDecl);
        m_viewRegistry.declare(settingsDoc);
        m_settingsEditorDisposable = m_viewRegistry.registerEditorProvider(
            BuiltinSettingsEditorProvider::viewType(), &m_settingsEditor, QStringLiteral("core.settings"));
    }
    // core.settings 的命令与「通用」配置走唯一贡献表：命令面板可见、
    // --contributions 可列（判据 grep）；配置声明进 configurations()（§5 零 owner 可开）。
    {
        registry::ContributesDoc coreCfg;
        coreCfg.owner = QStringLiteral("core.settings");
        registry::ContributedCommand openSettings;
        openSettings.command = QStringLiteral("workbench.action.openSettings");
        openSettings.title = QStringLiteral("打开设置");
        openSettings.category = QStringLiteral("首选项");
        openSettings.order = 0;
        coreCfg.commands.append(openSettings);
        coreCfg.configurations = coreBuiltinConfigurations();
        (*static_cast<nextpilot::plotscope::registry::ExtensionRegistry *>(
             appCtx.registryTree().find(nextpilot::plotscope::registry::RegistryKind::Extension)))
            .mountContribution(coreCfg);
        // 执行体：工作台内置命令 handler（workbench.* 保留前缀独占，扩展 handler 让出）。
        // 同时注册成服务（workbench.commands）：e2e 可断言"命令执行后页签能回答 ResourceRef"。
        m_workbenchHandler = new WorkbenchCommandHandler(m_editorsService, appCtx, commands, this);
        commands.registerSystemHandler(QStringLiteral("workbench.actions"), m_workbenchHandler,
                                       {QStringLiteral("workbench.action.openSettings")});
        m_services.registerSingleton(QStringLiteral("workbench.commands"), m_workbenchHandler,
                                     QStringLiteral("assembly"), 1,
                                     nextpilot::plotscope::instantiation::ServiceTypeTag<
                                         nextpilot::plotscope::assembly::WorkbenchCommandHandler>::value());
    }
    // §1.4.2 + §1.2.1：注册 4 条服务。owner=assembly，version=1。
    m_services.registerSingleton(
        QStringLiteral("vscode.workspace"), &m_workspace, QStringLiteral("assembly"), 1,
        nextpilot::plotscope::instantiation::ServiceTypeTag<nextpilot::plotscope::workbench::IWorkspace>::value());
    m_services.registerSingleton(
        QStringLiteral("vscode.window"), &m_window, QStringLiteral("assembly"), 1,
        nextpilot::plotscope::instantiation::ServiceTypeTag<nextpilot::plotscope::workbench::IWindow>::value());
    // CommandService 同时实现 ICommands 与 IIntentExecutor（两个基类）。registerSingleton 存
    // void*、getService<T> 用 static_cast<T*>(void*) 取回，不做基类偏移调整——因此这里必须
    // 存【接口子对象地址】，而不是派生对象起始地址。
    auto *commandsIface = static_cast<nextpilot::plotscope::workbench::ICommands *>(&m_commands);
    m_services.registerSingleton(
        QStringLiteral("vscode.commands"), commandsIface, QStringLiteral("assembly"), 1,
        nextpilot::plotscope::instantiation::ServiceTypeTag<nextpilot::plotscope::workbench::ICommands>::value());
    m_services.registerSingleton(QStringLiteral("script.capabilities"), &m_scriptCaps, QStringLiteral("assembly"), 1,
                                 nextpilot::plotscope::instantiation::ServiceTypeTag<
                                     nextpilot::plotscope::uischema::IScriptCapabilities>::value());
    // 壳读回三服务（裁定 52441633 §三）：MainWindow::build 用同 id + 同 typeTag 取回。
    m_services.registerSingleton(
        QStringLiteral("panels.readback"), &m_panelsService, QStringLiteral("assembly"), 1,
        nextpilot::plotscope::instantiation::ServiceTypeTag<nextpilot::plotscope::workbench::IPanelsService>::value());
    m_services.registerSingleton(
        QStringLiteral("format.hints"), &m_formatHints, QStringLiteral("assembly"), 1,
        nextpilot::plotscope::instantiation::ServiceTypeTag<nextpilot::plotscope::workbench::IFormatHints>::value());
    m_services.registerSingleton(
        QStringLiteral("theme.readback"), &m_themeService, QStringLiteral("assembly"), 1,
        nextpilot::plotscope::instantiation::ServiceTypeTag<nextpilot::plotscope::workbench::IThemeService>::value());
    // 槽位表服务（裁定 52442006 Step 2）：壳读写唯一通道。
    m_services.registerSingleton(
        QStringLiteral("slots.binding"), &m_slotsService, QStringLiteral("assembly"), 1,
        nextpilot::plotscope::instantiation::ServiceTypeTag<nextpilot::plotscope::workbench::ISlotsService>::value());
    // Ribbon 页/组贡献读回（contributes.ribbonPages/ribbonGroups）：壳经 IRibbonService 只读。
    m_services.registerSingleton(
        QStringLiteral("ribbon.readback"), &m_ribbonService, QStringLiteral("assembly"), 1,
        nextpilot::plotscope::instantiation::ServiceTypeTag<nextpilot::plotscope::workbench::IRibbonService>::value());
    // 视图注册表（Step 5）：写面给装配层内部（ExtensionService 经
    // getService<ViewRegistry> 灌数），读/打开面给壳（IViewsService）。
    m_services.registerSingleton(
        QStringLiteral("views.registry"), &m_viewRegistry, QStringLiteral("assembly"), 1,
        nextpilot::plotscope::instantiation::ServiceTypeTag<nextpilot::plotscope::registry::ViewRegistry>::value());
    m_services.registerSingleton(
        QStringLiteral("views.readback"), &m_viewsService, QStringLiteral("assembly"), 1,
        nextpilot::plotscope::instantiation::ServiceTypeTag<nextpilot::plotscope::workbench::IViewsService>::value());
    // 编辑器读回：写面复用 views.registry（ViewRegistry），读/打开面给壳。
    m_services.registerSingleton(
        QStringLiteral("editor.readback"), &m_editorsService, QStringLiteral("assembly"), 1,
        nextpilot::plotscope::instantiation::ServiceTypeTag<nextpilot::plotscope::workbench::IEditorService>::value());
    // 配置合并/复位/诊断：声明面读 ExtensionRegistry，持久化只落 SettingsStore。
    m_services.registerSingleton(QStringLiteral("configuration.readback"), &m_configurationService,
                                 QStringLiteral("assembly"), 1,
                                 nextpilot::plotscope::instantiation::ServiceTypeTag<
                                     nextpilot::plotscope::workbench::IConfigurationService>::value());
    // 壳级标准动作（骨架 Ribbon 入口）落 MenuRegistry；声明清单归 host::ShellContributions。
    // MenuRegistry 归本容器持有，壳经 menuRegistry() 读回。
    nextpilot::plotscope::host::declareShellActions(m_menuRegistry);

    // 壳骨架的页/组与动作同源：走 contributes 数据面（owner=app.skeleton），
    // 不再独占插槽通道；RibbonPart 读 IRibbonService 即见 9 页。
    if (auto *ext = static_cast<nextpilot::plotscope::registry::ExtensionRegistry *>(
            appCtx.registryTree().find(nextpilot::plotscope::registry::RegistryKind::Extension)))
    {
        auto shellDoc = nextpilot::plotscope::host::shellContributes();
        ext->mountContribution(shellDoc);
        // 视图容器（activitybar 入口）同批落 ViewRegistry：扩展服务若在本构造内
        // 已先建（激活早于本处），其灌数那趟看不到本 doc，故这里补灌一次。
        m_viewRegistry.declare(shellDoc);
    }
}

} // namespace nextpilot::plotscope::assembly
