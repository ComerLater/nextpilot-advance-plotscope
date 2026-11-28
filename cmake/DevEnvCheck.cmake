# 开发环境门禁（configure 期一次，幂等）
#
# 分工：判定、补装缺失组件、复查、环境变量、Qt 发现、Git hooks、ccache 实测全部在
# tools/ps-setup.py 的 `prepare` 里写一份（与 doctor 同一口径），输出一份 JSON。
# CMake 只做"落地"，四件事：
#   1. 把 JSON 的 env{} 写进本进程环境（PATH / PS_DEPS_ROOT / PS_QT_DIR /
#      CMAKE_PREFIX_PATH / CCACHE_DIR / LD_LIBRARY_PATH）；
#   2. 把 Qt 前缀固化进 PS_QT_DIR 缓存，并注入 CMAKE_PREFIX_PATH
#      （防 find_package 静默回退到系统里另一份 Qt）；
#   3. 把 ccache 结论放 _PS_CCACHE_OK / _PS_CCACHE_MSG，供 CMakeLists 的
#      PS_REQUIRE_CCACHE 硬门禁读；
#   4. 结论不 ok 就 FATAL，把 missing 与 messages 原样摆出来。
#
# 所以这里不再扫盘找工具、不读 git config、不各装各的组件，也不需要 bash/PowerShell
# 去 source env.sh / env.ps1 再解析变量——环境值由 Python 按 EnvPathLayout 算好，
# 两个平台共用同一份 Python 与同一份 CMake 代码。
#
# 自动化护栏：一次 configure 里至多跑一次 prepare，失败立即 FATAL 并展示输出尾部
# （附手动命令），绝不循环重试。为什么放在 configure：首次 cmake 一定经过这里，
# IDE（CMake Tools 等）打开工程也会自动 configure。
include_guard(GLOBAL)

# 环境根解析（PS_DEPS_ROOT 覆盖 + 按平台默认）收在 DepsRoot.cmake。
include("${CMAKE_SOURCE_DIR}/cmake/DepsRoot.cmake")

if(DEFINED ENV{CI} AND NOT "$ENV{CI}" STREQUAL "")
    message(STATUS "[dev-env] CI 环境，跳过本地开发环境检查")
    return()
endif()

# 用户可显式给的两个路径。声明成 cache 变量只为让它们在 CMakeCache 里是 PATH 类型、可被
# preset 写：取值顺序仍是 缓存变量（-D / preset）> 环境变量 > 自动发现。
set(PS_QT_DIR "" CACHE PATH "Qt 前缀（-D 优先，其次环境变量 PS_QT_DIR / QTDIR / CMAKE_PREFIX_PATH，最后自动发现）")
set(CCACHE_DIR "" CACHE PATH "ccache 缓存目录（-D 优先，其次环境变量；缺省 <PS_DEPS_ROOT>/caches/ccache）")

# 调 ps-setup.py prepare：系统里有 python 就直接跑内核；没有就走薄壳
# （tools/ps-setup.bat / .sh，自己下受管解释器再 exec 内核），小白机器也能走通。
# 结论写文件、CMake 读文件：stdout/stderr 只留给人看，日志与 JSON 不抢同一条流。
function(_ps_prepare out_json out_log)
    set(_ps_json_file "${CMAKE_BINARY_DIR}/ps-env.json")
    # 根目录按 DepsRoot.cmake 的顺序解析（-D 优先，其次环境变量，最后平台缺省），
    # 再把结果显式传给 python：python 自己只认环境变量与缺省，不读 -D，
    # 不传就会出现"CMake 用 A、python 装到 B"。
    ps_deps_root(_ps_root_declared)
    set(_args prepare --root "${_ps_root_declared}" --out "${_ps_json_file}")
    # ccache 的实编验收要用生成器最终选中的编译器，只有 CMake 知道它是谁。
    if(DEFINED CMAKE_C_COMPILER AND NOT "${CMAKE_C_COMPILER}" STREQUAL ""
       AND EXISTS "${CMAKE_C_COMPILER}")
        list(APPEND _args --cc "${CMAKE_C_COMPILER}")
    endif()
    # 缓存变量（-D / preset）优先于环境变量与自动发现：用户显式给的前缀与缓存目录要按
    # "cache" 交给 python，否则它会把自动发现的另一份写回去，等于吃掉用户的 -D。
    if(DEFINED PS_QT_DIR AND NOT "${PS_QT_DIR}" STREQUAL ""
       AND EXISTS "${PS_QT_DIR}/lib/cmake/Qt6/Qt6Config.cmake")
        list(APPEND _args --qt-dir "${PS_QT_DIR}")
    endif()
    if(DEFINED CCACHE_DIR AND NOT "${CCACHE_DIR}" STREQUAL "")
        list(APPEND _args --ccache-dir "${CCACHE_DIR}")
    endif()
    find_program(_ps_py NAMES python3 python QUIET)
    if(_ps_py)
        set(_ps_cmd "${_ps_py}" "${CMAKE_SOURCE_DIR}/tools/ps-setup.py" ${_args})
    elseif(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/tools/ps-setup.bat")
        set(_ps_cmd "${CMAKE_SOURCE_DIR}/tools/ps-setup.bat" ${_args})
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/tools/ps-setup.sh")
        find_program(_ps_bash NAMES bash sh QUIET)
        if(NOT _ps_bash)
            message(FATAL_ERROR "[dev-env] 未找到 python/bash，无法执行 tools/ps-setup.sh prepare")
        endif()
        set(_ps_cmd "${_ps_bash}" "${CMAKE_SOURCE_DIR}/tools/ps-setup.sh" ${_args})
    else()
        message(FATAL_ERROR "[dev-env] 未找到 python3，也没有 tools/ps-setup 薄壳，无法执行 ps-setup prepare")
    endif()
    file(REMOVE "${_ps_json_file}")
    execute_process(
        COMMAND ${_ps_cmd}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE _ps_out
        ERROR_VARIABLE _ps_err
        RESULT_VARIABLE _ps_rc)
    set(${out_log} "${_ps_out}${_ps_err}" PARENT_SCOPE)
    if(NOT EXISTS "${_ps_json_file}")
        message(FATAL_ERROR
            "[dev-env] ps-setup prepare 没写出结论（${_ps_json_file} 不存在，返回 ${_ps_rc}）：\n"
            "${_ps_out}${_ps_err}\n"
            "[dev-env] 手动跑一次看完整输出：python3 tools/ps-setup.py prepare")
    endif()
    file(READ "${_ps_json_file}" _ps_json)
    set(${out_json} "${_ps_json}" PARENT_SCOPE)
endfunction()

# 把 JSON 数组里的字符串逐条吐成 CMake 列表（messages / warnings / missing 共用）。
function(_ps_json_list json key out)
    string(JSON _len LENGTH "${json}" ${key})
    set(_items "")
    if(_len GREATER 0)
        math(EXPR _last "${_len} - 1")
        foreach(_i RANGE ${_last})
            string(JSON _item GET "${json}" ${key} ${_i})
            list(APPEND _items "${_item}")
        endforeach()
    endif()
    set(${out} "${_items}" PARENT_SCOPE)
endfunction()

_ps_prepare(_ps_json _ps_log)

string(JSON _ps_ok GET "${_ps_json}" ok)
string(JSON _ps_root GET "${_ps_json}" ps_deps_root)
string(JSON _ps_qt_path GET "${_ps_json}" qt path)
string(JSON _ps_qt_src GET "${_ps_json}" qt source)
string(JSON _ps_qt_ver GET "${_ps_json}" qt actual)
string(JSON _ps_qt_abi GET "${_ps_json}" qt abi)
string(JSON _ps_hook_ok GET "${_ps_json}" git_hook ok)
string(JSON _ps_cc_ok GET "${_ps_json}" ccache ok)
string(JSON _ps_cc_msg GET "${_ps_json}" ccache reason)

# ---- 1) 环境变量：JSON 说什么就落什么 ----
string(JSON _ps_env_len LENGTH "${_ps_json}" env)
if(_ps_env_len GREATER 0)
    math(EXPR _ps_env_last "${_ps_env_len} - 1")
    set(_ps_env_keys "")
    foreach(_i RANGE ${_ps_env_last})
        string(JSON _ps_env_key MEMBER "${_ps_json}" env ${_i})
        string(JSON _ps_env_val GET "${_ps_json}" env "${_ps_env_key}")
        set(ENV{${_ps_env_key}} "${_ps_env_val}")
        list(APPEND _ps_env_keys "${_ps_env_key}")
    endforeach()
endif()
message(STATUS "[dev-env] 环境根 ${_ps_root}；已落环境变量 [${_ps_env_keys}]")

_ps_json_list("${_ps_json}" messages _ps_msgs)
foreach(_msg IN LISTS _ps_msgs)
    message(STATUS "[dev-env] ${_msg}")
endforeach()
_ps_json_list("${_ps_json}" warnings _ps_warns)
foreach(_warn IN LISTS _ps_warns)
    message(WARNING "[dev-env] ${_warn}")
endforeach()

# ---- 2) 不 ok 就停在这里，提示够用即可 ----
if(NOT _ps_ok)
    _ps_json_list("${_ps_json}" missing _ps_missing)
    if(NOT _ps_hook_ok)
        set(_ps_hook_text "；Git hooks 未就位")
    endif()
    if(_ps_missing)
        set(_ps_missing_text "；缺 [${_ps_missing}]")
    endif()
    message(FATAL_ERROR
        "[dev-env] 开发环境门禁未通过${_ps_missing_text}${_ps_hook_text}\n"
        "[dev-env] ps-setup prepare 输出尾部：\n${_ps_log}\n"
        "[dev-env] 手动修复：python3 tools/ps-setup.py doctor（逐项给实测/要求/落点/修法）")
endif()

# ---- 3) Qt 前缀：固化进缓存 + 注入前缀链 ----
if(_ps_qt_path STREQUAL "")
    message(FATAL_ERROR
        "[dev-env] 未找到 Qt6（判据：含 lib/cmake/Qt6/Qt6Config.cmake 的前缀）。\n"
        "[dev-env] 已按顺序找过：CMakeCache/env（PS_QT_DIR、QTDIR、CMAKE_PREFIX_PATH）→ "
        "受管前缀 $PS_DEPS_ROOT/toolchain → 常见安装根 → 包管理器。\n"
        "[dev-env] 修法一：cmake -DPS_QT_DIR=<Qt 前缀，如 D:/program/Qt/6.8.3/msvc2022_64> ...\n"
        "[dev-env] 修法二：python3 tools/ps-setup.py install qt")
endif()
if(NOT _ps_qt_src STREQUAL "cache")
    set(PS_QT_DIR "${_ps_qt_path}" CACHE PATH "Qt 前缀（ps-setup prepare 自动发现，可用 -D 覆盖）" FORCE)
endif()
set(ENV{PS_QT_DIR} "${_ps_qt_path}")
list(FIND CMAKE_PREFIX_PATH "${_ps_qt_path}" _ps_qt_idx)
if(_ps_qt_idx LESS 0)
    list(PREPEND CMAKE_PREFIX_PATH "${_ps_qt_path}")
endif()
message(STATUS "[dev-env] Qt 前缀：${_ps_qt_path}（来源：${_ps_qt_src} ${_ps_qt_ver}，ABI：${_ps_qt_abi}）")

# ---- 4) ccache 结论交给 CMakeLists 的硬门禁（这里只搬运，不重判）----
set(_PS_CCACHE_OK "${_ps_cc_ok}")
set(_PS_CCACHE_MSG "${_ps_cc_msg}")
