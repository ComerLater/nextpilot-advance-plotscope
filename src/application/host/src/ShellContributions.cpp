#include "ShellContributions.h"

#include "ISlotsService.h"
#include "MenuId.h"
#include "MenuRegistry.h"
#include "Contribution.h"

namespace nextpilot::plotscope::host
{

namespace
{

// 声明清单的唯一事实源（裁定 52442006 Step 4）：两个通道（UiSlotHub 直连=host
// 内部；ISlotsService=壳）共用同一组回调，避免双份清单漂移。
template <typename PageFn, typename GroupFn, typename ActivityFn>
void applyShellContributions(PageFn page, GroupFn group, ActivityFn activity)
{
    // 声明者统一是 "app.skeleton"：它们不属于任何可卸载的模块或扩展包。
    const QString owner = QStringLiteral("app.skeleton");

    // --- 壳级页面 ---
    // 开始页（order 0）：产品级聚合落地页，暂由壳声明；等出现独立 home 模块再迁。
    // 边界：这里只给**容器**，页内具体业务条目仍由各模块贡献（如 DataModule 把
    // 打开/最近挂到 home.data 组），壳不预置业务动作。
    page(QStringLiteral("ribbon.page.home"), QStringLiteral("开始"), 0);
    group(QStringLiteral("ribbon.group.home.data"), QStringLiteral("常用"),
          QStringLiteral("ribbon.page.home"), 0);

    // 文件页（order 1）：文件 / 导出 / 截图 / 帮助四组。
    page(QStringLiteral("ribbon.page.file"), QStringLiteral("文件"), 1);
    group(QStringLiteral("ribbon.group.file.document"), QStringLiteral("文件"),
          QStringLiteral("ribbon.page.file"), 0);
    group(QStringLiteral("ribbon.group.file.export"), QStringLiteral("导出"),
          QStringLiteral("ribbon.page.file"), 1);
    group(QStringLiteral("ribbon.group.file.screenshot"), QStringLiteral("截图"),
          QStringLiteral("ribbon.page.file"), 2);
    group(QStringLiteral("ribbon.group.file.help"), QStringLiteral("帮助"),
          QStringLiteral("ribbon.page.file"), 3);

    // 视图页（order 8）：面板开关 + 外观。
    page(QStringLiteral("ribbon.page.view"), QStringLiteral("视图"), 8);
    group(QStringLiteral("ribbon.group.view.panels"), QStringLiteral("面板"),
          QStringLiteral("ribbon.page.view"), 0);
    group(QStringLiteral("ribbon.group.view.appearance"), QStringLiteral("外观"),
          QStringLiteral("ribbon.page.view"), 1);

    // 诊断入口归外壳（原 MainWindow 声明，A8b-M2 迁此）。
    nextpilot::plotscope::registry::ActivitySpec diagnostics;
    diagnostics.id = QStringLiteral("diagnostics");
    diagnostics.label = QStringLiteral("诊断");
    diagnostics.icon = QStringLiteral("diagnostics");
    diagnostics.order = 3;
    diagnostics.ribbonPage = QStringLiteral("ribbon.page.view");
    diagnostics.provider = owner;
    activity(diagnostics);
}

} // namespace

void declareShellContributions(nextpilot::plotscope::registry::UiSlotHub &registry)
{
    using registry::SlotKind;
    applyShellContributions(
        [&registry](const QString &id, const QString &label, int order) {
            registry.declareSlot(id, label, QStringLiteral("app.skeleton"), SlotKind::RibbonPage,
                                 QString(), order);
        },
        [&registry](const QString &id, const QString &label, const QString &parent, int order) {
            registry.declareSlot(id, label, QStringLiteral("app.skeleton"), SlotKind::RibbonGroup,
                                 parent, order);
        },
        [&registry](const registry::ActivitySpec &spec) { registry.registerActivity(spec); });
}

void declareShellContributions(nextpilot::plotscope::workbench::ISlotsService &slotSvc)
{
    using registry::SlotKind;
    applyShellContributions(
        [&slotSvc](const QString &id, const QString &label, int order) {
            slotSvc.declareSlot(id, label, QStringLiteral("app.skeleton"), SlotKind::RibbonPage,
                                QString(), order);
        },
        [&slotSvc](const QString &id, const QString &label, const QString &parent, int order) {
            slotSvc.declareSlot(id, label, QStringLiteral("app.skeleton"), SlotKind::RibbonGroup,
                                parent, order);
        },
        [&slotSvc](const registry::ActivitySpec &spec) { slotSvc.registerActivity(spec); });
}

void declareShellActions(registry::MenuRegistry &menus)
{
    // 组 → 页映射：与上方 applyShellContributions 声明同源。
    static const struct
    {
        const char *group;
        const char *page;
    } kGroupPage[] = {
        {"ribbon.group.file.document", "ribbon.page.file"},   {"ribbon.group.file.export", "ribbon.page.file"},
        {"ribbon.group.file.screenshot", "ribbon.page.file"}, {"ribbon.group.file.help", "ribbon.page.file"},
        {"ribbon.group.view.appearance", "ribbon.page.view"},
    };
    auto pageOf = [](const QString &group) -> QString {
        for (const auto &entry : kGroupPage)
            if (QLatin1String(entry.group) == group)
                return QString::fromLatin1(entry.page);
        return {};
    };

    const QString owner = QStringLiteral("app.skeleton");
    auto add = [&menus, &pageOf, &owner](const QString &id, const QString &label, const QString &group,
                                         const QString &op, const QString &icon, const QString &shortcut, int order) {
        registry::UiAction a;
        a.id = id;
        a.label = label;
        a.slotId = group;
        a.intentOp = op;
        a.icon = icon;
        a.shortcut = shortcut;
        a.order = order;
        const QString pageId = pageOf(group);
        if (pageId.isEmpty())
            return;
        menus.appendMenuItem(registry::MenuId::menubar(pageId), registry::IMenuItem{a, group, owner});
    };

    // 文件页 · 文件组：打开(0) / 导入布局(1) / 设置(2) / 打开工作区(3) / 保存工作区(4)
    add(QStringLiteral("file.open"), QStringLiteral("打开文件"), QStringLiteral("ribbon.group.file.document"),
        QStringLiteral("data.load"), QStringLiteral("file_open"), QStringLiteral("Ctrl+O"), 0);
    add(QStringLiteral("file.importLayout"), QStringLiteral("导入布局"), QStringLiteral("ribbon.group.file.document"),
        QStringLiteral("layout.import"), QStringLiteral("import"), QString(), 1);
    add(QStringLiteral("file.settings"), QStringLiteral("设置"), QStringLiteral("ribbon.group.file.document"),
        QStringLiteral("preferences.open"), QStringLiteral("settings"), QStringLiteral("Ctrl+,"), 2);
    add(QStringLiteral("workspace.open"), QStringLiteral("打开工作区"), QStringLiteral("ribbon.group.file.document"),
        QString(), QStringLiteral("file_open"), QString(), 3);
    add(QStringLiteral("workspace.save"), QStringLiteral("保存工作区"), QStringLiteral("ribbon.group.file.document"),
        QString(), QStringLiteral("export"), QString(), 4);

    // 视图页 · 外观组：切换主题(0)
    add(QStringLiteral("app.action.toggleTheme"), QStringLiteral("切换主题"),
        QStringLiteral("ribbon.group.view.appearance"), QStringLiteral("theme.toggle"), QStringLiteral("theme_toggle"),
        QStringLiteral("Ctrl+T"), 0);

    // 文件页 · 导出组：保存布局(0) / 导出设置(1) / 导出数据(2)
    add(QStringLiteral("file.saveLayout"), QStringLiteral("保存布局"), QStringLiteral("ribbon.group.file.export"),
        QStringLiteral("layout.save"), QStringLiteral("export"), QStringLiteral("Ctrl+S"), 0);
    add(QStringLiteral("file.exportSettings"), QStringLiteral("导出设置"), QStringLiteral("ribbon.group.file.export"),
        QStringLiteral("settings.export"), QStringLiteral("export_cfg"), QString(), 1);
    add(QStringLiteral("file.exportData"), QStringLiteral("导出数据"), QStringLiteral("ribbon.group.file.export"),
        QStringLiteral("data.export"), QStringLiteral("export"), QString(), 2);

    // 文件页 · 截图组：截图(0)
    add(QStringLiteral("file.screenshot"), QStringLiteral("截图"), QStringLiteral("ribbon.group.file.screenshot"),
        QStringLiteral("file.screenshot"), QStringLiteral("screenshot"), QString(), 0);

    // 文件页 · 帮助组：关于(0)
    add(QStringLiteral("help.about"), QStringLiteral("关于"), QStringLiteral("ribbon.group.file.help"),
        QStringLiteral("help.about"), QStringLiteral("settings"), QString(), 0);
}

registry::ContributesDoc shellContributes()
{
    registry::ContributesDoc doc;
    doc.owner = QStringLiteral("app.skeleton");
    applyShellContributions(
        [&doc](const QString &id, const QString &label, int order) {
            registry::RibbonPageDecl pageDecl;
            pageDecl.id = id;
            pageDecl.label = label;
            pageDecl.order = order;
            doc.ribbonPages.append(pageDecl);
        },
        [&doc](const QString &id, const QString &label, const QString &parent, int order) {
            registry::RibbonGroupDecl groupDecl;
            groupDecl.id = id;
            groupDecl.label = label;
            groupDecl.pageId = parent;
            groupDecl.order = order;
            doc.ribbonGroups.append(groupDecl);
        },
        [&doc](const registry::ActivitySpec &spec) {
            registry::ContributedViewContainer container;
            container.id = spec.id;
            container.title = spec.label;
            container.area = QStringLiteral("activitybar");
            container.icon = spec.icon;
            container.order = spec.order;
            container.ribbonPage = spec.ribbonPage;
            container.panelTypes = spec.panelTypes;
            container.centralEditor = spec.centralEditor;
            container.centralRef = spec.centralRef;
            doc.viewContainers.append(container);
        });
    return doc;
}
} // namespace nextpilot::plotscope::host
