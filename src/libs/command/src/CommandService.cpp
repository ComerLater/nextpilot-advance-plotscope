#include "command/CommandService.h"

#include "ErrorCodes.h"

namespace nextpilot::plotscope::command {
namespace err = nextpilot::plotscope::foundation::err;
using nextpilot::plotscope::foundation::Result;
using nextpilot::plotscope::intent::Intent;
using nextpilot::plotscope::intent::IntentResult;

void CommandService::registerCommand(const QString &id, Handler h)
{
    if (id.isEmpty() || !h || m_commands.contains(id))
        return; // 先到者为准（VSCode CommandsRegistry 同口径）
    m_commands.insert(id, std::move(h));
}

Result<QVariant> CommandService::executeCommand(const QString &id, const QVariantMap &args)
{
    auto it = m_commands.constFind(id);
    if (it == m_commands.end())
        return nextpilot::plotscope::foundation::fail(QStringLiteral("E_NO_COMMAND"),
                                                      QStringLiteral("未注册命令: %1").arg(id));
    return (*it)(args);
}

void CommandService::unregisterCommand(const QString &id) { m_commands.remove(id); }

Result<IntentResult> CommandService::execute(const Intent &intent)
{
    if (intent.op.isEmpty())
        return nextpilot::plotscope::foundation::fail(err::Unsupported, QStringLiteral("空命令 id"));

    // 1) 命令表：commandId == Intent.op、args == Intent.params。
    auto it = m_commands.constFind(intent.op);
    if (it != m_commands.end())
    {
        Result<QVariant> r = (*it)(intent.params);
        if (!r.ok())
            return Result<IntentResult>(r.error());
        IntentResult out;
        out.payload = r.value();
        return Result<IntentResult>(out);
    }

    // 2) 系统命令：按注册顺序问 canHandle（认领式 handler，如 data.* / workbench.*）。
    for (const QString &key : m_systemOrder)
    {
        auto *handler = m_system.value(key, nullptr);
        if (handler && handler->canHandle(intent))
            return handler->execute(intent);
    }

    return nextpilot::plotscope::foundation::fail(err::Unsupported,
                                                  QStringLiteral("没有命令能处理意图: %1").arg(intent.op));
}

void CommandService::registerSystemHandler(const QString &key, nextpilot::plotscope::intent::IIntentHandler *handler,
                                           const QStringList &ops)
{
    if (!handler || key.isEmpty())
        return;
    if (!m_system.contains(key))
        m_systemOrder.append(key);
    m_system.insert(key, handler);
    QStringList declared = ops;
    if (declared.isEmpty())
        declared << key;
    m_systemOps.insert(key, declared);
}

void CommandService::unregisterHandler(nextpilot::plotscope::intent::IIntentHandler *handler)
{
    for (auto it = m_system.begin(); it != m_system.end(); ++it)
        if (it.value() == handler)
        {
            m_systemOrder.removeAll(it.key());
            m_systemOps.remove(it.key());
            m_system.erase(it);
            return;
        }
}

QStringList CommandService::commandIds() const
{
    QStringList out = m_commands.keys();
    for (auto it = m_systemOps.constBegin(); it != m_systemOps.constEnd(); ++it)
        for (const QString &op : it.value())
            if (!out.contains(op))
                out.append(op);
    out.sort();
    return out;
}

} // namespace nextpilot::plotscope::command
