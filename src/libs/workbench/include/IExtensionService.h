#pragma once

// 扩展服务抽象。对位 VSCode `src/vs/workbench/services/extensions/common/extensionService.ts`
// 的 `class ExtensionService implements IExtensionService`；activationEvents 的编排细节
// 对位 `src/vs/workbench/services/extensions/common/extensionsActivator.ts`（ExtensionsActivator）。
//
// 分层：装配层（assembly::ExtensionService）实现本接口；调用方（壳/组合根/测试）只持抽象
// 指针，不反向依赖装配层。激活/反激活/拓扑/实例归属全部归实现方。

#include "ExtensionDescription.h"
#include "Intent.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace nextpilot::plotscope::host {
class IUiContext;
}

namespace nextpilot::plotscope::workbench {

// 方法名对齐 VSCode IExtensionService 语义：
//   activateByEvent        ↔ ExtensionService.activateByEvent(activationEvent)
//   deactivateExtension    ↔ ExtensionService.deactivateExtension(extension)
//   isActivated            ↔ ExtensionService.isActivated(extensionId)
//   getActivatedExtensions ↔ ExtensionService 内部已激活扩展表的读面
//   getExtensions          ↔ ExtensionService.getExtensions()
class IExtensionService
{
public:
    virtual ~IExtensionService() = default;

    // 按 activationEvent 激活匹配的内置扩展；disabledIds 排除；失败隔离，
    // 返回被跳过的扩展及原因。ui 非空时在激活后调用各扩展 onAttach。
    virtual QHash<QString, QString> activateByEvent(const QString &activationEvent, const QStringList &disabledIds,
                                                    nextpilot::plotscope::host::IUiContext *ui) = 0;

    // 反激活一个扩展：命令/菜单/视图/状态整批撤，无悬空。
    virtual bool deactivateExtension(const QString &extensionId) = 0;

    virtual bool isActivated(const QString &extensionId) const = 0;
    virtual QStringList getActivatedExtensions() const = 0;

    // 已登记（静态表）的扩展描述，供诊断/CLI 枚举。
    virtual QList<nextpilot::plotscope::registry::ExtensionDescription> getExtensions() const = 0;
};

} // namespace nextpilot::plotscope::workbench
