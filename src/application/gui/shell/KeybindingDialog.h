#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

#include "command/CommandsRegistry.h"
#include "command/KeybindingRegistry.h"

#include <functional>

class QLabel;
class QTableWidget;

namespace nextpilot::plotscope::gui
{

using command::CommandDescriptor;
using command::CommandsRegistry;
using command::KeybindingConflict;
using command::KeybindingRegistry;

// 快捷键编辑器：逐条命令改当前序列，留空恢复默认。
class KeybindingDialog : public QDialog
{
public:
    using SaveHandler = std::function<void()>;

    KeybindingDialog(CommandsRegistry &commands, KeybindingRegistry &bindings, QWidget *parent = nullptr);

    void setSaveHandler(SaveHandler handler) { m_saveHandler = std::move(handler); }
    QStringList editedSequences() const;
    QStringList conflictMessages() const;

private:
    void save();

    CommandsRegistry &m_commands;
    KeybindingRegistry &m_bindings;
    SaveHandler m_saveHandler;
    QTableWidget *m_table = nullptr;
    QLabel *m_conflicts = nullptr;
};

} // namespace nextpilot::plotscope::gui
