#pragma once

// 扩展/包的自注册入口（VSCode 扩展点注册函数的对位）。
//
// 模块 TU 只在文件末尾写一行 PS_REGISTER_EXTENSION(DataModule)，需要的只有本头：
// 不 include ExtensionRegistry.h（架构守卫禁止 modules 直接依赖注册表实现面）。
// 登记动作落到 ExtensionRegistry 的静态内置表，定义在 ExtensionRegistry.cpp。

#include "ExtensionDescription.h"

#include <QString>

#include <functional>
#include <memory>

namespace nextpilot::plotscope::host {
class IWorkbenchContribution;
}

namespace nextpilot::plotscope::registry {
class Pack;

// 内置扩展自注册（PS_REGISTER_EXTENSION 展开调用）。
bool registerBuiltinExtension(const ExtensionDescription &description,
                              std::function<nextpilot::plotscope::host::IWorkbenchContribution *()> factory);

// 内置包自注册（PS_REGISTER_PACK 展开调用）。
bool registerBuiltinPack(const QString &id, std::function<std::unique_ptr<Pack>()> factory);

} // namespace nextpilot::plotscope::registry

// 注册宏：在包 TU 文件末尾使用，把本包工厂登记进静态自注册表。
//
// 用法：
//   PS_REGISTER_PACK(nextpilot::plotscope::ext::ExamplePack)
//
// 宏必须带完整命名空间：它在调用处的命名空间作用域展开。
#define PS_REGISTER_PACK(FQN)                                                                                          \
    namespace {                                                                                                        \
    const bool g_ps_pack_reg_ = ::nextpilot::plotscope::registry::registerBuiltinPack(                                 \
        FQN::packId(), []() -> std::unique_ptr<::nextpilot::plotscope::registry::Pack> { return FQN::build(); });      \
    }

// 注册宏：在扩展 TU 文件末尾使用，把本扩展登记进内置扩展静态表。
//
// 用法：
//   PS_REGISTER_EXTENSION(DataModule)
//
// 其中 DataModule 需提供静态接口（无需实例化即可拿到描述与工厂）：
//   static ExtensionDescription extensionDescription();
//   static IWorkbenchContribution *create();
#define PS_REGISTER_EXTENSION(Cls)                                                                                     \
    namespace {                                                                                                        \
    const bool g_ps_extension_reg_ = ::nextpilot::plotscope::registry::registerBuiltinExtension(                       \
        Cls::extensionDescription(),                                                                                   \
        []() -> ::nextpilot::plotscope::host::IWorkbenchContribution * { return Cls::create(); });                     \
    }
