# ArchitectureGuard.cmake —— 三条铁律的 configure 期硬守卫（A4a）。
#
# 与 LibBoundaryGuard / LayerContractGuard / ShellLinkGuard 互补：那三个查库边界、
# 层契约、壳链接面；本守卫查**源码级 include 越界与文件夹归属**，专治"纸面合规、
# 源码偷编"的破口（如 LuaEditor 曾靠 ps_context 传递偷编 #include "Dataset.h"）。
#
# 违规一律 FATAL_ERROR——编译还没开始就红，不靠人工纪律。
# 宁可漏报不可误报：先剥离注释再扫描；抽象接口头（IMessageParser.h 在 libs/parser）
# 不算业务头，只有 toolbox/registry 容器头与具体业务头才拦。

get_filename_component(_PS_ARCH_GUARD_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# 读取文件并剥离 // 与 /* */ 注释，返回扫描文本。
function(_ps_arch_strip_comments _path _out)
  file(READ "${_path}" _txt)
  string(REGEX REPLACE "//[^\n\r]*" "" _scan "${_txt}")
  string(REGEX REPLACE "/\\*[^\n\r]*\\*/" "" _scan "${_scan}")
  set(${_out} "${_scan}" PARENT_SCOPE)
endfunction()

function(_ps_arch_guard_includes)
  # 规则表：目录 glob → 禁止的 include 正则 → 说明。
  # 1) 裸壳：零业务 include（铁律1）
  # 2) 通用控件库 libs/widget：零业务 include（可给第三方复用）
  # 3) modules：不得 include toolbox/parser 容器头（铁律2）；IMessageParser.h 是抽象接口，合规
  set(_rules
    "application/gui/shell/*|ExtensionRegistry[.]h|裸壳禁止 include 扩展注册表头（贡献由装配期注入）"
    "application/gui/shell/*|Dataset[.]h|裸壳禁止 include Dataset.h（业务数据）"
    "application/gui/shell/*|DatasetRegistry[.]h|裸壳禁止 include DatasetRegistry.h（业务数据）"
    "application/gui/shell/*|SourceRegistry[.]h|裸壳禁止 include SourceRegistry.h（业务数据）"
    "application/gui/shell/*|IngestPipeline[.]h|裸壳禁止 include IngestPipeline.h（业务摄取）"
    "application/gui/shell/*|Transform[.]h|裸壳禁止 include Transform.h（业务变换）"
    "application/gui/shell/*|IMessageParser[.]h|裸壳禁止 include 解析器接口"
    "application/gui/parts/*|ExtensionRegistry[.]h|Part 禁止 include 扩展注册表头（贡献由装配期注入）"
    "application/gui/parts/*|Dataset[.]h|Part 禁止 include Dataset.h（业务数据）"
    "application/gui/parts/*|DatasetRegistry[.]h|Part 禁止 include DatasetRegistry.h（业务数据）"
    "application/gui/parts/*|SourceRegistry[.]h|Part 禁止 include SourceRegistry.h（业务数据）"
    "application/gui/parts/*|IngestPipeline[.]h|Part 禁止 include IngestPipeline.h（业务摄取）"
    "application/gui/parts/*|Transform[.]h|Part 禁止 include Transform.h（业务变换）"
    "application/gui/parts/*|IMessageParser[.]h|Part 禁止 include 解析器接口"
    "libs/widget/*|ExtensionRegistry[.]h|通用控件库禁止 include 扩展注册表"
    "libs/widget/*|Dataset[.]h|通用控件库禁止 include Dataset.h（走能力注入采样）"
    "libs/widget/*|DatasetRegistry[.]h|通用控件库禁止 include DatasetRegistry.h"
    "libs/widget/*|SourceRegistry[.]h|通用控件库禁止 include SourceRegistry.h"
    "libs/context/include/AppContext|DatasetRegistry[.]h|上下文壳 AppContext 禁止 include 数据集仓库头（A0 八.2）"
    "libs/context/include/AppContext|SourceRegistry[.]h|上下文壳 AppContext 禁止 include 数据源仓库头（A0 八.2）"
    "libs/context/include/AppContext|ExtensionRegistry[.]h|上下文壳 AppContext 禁止 include 扩展注册表头（A0 八.2）"
    "libs/context/include/AppContext|Transform[.]h|上下文壳 AppContext 禁止 include 变换描述符头（A0 八.2）"
    "libs/workbench/include/*|DatasetRegistry[.]h|工作台接口层禁止 include 数据集仓库头（A0 八.2）"
    "libs/workbench/include/*|SourceRegistry[.]h|工作台接口层禁止 include 数据源仓库头（A0 八.2）"
    "libs/workbench/include/*|ExtensionRegistry[.]h|工作台接口层禁止 include 扩展注册表头（A0 八.2）"
    "libs/workbench/include/*|TransformHub[.]h|工作台接口层禁止 include 变换 hub 头（A0 八.2）"
    "libs/workbench/include/*|IngestPipeline[.]h|工作台接口层禁止 include 摄取管道头（A0 八.2）"
    "libs/workbench/include/*|Toolbox[.]h|工作台接口层禁止 include toolbox 头（A0 八.2）"
    # 4) 上下文壳 AppContext：对外只开接口，禁止把业务 registry 头拉回来（A0 §八.2）；5) 工作台接口层同上。IngestOrchestrator 是 context 写入路径，保留 registry 形参但只做前置声明，不再 include 业务头
    "application/modules/*|ExtensionRegistry[.]h|modules 禁止 include 扩展注册表头（贡献由装配期注入）"
  )
  foreach(_rule IN LISTS _rules)
    string(REPLACE "|" ";" _parts "${_rule}")
    list(GET _parts 0 _glob)
    list(GET _parts 1 _rx)
    list(GET _parts 2 _why)
    file(GLOB_RECURSE _files
      "${_PS_ARCH_GUARD_ROOT}/src/${_glob}.cpp"
      "${_PS_ARCH_GUARD_ROOT}/src/${_glob}.h")
    foreach(_f IN LISTS _files)
      _ps_arch_strip_comments("${_f}" _scan)
      if(_scan MATCHES "#[ \t]*include[ \t]*[\"<][^\n\"<>]*${_rx}")
        message(FATAL_ERROR
          "[架构守卫/include] ${_f}\n  ${_why}。\n"
          "  命中禁止模式: ${_rx}")
      endif()
    endforeach()
  endforeach()
endfunction()

function(_ps_arch_guard_folder_ownership)
  # gui/shell 只允许壳自己的文件；业务对话框/业务控件一律归模块或 libs/widget。
  # 用文件名黑名单（这些类已迁出，残留即归属错位）。
  set(_forbidden_names
    "ImportDialog" "ReportExporter" "LuaEditor" "ProtocolEditorDialog"
    "ProtocolInspectorDialog" "TransformEditorDialog" "TransformField"
    "FilterEditorDialog" "TransformScriptDialog")
  file(GLOB_RECURSE _shell_files
    "${_PS_ARCH_GUARD_ROOT}/src/application/gui/shell/*.cpp"
    "${_PS_ARCH_GUARD_ROOT}/src/application/gui/shell/*.h")
  foreach(_f IN LISTS _shell_files)
    get_filename_component(_name "${_f}" NAME_WE)
    if(_name IN_LIST _forbidden_names)
      message(FATAL_ERROR
        "[架构守卫/归属] ${_f}\n  业务对话框/控件不得留在 gui/shell（裸壳零业务）。"
        "应归 modules/<name>/gui 或 libs/widget。")
    endif()
  endforeach()
endfunction()

# 判据 6（A2b 壳层清债硬门）：
#  1) gui 代码不得再出现槽位服务/旧槽位类型残留——读取走 ViewRegistry/MenuRegistry/StatusHub，
#     Parts 自持状态；壳不再中转槽位。
#  2) ShellBinder 重建/标题动作入口已退场：Part 在 contributes 变化时自刷新。
#  3) 旧 ribbon/<page> 串已作废，MenuId 口径是 menubar/<pageId>。
#  4) 除 MainWindow.cpp 外 shell/*.cpp 不得超 500 行（薄壳）。
function(_ps_arch_guard_a2b_residue)
  # 1) gui 壳层残留。
  file(GLOB_RECURSE _gui_files
    "${_PS_ARCH_GUARD_ROOT}/src/application/gui/*.cpp"
    "${_PS_ARCH_GUARD_ROOT}/src/application/gui/*.h")
  set(_gui_residue "ISlotsService|IPanelsService|UiSlotHub|SlotKind|UiSlot|m_slots\\b")
  foreach(_f IN LISTS _gui_files)
    _ps_arch_strip_comments("${_f}" _scan)
    if(_scan MATCHES "${_gui_residue}")
      message(FATAL_ERROR "[架构守卫/A2b残留] ${_f}\n  壳不再中转槽位；读取走 ViewRegistry/MenuRegistry/StatusHub，Parts 自持状态。\n  命中模式: ${_gui_residue}")
    endif()
  endforeach()

  # 2) 已退场的 ShellBinder 重建/标题动作入口。
  file(GLOB_RECURSE _src_files
    "${_PS_ARCH_GUARD_ROOT}/src/**.cpp"
    "${_PS_ARCH_GUARD_ROOT}/src/**.h")
  set(_retired_entries "rebuildRibbon|rebuildActivityBar|setViewTitleActionProvider|viewTitleActionCount")
  foreach(_f IN LISTS _src_files)
    _ps_arch_strip_comments("${_f}" _scan)
    if(_scan MATCHES "${_retired_entries}")
      message(FATAL_ERROR "[架构守卫/退场入口] ${_f}\n  ShellBinder 重建/标题动作入口已退场，Part 在 contributes 变化时自刷新。\n  命中模式: ${_retired_entries}")
    endif()
  endforeach()

  # 3) 旧 ribbon/<page> 字面量作废。
  foreach(_f IN LISTS _src_files)
    _ps_arch_strip_comments("${_f}" _scan)
    if(_scan MATCHES "ribbon/")
      message(FATAL_ERROR "[架构守卫/旧槽位串] ${_f}\n  旧 ribbon/<page> 串已作废，MenuId 口径是 menubar/<pageId>。")
    endif()
  endforeach()

  # 4) shell/*.cpp 行数硬门（MainWindow.cpp 豁免）。
  file(GLOB _shell_cpps "${_PS_ARCH_GUARD_ROOT}/src/application/gui/shell/*.cpp")
  foreach(_f IN LISTS _shell_cpps)
    get_filename_component(_name "${_f}" NAME)
    if(_name STREQUAL "MainWindow.cpp")
      continue()
    endif()
    file(READ "${_f}" _contents)
    string(REGEX MATCHALL "\n" _line_breaks "${_contents}")
    list(LENGTH _line_breaks _line_count)
    math(EXPR _line_count "${_line_count} + 1")
    if(_line_count GREATER 500)
      message(FATAL_ERROR "[架构守卫/薄壳行数] ${_f}: ${_line_count} 行\n  除 MainWindow.cpp 外 shell/*.cpp 不得超 500 行。")
    endif()
  endforeach()
endfunction()

function(_ps_arch_guard_retired_concepts)
  # M4 扩展模型统一：旧"内置模块"模型退役概念一旦回归源码，configure 即红。
  # 只扫 src 代码（.h/.cpp）；注释已剥离，避免历史注释误报。
  set(_retired
    "BuiltinModuleHub"
    "PS_REGISTER_MODULE"
    "ModuleContributions"
    "ModuleInfo")
  file(GLOB_RECURSE _src_files
    "${_PS_ARCH_GUARD_ROOT}/src/**.cpp"
    "${_PS_ARCH_GUARD_ROOT}/src/**.h")
  foreach(_f IN LISTS _src_files)
    _ps_arch_strip_comments("${_f}" _scan)
    foreach(_token IN LISTS _retired)
      if(_scan MATCHES "${_token}")
        message(FATAL_ERROR
          "[架构守卫/退役概念] ${_f}\n"
          "  命中已退役的旧模块模型概念: ${_token}\n"
          "  M4 起内置扩展统一走 ExtensionDescription + ExtensionService（PS_REGISTER_EXTENSION），"
          "旧 BuiltinModuleHub/ModuleInfo/ModuleContributions/PS_REGISTER_MODULE 不得回归。")
      endif()
    endforeach()
  endforeach()
endfunction()

function(ps_arch_guard_contract)
  _ps_arch_guard_includes()
  _ps_arch_guard_folder_ownership()
  _ps_arch_guard_retired_concepts()
  # 清债落地后启用（判据6）：A2b 壳层清债硬门，gui 残留清完并反证后接回。
  # _ps_arch_guard_a2b_residue()
  message(STATUS "[架构守卫] 通过：include 黑名单 + 文件夹归属 + 退役概念无违规")
endfunction()
