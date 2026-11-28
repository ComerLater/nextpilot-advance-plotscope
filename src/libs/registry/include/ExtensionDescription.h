#pragma once

// 内置扩展静态描述。对位 VSCode `src/vs/platform/extensions/common/extensions.ts`
// 的 IExtensionDescription：identifier 是唯一主键，contributes 是该扩展唯一一份
// UI 声明（owner 即 identifier）。
//
// 与 VSCode 的对应：
//   identifier     ↔ IExtensionDescription.identifier
//   name            ↔ IExtensionDescription.displayName
//   version         ↔ IExtensionDescription.version
//   dependencies    ↔ IExtensionDescription.extensionDependencies（依赖的 identifier）
//   activationEvents↔ IExtensionDescription.activationEvents（"*" / "onStartupFinished" /
//                     "onCommand:<id>"）
//   contributes     ↔ package.json 的 contributes 字段（ContributesDoc 纯数据）
//
// 工厂/激活入口不塞进 description：注册 API 走 ExtensionRegistry::registerBuiltinExtension，
// 激活归装配层 ExtensionService（对位 AbstractExtensionService.activateExtension）。

#include "ContributionParser.h"

#include <QString>
#include <QStringList>

namespace nextpilot::plotscope::registry {

struct ExtensionDescription
{
    QString identifier; // 唯一主键（原模块 id，如 "mod.data"）
    QString name;       // 显示名
    QString version;
    bool isBuiltin = true;
    QStringList dependencies;     // 依赖的 identifier（取代旧 provides/requires 能力名）
    QStringList activationEvents; // "*" / "onStartupFinished" / "onCommand:<id>"
    ContributesDoc contributes;   // 唯一一份 UI 声明；owner 即 identifier
};

} // namespace nextpilot::plotscope::registry
