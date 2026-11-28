#include "MenuId.h"

namespace nextpilot::plotscope::registry {

QString MenuId::menubar(const QString &pageId) { return QStringLiteral("menubar/") + pageId; }

QString MenuId::viewContainerTitle() { return QStringLiteral("viewContainer/title"); }

QString MenuId::viewTitle() { return QStringLiteral("view/title"); }

QString MenuId::viewItemContext() { return QStringLiteral("view/item/context"); }

} // namespace nextpilot::plotscope::registry
