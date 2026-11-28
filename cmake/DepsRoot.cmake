# 开发环境根目录的唯一解析点。
#
# 约定（优先级从高到低，命令行优先）：
#   1. -DPS_DEPS_ROOT（同名 cache 变量；CMakePresets 的 cacheVariables 也只是默认 -D）
#   2. 环境变量 PS_DEPS_ROOT
#   3. 平台缺省：Windows %USERPROFILE%\.ps-deps（POSIX 的 HOME 在 Windows 上通常为空，
#      照搬会拼出 "/.ps-deps"），其他平台 $HOME/.ps-deps
#
# 注意分工：PS_DEPS_ROOT 只表示"环境根"（toolchain/ + bin/ + caches/ + env.*），
# 预编译第三方库的前缀是另一个变量 PS_LIBS_PREFIX（见 cmake/Dependencies.cmake）。
# 两者曾经同名，导致 `-DPS_DEPS_ROOT` 顺带改了库搜索路径；已拆开，互不影响。
#
# 只有这一处写默认值：DevEnvCheck、CMakeLists 的 ccache 提示、tools/ps-setup.py
# 全部按同一个变量与同一套顺序解析（python 侧由门禁用 --root 传进来），
# 避免各写一份默认路径导致"装到 A、找不到 B"。
include_guard(GLOBAL)

function(ps_deps_root out)
    if(DEFINED PS_DEPS_ROOT AND NOT "${PS_DEPS_ROOT}" STREQUAL "")
        set(_ps_root_val "${PS_DEPS_ROOT}")
    elseif(DEFINED ENV{PS_DEPS_ROOT} AND NOT "$ENV{PS_DEPS_ROOT}" STREQUAL "")
        set(_ps_root_val "$ENV{PS_DEPS_ROOT}")
    elseif(WIN32)
        if(DEFINED ENV{USERPROFILE} AND NOT "$ENV{USERPROFILE}" STREQUAL "")
            set(_ps_root_val "$ENV{USERPROFILE}/.ps-deps")
        elseif(DEFINED ENV{LOCALAPPDATA} AND NOT "$ENV{LOCALAPPDATA}" STREQUAL "")
            set(_ps_root_val "$ENV{LOCALAPPDATA}/ps-deps")
        else()
            message(FATAL_ERROR "[dev-env] 无法确定环境根：PS_DEPS_ROOT 未设置且 USERPROFILE/LOCALAPPDATA 都为空")
        endif()
    else()
        set(_ps_root_val "$ENV{HOME}/.ps-deps")
    endif()
    # 统一成正斜杠：CMake 的 file()/EXISTS 在 Windows 上正反斜杠都能用，
    # 但混在一句话里打日志很难读，且 ENV{PATH} 拼接需要一致的分隔。
    file(TO_CMAKE_PATH "${_ps_root_val}" _ps_root_val)
    set(${out} "${_ps_root_val}" PARENT_SCOPE)
endfunction()
