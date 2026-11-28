#pragma once
// 测试专用：A0 §八.2 后 AppContext 不再对外开 registry 访问器。
// 白盒用例按 kind 从注册树取本体（与装配层同一路径）。
#include "AppContext.h"
#include "DatasetRegistry.h"
#include "ExtensionRegistry.h"
#include "RegistryNode.h"
#include "SourceRegistry.h"

namespace plotscope_test {
inline nextpilot::plotscope::dataset::DatasetRegistry &datasetHub(nextpilot::plotscope::context::AppContext &c)
{
    return *static_cast<nextpilot::plotscope::dataset::DatasetRegistry *>(
        c.registryTree().find(nextpilot::plotscope::registry::RegistryKind::Dataset));
}
inline nextpilot::plotscope::dataset::SourceRegistry &sourceHub(nextpilot::plotscope::context::AppContext &c)
{
    return *static_cast<nextpilot::plotscope::dataset::SourceRegistry *>(
        c.registryTree().find(nextpilot::plotscope::registry::RegistryKind::Source));
}
inline nextpilot::plotscope::registry::ExtensionRegistry &extensionHub(nextpilot::plotscope::context::AppContext &c)
{
    return *static_cast<nextpilot::plotscope::registry::ExtensionRegistry *>(
        c.registryTree().find(nextpilot::plotscope::registry::RegistryKind::Extension));
}
} // namespace plotscope_test