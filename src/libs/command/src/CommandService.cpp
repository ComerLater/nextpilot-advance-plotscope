#include "command/CommandService.h"

#include "ErrorCodes.h"

namespace nextpilot::plotscope::command {
namespace err = nextpilot::plotscope::foundation::err;
using nextpilot::plotscope::foundation::Result;
using nextpilot::plotscope::intent::Intent;
using nextpilot::plotscope::intent::IntentResult;

void CommandService::registerCommand(const QString &id, Handler h)
{
    // 先到者为准由 CommandsRegistry 保证；这里只做门面转发。
    m_registry.registerCommand(id, std::move(h));
}

Result<QVariant> CommandService::executeCommand(const QString &id, const QVariantMap &args)
{
    Result<QVariant> out{QVariant()};
    QString error;
    if (!m_registry.executeById(id, args, &out, &error))
    {
        if (out.error().code.isEmpty())
            return nextpilot::plotscope::foundation::fail(QStringLiteral("E_NO_COMMAND"), error);
        return out;
    }
    return out;
}

void CommandService::unregisterCommand(const QString &id) { m_registry.unregisterCommand(id); }

Result<IntentResult> CommandService::execute(const Intent &intent)
{
    if (intent.op.isEmpty())
        return nextpilot::plotscope::foundation::fail(err::Unsupported, QStringLiteral("空命令 id"));

    // 1) 命令表：commandId == Intent.op、args == Intent.params（handler 表在 CommandsRegistry）。
    if (m_registry.hasHandler(intent.op))
    {
        Result<QVariant> r{QVariant()};
        QString error;
        const bool ok = m_registry.executeById(intent.op, intent.params, &r, &error);
        if (!ok)
        {
            if (!r.ok())
                return Result<IntentResult>(r.error());
            return nextpilot::plotscope::foundation::fail(QStringLiteral("E_NO_COMMAND"), error);
        }
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
    QStringList out = m_registry.commandIds();
    for (auto it = m_systemOps.constBegin(); it != m_systemOps.constEnd(); ++it)
        for (const QString &op : it.value())
            if (!out.contains(op))
                out.append(op);
    out.sort();
    return out;
}

} // namespace nextpilot::plotscope::command
