find_package(Qt6 REQUIRED COMPONENTS Core Network)

add_library(ps_host STATIC
    src/DataLoadHandler.cpp
    src/DataCommandsHandler.cpp
    src/XlsxBook.cpp
    src/AIToolsHandler.cpp
    src/ShellContributions.cpp
    src/Toolbox.cpp
    src/PluginScanner.cpp
    include/DataLoadHandler.h
    include/DataCommandsHandler.h
    include/XlsxBook.h
    include/AIToolsHandler.h
    include/ShellContributions.h
    include/StaticRegistryNodes.h
    include/Toolbox.h
    include/PluginScanner.h
)

target_include_directories(ps_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# 录制引擎（Recorder）：data.record.* 意图要直接创建它，故 host 公开链接。
# host 只提供系统命令 handler 与静态注册节点；命令执行归 ps_command 的 CommandService，
# 扩展装载归装配层 ExtensionService。
target_link_libraries(ps_host PUBLIC ps_workbench ps_context ps_registry ps_recording Qt6::Core Qt6::Network)
set_target_properties(ps_host PROPERTIES AUTOMOC ON CXX_STANDARD 17)
