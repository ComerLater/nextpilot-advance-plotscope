#pragma once

// CommandService：命令执行服务（VSCode 对位 `vs/platform/commands/common/commands.ts`
// 的 CommandService / CommandsRegistry）。
//
// 职责：
//   - 命令表：委托 `CommandsRegistry`（id → handler，VSCode `CommandsRegistry` 同口径）；
//     CommandService 不另存一份 id 表，只把 ICommands / IIntentExecutor 的入口接到它上面；
//   - 系统命令：组合根/工作台内置动作注册的 handler（不归属任何可卸载扩展），
//     与扩展命令共用同一条 `execute` 路径，不存在第二条执行面；
//   - 执行：`executeCommand(id, args)` 与 `execute(Intent)` 同源——
//     两者都把 (id, args) 交给命令表的 handler。
//
// 命名全部对齐 VSCode：命令＝处理器（registerCommand），没有"意图处理器池"
// 这层中介；`Intent` 只是本仓库既有的入参载体（op == commandId、params == args）。

#include "CommandsRegistry.h"
#include "ICommands.h"
#include "IIntentExecutor.h"
#include "Intent.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

namespace nextpilot::plotscope::command {

class CommandService final : public nextpilot::plotscope::workbench::ICommands,
                             public nextpilot::plotscope::intent::IIntentExecutor
{
public:
    // registry 由组合根持有，二者同生命周期；CommandService 只做门面与意图派发。
    explicit CommandService(CommandsRegistry &registry) : m_registry(registry) {}

    CommandsRegistry &registry() { return m_registry; }
    const CommandsRegistry &registry() const { return m_registry; }

    // ---- workbench::ICommands ----
    void registerCommand(const QString &id, Handler h) override;
    nextpilot::plotscope::foundation::Result<QVariant> executeCommand(const QString &id,
                                                                      const QVariantMap &args) override;
    void unregisterCommand(const QString &id) override;

    // ---- intent::IIntentExecutor ----
    // 先查命令表（op == commandId），未命中再问系统命令 handler（canHandle）。
    // 命令表命中但 handler 失败时透传失败，不静默落到系统命令。
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult>
    execute(const nextpilot::plotscope::intent::Intent &intent) override;

    // 系统命令：由组合根/工作台内置动作注册，扩展装卸不影响。
    // ops 声明它认领的 commandId 列表（供 ai.tools 目录与诊断列举），不另建命名空间。
    void registerSystemHandler(const QString &key, nextpilot::plotscope::intent::IIntentHandler *handler,
                               const QStringList &ops = {});
    void unregisterHandler(nextpilot::plotscope::intent::IIntentHandler *handler);

    // 已注册命令列表（命令表 ∪ 系统命令声明的 ops），供 ai.tools / 诊断。
    QStringList commandIds() const;
    int commandCount() const { return m_registry.count(); }

private:
    CommandsRegistry &m_registry;
    // 系统命令：按注册顺序保存 key，保证执行顺序确定（QHash 遍历序不稳定）。
    QList<QString> m_systemOrder;
    QHash<QString, nextpilot::plotscope::intent::IIntentHandler *> m_system;
    QHash<QString, QStringList> m_systemOps;
};

} // namespace nextpilot::plotscope::command
