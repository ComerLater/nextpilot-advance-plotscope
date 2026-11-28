// 壳基线契约：不链任何业务模块/工具箱，只验工作台骨架本身。
//
// 与 test_gui.cpp 的分工：
//   test_gui_shell  —— 壳基线（Ribbon 页/组、活动栏壳级入口、菜单面、停靠宿主）
//   test_gui_<m>    —— 各模块挂回后的模块段（Step 2 逐个恢复）
//
// 本文件刻意只链 ps_gui / ps_gui_theme / ps_widgets / ps_app / ps_assembly 与 libs，
// 装配清单里一个业务目标都不出现：壳若偷偷依赖业务，这里编译就挂。

#include "App.h"
#include "AppContext.h"
#include "RuntimeHost.h"
#include "MainWindow.h"
#include "ShellBinder.h"
#include "BuiltinWorkbenchViews.h"
#include "BuiltinViewProviders.h"
#include "IRibbonService.h"
#include "ProductSpec.h"
#include "MenuRegistry.h"
#include "MenuId.h"

#include <QApplication>
#include <QWidget>

#include <cstdio>
#include <cstdlib>

using namespace nextpilot::plotscope::app;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::workbench;
using namespace nextpilot::plotscope::gui;
using namespace nextpilot::plotscope::host;

#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond);                                          \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)

#define OK(msg) fprintf(stdout, "[ok] %s\n", msg)

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication qtApp(argc, argv);

    App application;
    MainWindow window(application.context(), plotScopeProductSpec());

    // 壳内置工作台视图（装配层声明半 + gui 侧 provider 注册半），与 gui_main 同序。
    CHECK(nextpilot::plotscope::assembly::declareBuiltinWorkbenchViews(application.host().services()));
    CHECK(nextpilot::plotscope::gui::installBuiltinWorkbenchViewProviders(application.host().services()));

    window.build(application.host());
    window.resize(1280, 800);

    // --- A) 产品身份来自 ProductSpec，壳零硬编码 ---
    {
        const auto spec = plotScopeProductSpec();
        CHECK(window.windowTitle() == spec.displayName);
        CHECK(!spec.aboutText.isEmpty());
        OK("壳基线：窗口标题来自 ProductSpec");
    }

    // --- B) 骨架就位：Ribbon 宿主 / 停靠宿主 / 绑定器 ---
    {
        CHECK(window.menuWidget() != nullptr);
        auto *binder = window.binder();
        CHECK(binder != nullptr);
        CHECK(binder->dockHost() != nullptr);
        OK("壳基线：Ribbon 宿主与停靠宿主就位");
    }

    // --- C) 壳只拥有自己那 3 页：开始 / 文件 / 视图 ---
    // 页的归属要分清：壳声明 home/file/view 三个**容器**与 7 个组；
    // data/plot/script/protocol/analysis/report 六页归各业务模块，业务出壳后本就不该出现。
    // 所以这里断言"壳拥有的正好是这 3 页"，而不是全产品的 9 页——后者是模块挂回后的事。
    {
        auto *ribbonSvc = application.host().services()->getService<IRibbonService>(QStringLiteral("ribbon.readback"));
        CHECK(ribbonSvc != nullptr);
        const auto pages = ribbonSvc->ribbonPages();
        CHECK(pages.size() == 3);
        QStringList ids;
        for (const auto &pg : pages)
            ids << pg.id;
        CHECK(ids.contains(QStringLiteral("ribbon.page.home")));
        CHECK(ids.contains(QStringLiteral("ribbon.page.file")));
        CHECK(ids.contains(QStringLiteral("ribbon.page.view")));
        int groups = 0;
        for (const auto &page : pages)
            groups += ribbonSvc->ribbonGroupsOf(page.id).size();
        CHECK(groups == 7);
        OK("壳基线：Ribbon 3 页 + 7 组骨架就位（业务页归模块）");
    }

    // --- D) 活动栏壳级入口（diagnostics 归壳，不经任何模块）---
    {
        auto *binder = window.binder();
        CHECK(binder->activityBarWidget() != nullptr);
        const QStringList ids = binder->activityIds();
        CHECK(ids.contains(QStringLiteral("diagnostics")));
        OK("壳基线：活动栏只有壳级入口");
    }

    // --- E) 菜单面：壳标准动作落 MenuRegistry（menubar/<pageId>），不靠业务 ---
    {
        auto *menus = application.host().menuRegistry();
        CHECK(menus != nullptr);
        // 壳骨架动作（新建/打开/导出/退出/设置等）落在文件页与视图页。
        int shellItems = 0;
        shellItems += menus->getMenuItems(MenuId::menubar(QStringLiteral("ribbon.page.file"))).size();
        shellItems += menus->getMenuItems(MenuId::menubar(QStringLiteral("ribbon.page.view"))).size();
        CHECK(shellItems > 0);
        OK("壳基线：壳标准动作落在菜单面");
    }

    fprintf(stdout, "[shell] 壳基线通过\n");
    return 0;
}
