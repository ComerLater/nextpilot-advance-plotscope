#pragma once

// vscode.commands 对位：命令＝registerCommand(id, handler)，handler 与命令是同一个东西。
// 服务 id "vscode.commands"，实现是 command::CommandService。仓库不另起第二张命令表：
// 模块的 IIntentHandler 经 asCommandHandler 桥成 Handler 注册进同一张表；
// 执行链只有 CommandService::execute 一条（命令表 + 系统命令同一入口）。

#include "VsCodeTypes.h"

namespace nextpilot::plotscope::workbench {

class ICommands
{
public:
    virtual ~ICommands() = default;

    // 命令＝处理器：入参 QVariantMap（== Intent.params），返回 Result<QVariant>。
    using Handler = std::function<Result<QVariant>(const QVariantMap &args)>;

    // 注册命令处理器。同 id 二次注册失败（先到者为准）。
    virtual void registerCommand(const QString &id, Handler h) = 0;

    // 执行命令。命中则调用 handler(args)，否则返回 fail 结果。
    virtual Result<QVariant> executeCommand(const QString &id, const QVariantMap &args) = 0;

    // 注销命令（供 handler 生命周期结束时使用）。
    virtual void unregisterCommand(const QString &id) = 0;
};

} // namespace nextpilot::plotscope::workbench
