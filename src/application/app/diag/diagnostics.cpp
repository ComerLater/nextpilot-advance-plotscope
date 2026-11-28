#include "diagnostics.h"
#include "../core/App.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QVersionNumber>

using namespace nextpilot::plotscope::app;
using namespace nextpilot::plotscope::host;
using namespace nextpilot::plotscope::registry;

namespace nextpilot::plotscope::app::diag {

QJsonObject collect(App &application)
{
    QJsonObject root;

    // 应用与环境
    root[QStringLiteral("application")] = QJsonObject{
        {QStringLiteral("name"), QCoreApplication::applicationName()},
        {QStringLiteral("version"), QCoreApplication::applicationVersion()},
        {QStringLiteral("qt"), QString::fromLatin1(qVersion())},
    };

    // 内置扩展（原模块）
    QJsonArray modules;
    for (const auto &info : application.extensions().getExtensions())
    {
        modules.append(QJsonObject{
            {QStringLiteral("id"), info.identifier},
            {QStringLiteral("dependencies"), QJsonArray::fromStringList(info.dependencies)},
        });
    }
    root[QStringLiteral("modules")] = modules;

    // 结构化状态条目（登记序；会话快照与工作区预设的内容来源）。
    QJsonArray states;
    for (const QString &id : application.context().states().ids())
        states.append(id);
    root[QStringLiteral("states")] = states;

    // 扩展包
    QJsonArray packs;
    const auto &toolbox = (*static_cast<nextpilot::plotscope::registry::ExtensionRegistry *>(
        application.context().registryTree().find(nextpilot::plotscope::registry::RegistryKind::Extension)));
    for (const auto &pack : toolbox.packs.values())
    {
        packs.append(QJsonObject{
            {QStringLiteral("id"), pack.id},
            {QStringLiteral("name"), pack.name},
            {QStringLiteral("domain"), QString::fromLatin1(domainKey(pack.domain))},
        });
    }
    root[QStringLiteral("packs")] = packs;

    // 数据面：数据集/源数量（载入前为 0，诊断截屏常配合 --datafile 使用）
    root[QStringLiteral("data")] = QJsonObject{
        {QStringLiteral("datasets"), application.context().workspace().dataset().count()},
    };

    // AI 工具目录（MCP 桥/意图工具层的白名单面）
    const QJsonArray tools = AIToolsHandler::toolCatalog();
    root[QStringLiteral("aiTools")] = QJsonObject{
        {QStringLiteral("count"), tools.size()},
        {QStringLiteral("names"),
         [&] {
             QStringList names;
             for (const QJsonValue &v : tools)
                 names << v.toObject().value(QStringLiteral("name")).toString();
             return QJsonArray::fromStringList(names);
         }()},
    };

    return root;
}

} // namespace nextpilot::plotscope::app::diag
