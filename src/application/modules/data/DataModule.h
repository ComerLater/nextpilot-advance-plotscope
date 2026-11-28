#pragma once

#include "ISubscriptions.h"
#include "IWorkbenchContribution.h"

#include <QList>
#include <QPair>
#include <QVector>

namespace nextpilot::plotscope::registry {
struct UiAction;
}
namespace nextpilot::plotscope::parser {
class IMessageParser;
}
namespace nextpilot::plotscope::host {
}
namespace nextpilot::plotscope::uischema {
class IScriptCapabilities; // initialize 注册 view action 后留存，onDetach 注销
}

// 数据模块：管理"打开了哪些数据集"，不认识任何具体格式。
// 格式解析由扩展完成，data 模块只消费仓库里的 Dataset。
class DataModule final : public nextpilot::plotscope::host::IWorkbenchContribution
{
    Q_OBJECT
public:
    // 静态描述：供编译期自注册（PS_REGISTER_EXTENSION）使用，无需实例化。
    static nextpilot::plotscope::registry::ExtensionDescription extensionDescription();
    static nextpilot::plotscope::host::IWorkbenchContribution *create();

    nextpilot::plotscope::registry::ExtensionDescription description() const override;
    void initialize(nextpilot::plotscope::instantiation::InstantiationService &services) override;

    // GUI 环境下请求自己的界面部件（headless 时宿主不调用）。
    void onAttach(nextpilot::plotscope::host::IUiContext &ui) override;
    // 卸载：注销本模块注册的 view action，避免留下指向已销毁实例的闭包。
    void onDetach() override;
    nextpilot::plotscope::registry::ContributesDoc contributes() const override;

    // 纯数据派生：按已装载的格式驱动生成"来源"画廊条目——每个驱动一条"文件格式"，
    // 前四条再进"常用"组。条目集合严格等于入参驱动列表的派生，壳里不保留常量表。
    // headless 可测：装/卸驱动，图块随之增减。
    static QList<nextpilot::plotscope::registry::UiAction>
    sourceGalleryActions(const QList<nextpilot::plotscope::parser::IMessageParser *> &drivers);
    // 元数据重载：initialize 从 IWorkspace::sourceDriverMetas() 缓存，onAttach 用它生成图块。
    static QList<nextpilot::plotscope::registry::UiAction>
    sourceGalleryActions(const QVector<QPair<QString, QString>> &metas);

private:
    // 数据表格查看器入口（data.table.open）在 initialize 注册，
    // 运行时经 m_ui 按需创建中央页签面板（GUI 环境才有值）。
    nextpilot::plotscope::host::IUiContext *m_ui = nullptr;
    nextpilot::plotscope::uischema::IScriptCapabilities *m_caps = nullptr; // initialize 留存，onDetach 用
    QVector<QPair<QString, QString>> m_driverMetas; // initialize 缓存的格式驱动 {id, displayName}
    // 数据集增删订阅（vscode.workspace 的 onDidAddDocument/onDidRemoveDocument），
    // 取代原先直连 DatasetRegistry 的 Qt 信号。
    nextpilot::plotscope::foundation::Subscription m_datasetCountSub;
    nextpilot::plotscope::foundation::Subscription m_datasetCountSub2;
};
