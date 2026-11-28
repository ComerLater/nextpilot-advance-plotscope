# 扩展注册与贡献机制（模块 = 内置扩展）

用途：新人入口。讲清仓内扩展（含原“功能模块”）怎么被描述、登记、激活，contributes 各项灌到哪张数据面表，以及诊断口径。设计取舍的完整论证见 `src/ARCHITECTURE.md`，命名对位见 [VSCode 命名对位表](vscode-alignment.md)。

口径：本文按 M4 Stage 1 目标模型写（模块统一为内置扩展，单一注册表、单一激活/贡献链路）。代码落地期间，凡“待落地后复核”的断言集中列在 [§8](#8-待代码落地后复核的断言)，不要据此反推未落地的接口签名。

## 1. 一句话结论

- 仓内 8 个功能模块（Analysis / Data / Plot / Protocol / Report / Script / Settings / Workspace）统一建模为**内置扩展**（builtin extension）：一份 `ExtensionDescription` 声明 + 一个工厂，登记进**唯一**的 `ExtensionRegistry`，由装配层 `ExtensionService` 按 `activationEvents` 与 `dependencies` 激活，contributes 一次性灌进各数据面注册表。
- 旧的“模块表 + provides/requires 能力拓扑 + `ModuleContributions` 装配”链路整体退场，替换关系见 [§7](#7-被替换掉的旧概念)。
- 壳只消费 `MenuRegistry` / `ViewRegistry` / `StatusHub` / theme 这类 VSCode 语义数据面，不持有业务模块表。
- 本文的“扩展”同时覆盖三类形态：内置功能模块（本仓 8 个）、内置工具箱 Pack（`toolbox/<域>/`）、外部 manifest 插件；三者走同一张注册表，差异只在发现路径与生命周期。

## 2. 对位 VSCode 什么

| 本仓                                    | VSCode 对位                                                                                         | 出处（VSCode 仓 `src/` 下）                                                  | 状态        |
| --------------------------------------- | --------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------- | ----------- |
| `ExtensionDescription`                  | `IExtensionDescription`（identifier / name / version / isBuiltin / activationEvents / contributes） | `vs/platform/extensions/common/extensions.ts`                                | ✅          |
| `ExtensionRegistry`（登记/查询面）      | 内置扩展扫描登记面（VSCode 无同名单例类）                                                           | `vs/workbench/services/extensions/common/builtinExtensionsScannerService.ts` | ✅          |
| `ExtensionService`（激活）              | `ExtensionService` / `AbstractExtensionService.activateExtension`                                   | `vs/workbench/services/extensions/common/extensionService.ts`                | ✅          |
| contributes `commands` / `menus`        | 菜单/命令扩展点                                                                                     | `vs/workbench/api/common/menusExtensionPoint.ts`                             | ✅          |
| `MenuId` / `MenuRegistry`               | 同名                                                                                                | `vs/platform/actions/common/actions.ts`                                      | ✅          |
| contributes `views` / `viewsContainers` | 视图扩展点                                                                                          | `vs/workbench/api/browser/viewsExtensionPoint.ts`                            | ✅          |
| contributes 清单解析                    | `IExtensionPointManager`（扩展点注册表一侧）                                                        | `vs/platform/extensions/common/extensionsRegistry.ts`                        | ⚠️ 路径待核 |

## 3. ExtensionDescription（扩展静态描述）

`ExtensionDescription` 是一个扩展的唯一声明，对位 VSCode `IExtensionDescription`。它只承载**静态数据**，不含行为、不含工厂指针。

| 字段               | 含义                                                         | 对位 / 备注                                         |
| ------------------ | ------------------------------------------------------------ | --------------------------------------------------- |
| `identifier`       | 唯一主键（扩展 id，如 `plot` / `data`）                      | `IExtensionDescription.identifier`                  |
| `name`             | 显示名                                                       | `IExtensionDescription.displayName`                 |
| `version`          | 版本字符串                                                   | `IExtensionDescription.version`                     |
| `isBuiltin`        | 是否内置扩展；仓内 8 个功能模块与内置域包均为 `true`         | 对位 `isBuiltin`                                    |
| `dependencies`     | 依赖的 `identifier` 列表                                     | 取代旧 `provides/requires` 能力名；激活拓扑按这里排 |
| `activationEvents` | 激活条件：`"*"` / `"onStartupFinished"` / `"onCommand:<id>"` | 对位 activation events；本期内置扩展先取 `"*"`      |
| `contributes`      | 唯一一份 UI 声明（`ContributesDoc`），owner 即 `identifier`  | 对位 package.json 的 `contributes`                  |

分工边界：

- 描述里**不塞**工厂/激活入口。注册 API 是 `ExtensionRegistry::registerBuiltinExtension(const ExtensionDescription &, ExtensionFactory)`；查询 API 为 `builtinExtensions()` / `builtinExtensionIds()`，另有 `resetBuiltinExtensions()` 供测试清空。
- 注册宏 `PS_REGISTER_EXTENSION(Cls)`：`Cls` 提供 `static ExtensionDescription description();` 与 `static IWorkbenchContribution *create();`。宏只是编译期登记手段，不引入新概念。
- 代码落点：`src/libs/registry/include/ExtensionDescription.h`（已落地，字段与本节一致；遗留复核项见 §8）。

## 4. 单一注册表 ExtensionRegistry

`ExtensionRegistry` 是仓内**唯一**的扩展注册表。两条发现路径汇入同一张表，不设第二张宿主表。

### 4.1 两条发现路径

| 路径               | 登记手段                                                         | 挂载方式                                               | 代码落点                                        |
| ------------------ | ---------------------------------------------------------------- | ------------------------------------------------------ | ----------------------------------------------- |
| 内置扩展           | `PS_REGISTER_EXTENSION` 静态自注册工厂                           | 启动期由 `ExtensionService` 登记 + 激活                | `ExtensionRegistry.h` / `ExtensionRegistry.cpp` |
| 工具箱 Pack        | `PS_REGISTER_PACK` 静态自注册工厂                                | `mount(Pack)` 一次挂入，`unmount(packId)` 按包原子撤销 | `src/libs/registry/include/ExtensionRegistry.h` |
| 外部 manifest 插件 | 扫描 `<root>/<id>/manifest.json`，只登记 contributes，不执行代码 | `mountContribution(doc)` 入表                          | `src/libs/registry/include/ExtensionScanner.h`  |

### 4.2 两个数据面（并列、数据独立）

- **Pack 数据面**：`uiActions` / `scripts` / `dataAssets` / `docs` / `examples` / `reports` / `viewTitleActions` / `itemContexts` / `themeTokens` 等 `Registry<T>` 表，条目是纯数据 struct。挂载单位是 `Pack`（一个纯数据清单 + 可选解析器）。
- **contributes 文档面**：`mountContribution(const ContributesDoc &)` 把一份 owner 级声明挂进来；`unmountContribution(owner)` 整批撤。contributes 只带声明数据，**不创建 Pack**，避免双份来源。

### 4.3 机制复用与查询

- 静态自注册复用既有 `StaticHub`（`src/libs/registry/include/StaticAutoRegistry.h`），不新造第二张静态表。
- 命令查询面：`findCommand(commandId)` / `ownerOfCommand(commandId)` / `contributedCommands()` / `paletteEntries()` / `commandIdsOf(owner)`。
- 声明查询面：`contributedViewContainers()` / `contributedViews()` / `contributedMenuItems()` / `contributedColors()` / `contributedStatuses()` / `contributedCustomEditors()` / `contributedConfigurations()`；菜单项按 place 取：`menuItemsIn(place)`。
- 唯一性校验：扩展 `identifier` 全局唯一；命令 id / view id / viewType / 设置属性点号键跨 owner 唯一。冲突先到者为准 + 诊断，非法条目校验通过前不占 id。
- 诊断收集：`contributionDiagnostics()` 按 `owner` / `keyPath` 记录解析与挂载问题，供诊断面板与单测断言读回。
- 代码落点：`src/libs/registry/include/ExtensionRegistry.h`、`src/libs/registry/src/ExtensionRegistry.cpp`。

## 5. ExtensionService 激活

激活服务位于**装配层**（`src/application/assembly/`）。壳目录不放任何“装配贡献”的类，否则“壳零业务”边界含混。

- 生命周期抽象（已落地）：`src/libs/workbench/include/IExtensionLifecycle.h`，接口方法 `activateAll(disabledIds, ui)` / `deactivate(extensionId)` / `deactivateAll()` / `isActive` / `activeIds` / `descriptions()`；装配层实现本接口，`RuntimeHost` 只持抽象指针转发，不反向依赖装配层。
- 现状旧服务：`src/application/assembly/include/ExtensionActivationService.h`、`src/application/assembly/src/ExtensionActivationService.cpp`（把贡献命令灌进命令表 + 挂 ViewRegistry）。
- 目标装配类 `assembly::ExtensionService`（对位 VSCode `ExtensionService` / `AbstractExtensionService.activateExtension`）实现 `IExtensionLifecycle`；与旧 `ExtensionActivationService` 的合并/改名以代码落地为准（见 §8 第 3 条）。

### 5.1 激活流程（`activateAll()`）

1. 收集 `ExtensionRegistry` 上的内置扩展描述。
2. 按 `activationEvents`（`"*"` / `"onStartupFinished"` 即启动激活）与 `dependencies` 做拓扑排序；有环或缺依赖时隔离该扩展，记诊断，不牵连其余。
3. 建实例 → `initialize(services)`。
4. 把 `description.contributes` mount 进 `ExtensionRegistry`。
5. 装配层 installer 把声明灌进各数据面表（见 §6）。
6. `onAttach(ui)` 接入界面。

失败隔离：单个扩展建实例 / 初始化失败只记诊断并跳过，不阻断其余扩展激活；headless 装配下无界面扩展的声明仍可查（纯数据，与 ui 无关）。

### 5.2 撤销

- `deactivate(identifier)` 对称撤销：命令 / 菜单 / 视图 / 状态整批撤，无悬空注册句柄。
- 撤销三件套的边界见 [VSCode 命名对位表 §5](vscode-alignment.md#5-撤销三件套边界写死不许互相替代)：运行层注册走 `IDisposable`，事件退订走 `Subscription`，owner 整批撤走注册表自身的 `unregisterOwner(owner)`，三者不许互相替代。

### 5.3 RuntimeHost 的退化

`RuntimeHost` 退化为执行 / 服务 / 注册表面：不再持有“模块表 + provides/requires 拓扑 + 模块装载”语义。激活编排归装配层生命周期实现。

- 现状落点：`src/application/host/include/RuntimeHost.h`。头文件已移除 `registerModule` / `ModuleRegistration` / `ModuleInfo` 依赖，改为持 `IExtensionLifecycle` 指针并转发 `activate` / `deactivate` / `deactivateAll` / `isActive` / `activeIds`；残余模块节点与拓扑清理见 §8 第 8 条。
- 组合根现状：`src/application/app/core/App.cpp` 遍历内置登记、交宿主激活；改为直接驱动装配层 `ExtensionService` 的时序待落地复核（§8 第 8 条）。

## 6. contributes 各项灌到哪张表

`ContributesDoc` 定义在 `src/libs/registry/include/ContributionParser.h`。装配层把各字段灌进对应数据面注册表，壳只读回渲染。

| contributes 键                | 目标数据面注册表                                                                | 代码落点                                                                     | VSCode 出处                                                                               |
| ----------------------------- | ------------------------------------------------------------------------------- | ---------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------- |
| `commands`                    | 命令表（`CommandsRegistry`）+ 命令面板菜单                                      | `ExtensionActivationService.cpp`；`src/libs/registry/include/MenuRegistry.h` | `vs/workbench/api/common/menusExtensionPoint.ts`                                          |
| `menus`（各 place）           | `MenuRegistry::appendMenuItem(menuId, item)`                                    | `src/libs/registry/include/MenuRegistry.h`                                   | `vs/workbench/api/common/menusExtensionPoint.ts`；`vs/platform/actions/common/actions.ts` |
| `viewsContainers` / `views`   | `ViewRegistry::declare(doc)`                                                    | `src/libs/registry/include/ViewRegistry.h`                                   | `vs/workbench/api/browser/viewsExtensionPoint.ts`                                         |
| `statusbar`                   | `StatusHub::registerSpec(spec)`                                                 | `src/libs/context/include/StatusHub.h`                                       | statusbar 贡献点（⚠️ 扩展点路径待核）                                                     |
| `colors`                      | 主题 token 面（语义名 + 兜底色，壳解析成画笔）                                  | `ContributionParser.h`（`ContributedColor`）；theme 读取面在壳主题层         | colors 贡献点（⚠️ 路径待核）                                                              |
| `customEditors`               | `ViewRegistry::registerEditorProvider`；声明先于 provider                       | `src/libs/registry/include/ViewRegistry.h`                                   | `contributes.customEditors`（清单字段）                                                   |
| `configuration`               | 设置服务（`ConfigurationSpec` / `ConfigurationProperty`），Settings editor 读回 | `ContributionParser.h`；装配层设置服务                                       | `contributes.configuration`（清单字段）                                                   |
| 视图标题 / 条目右键动作提供器 | `ViewRegistry::registerActionProvider`                                          | `src/libs/registry/include/ViewRegistry.h`                                   | `view/title` / `view/item/context`（菜单 place）                                          |

补充约定：

- place 存原串：`view/title`、`view/item/context`、`viewContainer/title`、`menubar/<pageId>`；菜单 place 不许写成点号（对位表 §4）。
- 声明层与运行层分离：先 mount 声明，再注册 provider 工厂；`unmount(owner)` 时声明与工厂整批撤。
- 菜单不是动态数据源：`MenuRegistry` 不创建 `QAction`、不渲染，那是 gui 层 `ActionBar` / Part 的事；变更经 `onDidChangeMenu` 增量通知。
- 业务文案边界：命令标签、分组名、面板标题、状态栏项文案、Activity 标签一律来自 contributes 声明，不进 `ProductSpec`（见 [裸壳契约 19b](shell-contract-c8-d18a-19.md)）。

## 7. 被替换掉的旧概念

Stage 1 落地后，下列概念整体退场。现状落点是 HEAD 基线，便于考古对照；新代码不许再引用旧名。

| 旧概念                                  | 现状落点（HEAD 基线）                                                                                                                                        | 目标去向                                                                                                                        |
| --------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------- |
| `BuiltinModuleHub`                      | `src/application/host/include/BuiltinModuleHub.h`（8 个模块尚未逐文件迁出）                                                                                  | 删除；登记归 `ExtensionRegistry::registerBuiltinExtension`                                                                      |
| `ModuleInfo`（`provides` / `requires`） | `src/libs/module/include/ModuleInfo.h`（`RuntimeHost.h` 已不再 include）                                                                                     | 并入 `ExtensionDescription`（`dependencies`）；`libs/module` 目标整库删除（CMake 一并）                                         |
| `PS_REGISTER_MODULE`                    | `BuiltinModuleHub.h` 内的宏                                                                                                                                  | 换 `PS_REGISTER_EXTENSION`（`description()` / `create()` 静态接口）                                                             |
| `ModuleContributions`                   | `src/libs/registry/include/ModuleContributions.h`（旧头仍在；接口已切到 `ContributesDoc`）                                                                   | 字段并入 `ContributesDoc`，owner 由 `identifier` 写；类型整体退场，旧头待删                                                     |
| `ContributionsInstaller`                | `src/application/assembly/include/ExtensionActivationService.h` 内；接口 `src/libs/workbench/include/IContributionsInstaller.h`（`install(ContributesDoc)`） | 收进 `ExtensionService` 激活链路；灌写目标随壳债清零改为 `MenuRegistry` / `ViewRegistry` / `StatusHub` / theme                  |
| 槽位面（已退役）                        | 已删除（原 `ISlotsService.h` / `UiSlotHub.h`）                                                                                                               | 页/组走 `contributes.ribbonPages/ribbonGroups`，条目走 `MenuRegistry`（`menubar/<pageId>` + `group`），活动走 `viewsContainers` |
| `RuntimeHost` 模块装载语义              | `RuntimeHost.h`（头文件已退化为生命周期转发；残余 `m_nodes` / 拓扑 / 静态节点待清）                                                                          | 退化为执行 / 服务 / 注册表面；激活编排归 `ExtensionService`                                                                     |

`ModuleContributions` 现字段与 `ContributesDoc` 的对位关系（迁移时逐字段核对，不丢呈现字段）：

- `ribbonPages` / `ribbonGroups` → `ContributesDoc::ribbonPages` / `ribbonGroups`（本仓 Ribbon 数据，注释对位 `menusExtensionPoint.ts` 的 place/group 与 `actions.ts` 的 `MenuId`/`MenuItem.group`）。
- `actions` → `ContributesDoc::actions`（灌 `commands` + `menus`）；`activities` → 视图入口声明；`themeTokens` → `colors`；`statuses` → 代码侧 `statusItems`（manifest 侧仍为 `ContributedStatus` 列表）；`actionProviders` → 视图动作提供器注册面。

## 8. 待代码落地后复核的断言

以下条目按落地进度逐条核对源码；已落地的标“已核”，未落地的落地后回改本文：

1. ~~`ExtensionDescription` 文件名与字段~~ **已核**：`src/libs/registry/include/ExtensionDescription.h`，字段 `identifier / name / version / isBuiltin / dependencies / activationEvents / contributes` 与 §3 一致。
2. 注册表 API **已落地**：`registerBuiltinExtension(desc, factory)`、`builtinExtensions()`、`builtinExtensionIds()`、`resetBuiltinExtensions()`，工厂类型 `ExtensionFactory`；复核最终签名是否保留、静态表是否仍复用 `StaticHub`。
3. 装配层激活服务最终形态：旧 `ExtensionActivationService`（命令绑定）与目标 `assembly::ExtensionService`（`IExtensionLifecycle` 实现）的合并/改名；接口方法名以 `IExtensionLifecycle` 为准（`activateAll` / `deactivate` / `deactivateAll` / `isActive` / `activeIds` / `descriptions`）。
4. ~~`RegistryKind::BuiltinModule` 收敛~~ **已核一半**：枚举值已从 `RegistryNode.h` 移除，内置扩展与扩展包同属 `Extension` 节点；复核 `diag.registries` 输出不再出现 `builtin-module` 节点、kind 字符串集合收敛。
5. ~~`ContributesDoc` 并入 Ribbon/提供器字段~~ **已核一半**：`ribbonPages / ribbonGroups / actions / activities / themeTokens / statusItems / actionProviders` 已并入；复核 `statuses` / `statusItems` 命名是否统一，以及 `ModuleContributions.h` 旧头与 `libs/module` 删除时机。
6. 8 个内置扩展（Analysis / Data / Plot / Protocol / Report / Script / Settings / Workspace）的 `identifier` 清单、`dependencies` 等价关系、`activationEvents` 取值；modules 逐文件迁移尚未完成。
7. `statusbar` 声明灌入 `StatusHub` 的最终接口名与归属层（装配层 vs 服务容器）。
8. `App.cpp` 组合根改为驱动装配层 `ExtensionService` 后的激活时序与失败隔离口径；`RuntimeHost` 残余模块节点（`m_nodes` / 依赖拓扑 / `StaticRegistryNodes` 模块节点）清理。

## 9. diag.registries 口径

- 全体注册对象挂一棵 `RegistryTree`；`RegistryNode` 构造即上树，析构自动摘除，`(kind, name)` 定位。代码落点：`src/libs/registry/include/RegistryNode.h`、`src/libs/registry/include/RegistryTree.h`。
- 扩展节点 `kind = Extension`：内置扩展与扩展包同属 `Extension` 节点，旧 `BuiltinModule` kind 已移除，不留两个 kind 讲同一件事。`ExtensionRegistry::count()` / `keysAsStrings()` 对外枚举口径 = 内置扩展 id ∪ contributes owner（同一 owner 只算一次）；只装外置 manifest 时扩展行也必须 `count > 0`，诊断不能说“没有扩展”而命令面板全是扩展命令。
- 诊断入口：`diag.registries` view action，诊断面板读回枚举。代码落点：`src/application/gui/views/diagnostics/DiagnosticsPanel.cpp`；宿主 action 注册在 `src/application/host/src/RuntimeHost.cpp`（落地后复核归属是否随 `ExtensionService` 迁移）。

## 10. 阅读顺序

1. 本文 §1–§5：模型、描述、注册表、激活。
2. `ExtensionDescription.h`：扩展静态描述字段。
3. `ExtensionRegistry.h`：表结构与登记/查询面。
4. `IExtensionLifecycle.h`：激活/反激活生命周期接口；`ExtensionActivationService.h` / `.cpp`：命令绑定与灌写现状。
5. `ContributionParser.h`：`ContributesDoc` 各字段定义。
6. 任一内置扩展实现（`src/application/modules/<域>/`）：`description()` / `create()` / contributes 声明实例。
