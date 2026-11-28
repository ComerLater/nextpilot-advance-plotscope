#include "PreferencesIntentHandler.h"
#include "Intent.h"

#include "command/CommandService.h"

using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::command;

namespace nextpilot::plotscope::gui
{

PreferencesIntentHandler::PreferencesIntentHandler(CommandService &commands, QObject *parent)
    : QObject(parent), m_commands(commands)
{
}

bool PreferencesIntentHandler::canHandle(const Intent &intent) const
{
    return intent.op == QStringLiteral("preferences.open");
}

foundation::Result<IntentResult> PreferencesIntentHandler::execute(const Intent &intent)
{
    // 转派到工作台内置命令——最终执行体唯一（WorkbenchCommandHandler →
    // IEditorService::openEditor(workbench.editor.settings2, settings: 资源)）。
    // 本处理器不另造对话框、不重复实现打开逻辑，也不让壳碰编辑器服务。
    Intent open;
    open.op = QStringLiteral("workbench.action.openSettings");
    open.params = intent.params;
    return m_commands.execute(open);
}

} // namespace nextpilot::plotscope::gui
