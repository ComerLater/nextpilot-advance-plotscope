// data 模块段：壳基线 + data 模块挂回后的契约（第 2 步第 1 个模块）。
//
// 与 test_gui_shell 的分工：
//   test_gui_shell  —— 只有壳，验骨架本身
//   test_gui_data   —— 壳 + data 模块（+ 公共解析器），验"数据源面板/加载/重载"这条链路
//
// 装配清单里只出现 ps_module_data / ps_module_data_gui 与 ps_toolbox_common（CSV 解析器），
// 不出现 plot/script/protocol/analysis/report：这些模块的段归它们自己的文件。

#include "App.h"
#include "MainWindow.h"
#include "GuiContext.h"
#include "ShellBinder.h"
#include "BuiltinWorkbenchViews.h" // 装配层：壳内置工作台视图声明半
#include "BuiltinViewProviders.h"  // gui 侧：壳内置工作台视图 provider 注册半
#include "DataViewFactories.h"     // data 模块视图装配：datasetTree view + data.table editor
#include "DataManagerPanel.h"
#include "Region.h"
#include "ProductSpec.h"
#include "test_registry_access.h"

#include <QApplication>
#include <QObject>
#include <QStringList>
#include <QWidget>

#include <cstdio>
#include <cstdlib>

using namespace nextpilot::plotscope::app;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::workbench;
using namespace nextpilot::plotscope::gui;
using namespace nextpilot::plotscope::host;
using namespace nextpilot::plotscope::intent;

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

    // 与 gui_main 同序：壳内置视图（声明半 + provider 半）→ data 模块视图装配 → build。
    CHECK(nextpilot::plotscope::assembly::declareBuiltinWorkbenchViews(application.host().services()));
    CHECK(nextpilot::plotscope::gui::installBuiltinWorkbenchViewProviders(application.host().services()));
    CHECK(nextpilot::plotscope::data::installDataViewFactories(application.host().services(), &application.context(),
                                                               &application.host()));

    window.build(application.host());
    window.resize(1280, 800);

    auto *binder = window.binder();
    CHECK(binder != nullptr);

    // --- 数据源面板就位：模块 openView 申请、视图层 provider 创建 ---
    // 面板在 dock.left 里可能与其他面板并列且顺序不定，按类型查找。
    DataManagerPanel *panel = nullptr;
    for (QWidget *w : binder->panelWidgets(Region::PrimarySidebar))
        if (!panel)
            panel = qobject_cast<DataManagerPanel *>(w);
    CHECK(panel != nullptr);

    // --- 1) 真实加载路径：data.load 意图 → 摄取管道 → 源列表出现 ---
    const QString fixture = QStringLiteral(PS_TEST_FIXTURE_DIR "/demo_flight.csv");
    // 加载生命周期推送捕获：真实 data.load 入口应先推"加载中…"，摄取结束回终态。
    QStringList progressEvents;
    QObject::connect(window.context(), &GuiContext::statusTextChanged, &window,
                     [&progressEvents](const QString &id, const QString &text) {
                         if (id == QStringLiteral("data.loading.progress"))
                             progressEvents << text;
                     });
    Intent load;
    load.op = QStringLiteral("data.load");
    load.params[QStringLiteral("path")] = fixture;
    const auto loaded = application.host().execute(load);
    CHECK(loaded);
    // start 早于 finish：只测终态等于没测。
    const int startIdx = progressEvents.indexOf(QStringLiteral("加载中…"));
    CHECK(startIdx >= 0);
    int finishIdx = -1;
    for (int i = 0; i < progressEvents.size(); ++i)
    {
        if (progressEvents.at(i) == QStringLiteral("空闲") || progressEvents.at(i) == QStringLiteral("加载失败"))
        {
            finishIdx = i;
            break;
        }
    }
    CHECK(finishIdx >= 0);
    CHECK(startIdx < finishIdx);
    CHECK(window.context()->statusText(QStringLiteral("data.loading.progress")) == QStringLiteral("空闲"));
    const int loadedDsId = loaded.value().payload.toMap().value(QStringLiteral("dataset")).toInt();
    CHECK(loadedDsId >= 0);

    panel->refresh();
    CHECK(panel->sourceNodeCount() == 1);
    CHECK(!panel->sourceStateText(0).isEmpty());
    CHECK(panel->shownSourceId() >= 0);
    CHECK(panel->activeChip() == 0);
    CHECK(panel->datasetNodeCount() == 1);
    CHECK(panel->tableNodeCount(0) >= 1);
    CHECK(panel->chipText(0).startsWith(QStringLiteral("字段")));
    CHECK(panel->chipText(1).startsWith(QStringLiteral("参数")));
    CHECK(panel->chipText(2).startsWith(QStringLiteral("消息")));
    OK("data 段：源列表 + 字段视图（源 → 表 → 列）+ 三 chip 就位");

    // --- 2) 活动源 id 与数据集 id 是两套空间 ---
    // data.reload 按**源** id 找源；视图若把数据集 id 写进活动源，handler 永远找不到源。
    const int sourceId = plotscope_test::sourceHub(application.context()).sourceOfDataset(loadedDsId);
    CHECK(sourceId >= 0);
    const auto *info = plotscope_test::sourceHub(application.context()).get(sourceId);
    CHECK(info != nullptr);
    CHECK(info->datasetIds.contains(loadedDsId));
    OK("data 段：源 id 与数据集 id 分离，sourceOfDataset 反查正确");

    // --- 3) data.reload 按源 id 重载成功，数据集 id 稳定（绘图视图不失联）---
    {
        Intent rel;
        rel.op = QStringLiteral("data.reload");
        rel.params[QStringLiteral("id")] = sourceId;
        const auto r = application.host().execute(rel);
        CHECK(r);
        const int newDsId = r.value().payload.toMap().value(QStringLiteral("dataset")).toInt();
        CHECK(newDsId == loadedDsId);
        panel->refresh();
        CHECK(panel->sourceNodeCount() == 1);
        OK("data 段：data.reload 按源 id 重载成功，数据集 id 稳定");
    }

    // 退出前先拆模块：本函数里 window 先于 application 析构，而 ~App 还会再跑一次
    // deactivateAll。不先拆，那次 deactivateAll 就落到已析构的 ExtensionService 上
    // （云端实测 SIGSEGV：~App → ~RuntimeHost → deactivateAll → getActivatedExtensions）。
    application.host().deactivateAll();

    fprintf(stdout, "[shell+data] 模块段通过\n");
    return 0;
}
