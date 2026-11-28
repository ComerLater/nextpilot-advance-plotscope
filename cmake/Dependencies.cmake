include_guard(GLOBAL)

include(FetchContent)
include(GNUInstallDirs)

# ---------------- 三方库版本与源码镜像：本文件是唯一源 ----------------
#
# 版本、镜像 URL、sha256 全部写在这里。仓库不再有外部版本清单文件
# （原 deps-version.json 的 packages 段已并进来，toolchains 段归 tools/ps-setup.py）。
#
# 每行 "<url>|<sha256>"：gitee 归档 zip 与 GitHub tar.gz 内容不同、哈希不同，
# 而 CMake 的 URL_HASH 只接受单值，所以用 _ps_pick_mirror() 逐个探测下载 +
# 各自校验，选第一个通过的交给 FetchContent（选中文件缓存在 build 目录，
# 重复 configure 不再联网）。系统或 vcpkg 已有该库时完全不触发网络。

set(PS_VER_ZLIB "1.3.1")
set(PS_MIRRORS_ZLIB
    "https://gitee.com/nphelper/zlib/repository/archive/v1.3.1.zip|7c31009abc4e76ddc32e1448b6051bafe5f606aac158bb36166100a21ec170c6"
    "https://github.com/madler/zlib/archive/refs/tags/v1.3.1.tar.gz|17e88863f3600672ab49182f217281b6fc4d3c762bde361935e436a95214d05c")

set(PS_VER_ZSTD "1.5.7")
set(PS_MIRRORS_ZSTD
    "https://gitee.com/nphelper/zstd/repository/archive/v1.5.7.zip|847e3897fe0e3b42139f5c93218b5209bb9e7c513231869d7746ed8c7906900c"
    "https://github.com/facebook/zstd/archive/refs/tags/v1.5.7.tar.gz|37d7284556b20954e56e1ca85b80226768902e2edabd3b649e9e72c0c9012ee3")

set(PS_VER_LZ4 "1.10.0")
set(PS_MIRRORS_LZ4
    "https://gitee.com/nphelper/lz4/repository/archive/v1.10.0.zip|39899812e1ecbe5c126b3b360ca3e93e440493424c108c4db98d7502da0ebe43"
    "https://github.com/lz4/lz4/archive/refs/tags/v1.10.0.tar.gz|537512904744b35e232912055ccf8ec66d768639ff3abe5788d90d792ec5f48b")

set(PS_VER_NANOARROW "0.9.0")
set(PS_TAG_NANOARROW "apache-arrow-nanoarrow-0.9.0")  # 上游 tag 与版本号不同名，保持一致
set(PS_MIRRORS_NANOARROW
    "https://gitee.com/nphelper/arrow-nanoarrow/repository/archive/apache-arrow-nanoarrow-0.9.0.zip|c38c77e982f41f8be2c5bf8c6194e5239be59d135ec8d743d137c4b89171f343"
    "https://github.com/apache/arrow-nanoarrow/archive/refs/tags/apache-arrow-nanoarrow-0.9.0.tar.gz|22c3edeaa5cb384c6da091f32a4e1c937779f2d62f84d646028de658b03d4385")

# _ps_pick_mirror <pkg> <out_file_var> <out_hash_var> <out_url_var>
# 按 PS_MIRRORS_<PKG> 的顺序逐个试：缓存命中且哈希对 → 直接用；否则下载校验。
# 全失败是致命错误（此时无法构建，含糊跳过只会得到更难查的链接错误）。
function(_ps_pick_mirror pkg out_file out_hash out_url)
    string(TOUPPER "${pkg}" _ps_pkg_upper)
    set(_ps_mirrors "${PS_MIRRORS_${_ps_pkg_upper}}")
    if(NOT _ps_mirrors)
        message(FATAL_ERROR "${pkg}: 未配置源码镜像（PS_MIRRORS_${_ps_pkg_upper}）")
    endif()

    set(_cache_dir "${CMAKE_BINARY_DIR}/ps-deps-download-cache")
    file(MAKE_DIRECTORY "${_cache_dir}")

    set(_idx 0)
    foreach(_entry IN LISTS _ps_mirrors)
        string(REPLACE "|" ";" _parts "${_entry}")
        list(LENGTH _parts _n)
        if(NOT _n EQUAL 2)
            message(FATAL_ERROR "${pkg}: 镜像条目格式应为 <url>|<sha256>，实际：${_entry}")
        endif()
        list(GET _parts 0 _u)
        list(GET _parts 1 _h)

        # 保留归档扩展名：ExternalProject 靠文件名后缀判断如何解压（.zip / .tar.gz）
        set(_ext "")
        if(_u MATCHES "\\.(tar\\.gz|tgz|tar\\.xz|tar\\.bz2|zip|tar)(\\?|$)")
            set(_ext "${CMAKE_MATCH_1}")
        endif()
        if(_ext)
            set(_dst "${_cache_dir}/${pkg}-${_idx}.${_ext}")
        else()
            set(_dst "${_cache_dir}/${pkg}-${_idx}")
        endif()

        set(_ok FALSE)
        if(EXISTS "${_dst}")
            file(SHA256 "${_dst}" _actual)
            if(_actual STREQUAL _h)
                set(_ok TRUE)
            endif()
        endif()
        if(NOT _ok)
            file(DOWNLOAD "${_u}" "${_dst}" STATUS _st TLS_VERIFY ON)
            list(GET _st 0 _code)
            if(NOT _code EQUAL 0)
                message(STATUS "[deps] ${pkg} 镜像 ${_idx} 不可达，试下一个（${_u}）")
                math(EXPR _idx "${_idx} + 1")
                continue()
            endif()
            file(SHA256 "${_dst}" _actual)
            if(NOT _actual STREQUAL _h)
                message(STATUS "[deps] ${pkg} 镜像 ${_idx} 哈希不符，试下一个（${_u}）")
                math(EXPR _idx "${_idx} + 1")
                continue()
            endif()
        endif()

        set(${out_file} "${_dst}" PARENT_SCOPE)
        set(${out_hash} "${_h}" PARENT_SCOPE)
        set(${out_url} "${_u}" PARENT_SCOPE)
        return()
    endforeach()
    message(FATAL_ERROR "${pkg}: 所有源码镜像都不可达或哈希不匹配（见 cmake/Dependencies.cmake 的 PS_MIRRORS_${_ps_pkg_upper}）")
endfunction()

option(PS_DEPS_FETCH "缺少 zlib/sqlite3/zstd/lz4/nanoarrow 时自动拉源码构建" ON)
option(PS_DEPS_PREFER_SYSTEM "优先使用系统或 vcpkg 已安装的库" ON)
option(PS_DEPS_INSTALL "为源码构建的依赖生成 install 规则（component=ps_deps）" OFF)
set(PS_DEPS_ROOT "" CACHE PATH "已安装 zlib/sqlite3/zstd/lz4 的前缀，含 include 和 lib")
# 注意重名：工具链根也叫 PS_DEPS_ROOT（见 cmake/DepsRoot.cmake），但那是环境根
# （toolchain/ + env.*），和这里的 libs 前缀不是同一件事。命令行优先级最高：
# `-DPS_DEPS_ROOT=<path>` 同时决定工具链根与 libs 前缀（两者同值）；只想让工具链根换
# 地方、libs 用系统/vcpkg 时，用环境变量 PS_DEPS_ROOT 并显式 -DPS_DEPS_ROOT="" 。

if(PS_DEPS_ROOT)
    list(APPEND CMAKE_PREFIX_PATH "${PS_DEPS_ROOT}")
endif()
if(DEFINED ENV{VCPKG_ROOT})
    set(_ps_vcpkg_triplet "$ENV{VCPKG_DEFAULT_TRIPLET}")
    if(NOT _ps_vcpkg_triplet)
        if(WIN32)
            set(_ps_vcpkg_triplet "x64-windows")
        else()
            set(_ps_vcpkg_triplet "x64-linux")
        endif()
    endif()
    list(APPEND CMAKE_PREFIX_PATH "$ENV{VCPKG_ROOT}/installed/${_ps_vcpkg_triplet}")
endif()

function(ps_dep_missing name packages)
    if(NOT PS_DEPS_FETCH)
        message(FATAL_ERROR
            "未找到 ${name}。请安装后重新配置，或保持 PS_DEPS_FETCH=ON 自动拉源码。\n"
            "vcpkg 方式：vcpkg install ${packages} --triplet x64-windows\n"
            "然后把 -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake 传给 CMake。")
    endif()
endfunction()

function(ps_make_interface_target name)
    if(NOT TARGET ${name})
        add_library(${name} INTERFACE)
    endif()
endfunction()

function(ps_resolve_zlib)
    if(PS_DEPS_ROOT)
        find_path(PS_ZLIB_INCLUDE_DIR NAMES zlib.h PATHS "${PS_DEPS_ROOT}/include" NO_DEFAULT_PATH)
        find_library(PS_ZLIB_LIBRARY NAMES zd z zlib zlibstatic zdll
            PATHS "${PS_DEPS_ROOT}/lib" NO_DEFAULT_PATH)
        if(PS_ZLIB_INCLUDE_DIR AND PS_ZLIB_LIBRARY)
            ps_make_interface_target(ps_zlib)
            target_include_directories(ps_zlib INTERFACE ${PS_ZLIB_INCLUDE_DIR})
            target_link_libraries(ps_zlib INTERFACE ${PS_ZLIB_LIBRARY})
            message(STATUS "PlotScope deps: zlib = ${PS_DEPS_ROOT}")
            return()
        endif()
    endif()

    if(PS_DEPS_PREFER_SYSTEM)
        find_package(ZLIB QUIET)
        if(ZLIB_FOUND)
            ps_make_interface_target(ps_zlib)
            target_link_libraries(ps_zlib INTERFACE ZLIB::ZLIB)
            message(STATUS "PlotScope deps: zlib = system")
            return()
        endif()

        find_path(PS_ZLIB_INCLUDE_DIR NAMES zlib.h)
        find_library(PS_ZLIB_LIBRARY NAMES z zlib zlibstatic zdll)
        if(PS_ZLIB_INCLUDE_DIR AND PS_ZLIB_LIBRARY)
            ps_make_interface_target(ps_zlib)
            target_include_directories(ps_zlib INTERFACE ${PS_ZLIB_INCLUDE_DIR})
            target_link_libraries(ps_zlib INTERFACE ${PS_ZLIB_LIBRARY})
            message(STATUS "PlotScope deps: zlib = ${PS_ZLIB_LIBRARY}")
            return()
        endif()
    endif()

    ps_dep_missing("zlib" "zlib")
    _ps_pick_mirror(zlib _zf _zh _zu)
    message(STATUS "PlotScope deps: zlib = FetchContent（镜像 ${_zu}）")
    FetchContent_Declare(zlib
        URL "${_zf}"
        URL_HASH SHA256=${_zh}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(zlib)

    ps_make_interface_target(ps_zlib)
    if(TARGET zlibstatic)
        set(_ps_zlib_target zlibstatic)
    elseif(TARGET zlib)
        set(_ps_zlib_target zlib)
    else()
        message(FATAL_ERROR "zlib FetchContent 完成，但没有生成 zlibstatic/zlib 目标")
    endif()
    target_link_libraries(ps_zlib INTERFACE ${_ps_zlib_target})

    if(PS_DEPS_INSTALL)
        install(FILES "$<TARGET_FILE:${_ps_zlib_target}>"
            DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT ps_deps)
        install(FILES ${zlib_SOURCE_DIR}/zlib.h ${zlib_BINARY_DIR}/zconf.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            COMPONENT ps_deps)
    endif()
endfunction()

function(ps_resolve_zstd)
    if(PS_DEPS_ROOT)
        find_path(PS_ZSTD_INCLUDE_DIR NAMES zstd.h PATHS "${PS_DEPS_ROOT}/include" NO_DEFAULT_PATH)
        find_library(PS_ZSTD_LIBRARY NAMES zstdd zstd libzstd zstd_static
            PATHS "${PS_DEPS_ROOT}/lib" NO_DEFAULT_PATH)
        if(PS_ZSTD_INCLUDE_DIR AND PS_ZSTD_LIBRARY)
            ps_make_interface_target(ps_zstd)
            target_include_directories(ps_zstd INTERFACE ${PS_ZSTD_INCLUDE_DIR})
            target_link_libraries(ps_zstd INTERFACE ${PS_ZSTD_LIBRARY})
            message(STATUS "PlotScope deps: zstd = ${PS_DEPS_ROOT}")
            return()
        endif()
    endif()

    if(PS_DEPS_PREFER_SYSTEM)
        find_path(PS_ZSTD_INCLUDE_DIR NAMES zstd.h)
        find_library(PS_ZSTD_LIBRARY NAMES zstd libzstd zstd_static)
        if(PS_ZSTD_INCLUDE_DIR AND PS_ZSTD_LIBRARY)
            ps_make_interface_target(ps_zstd)
            target_include_directories(ps_zstd INTERFACE ${PS_ZSTD_INCLUDE_DIR})
            target_link_libraries(ps_zstd INTERFACE ${PS_ZSTD_LIBRARY})
            message(STATUS "PlotScope deps: zstd = ${PS_ZSTD_LIBRARY}")
            return()
        endif()
    endif()

    ps_dep_missing("zstd" "zstd")
    _ps_pick_mirror(zstd _sf _sh _su)
    message(STATUS "PlotScope deps: zstd = FetchContent（镜像 ${_su}）")
    FetchContent_Declare(zstd
        URL "${_sf}"
        URL_HASH SHA256=${_sh}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_GetProperties(zstd)
    if(NOT zstd_POPULATED)
        FetchContent_Populate(zstd)
    endif()

    if(EXISTS ${zstd_SOURCE_DIR}/build/cmake/CMakeLists.txt)
        set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
        set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
        set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
        set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
        add_subdirectory(${zstd_SOURCE_DIR}/build/cmake ${zstd_BINARY_DIR})
        ps_make_interface_target(ps_zstd)
        if(TARGET libzstd_static)
            set(_ps_zstd_target libzstd_static)
        elseif(TARGET zstd_static)
            set(_ps_zstd_target zstd_static)
        else()
            message(FATAL_ERROR "zstd CMake 完成，但没有生成 libzstd_static/zstd_static 目标")
        endif()
        set(_ps_zstd_include ${zstd_SOURCE_DIR}/lib)
    elseif(EXISTS ${zstd_SOURCE_DIR}/zstd.h)
        # 部分国内镜像把 zstd C 源码平铺在根目录，直接编译静态库。
        file(GLOB ZSTD_C_SOURCES CONFIGURE_DEPENDS ${zstd_SOURCE_DIR}/*.c)
        list(FILTER ZSTD_C_SOURCES EXCLUDE REGEX ".*/(cover|fastcover|zdict|divsufsort)\\.c$")
        add_library(ps_zstd STATIC ${ZSTD_C_SOURCES})
        target_include_directories(ps_zstd PUBLIC ${zstd_SOURCE_DIR})
        target_compile_definitions(ps_zstd PRIVATE ZSTD_DISABLE_ASM)
        set_target_properties(ps_zstd PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            OUTPUT_NAME zstd)
        set(_ps_zstd_target ps_zstd)
        set(_ps_zstd_include ${zstd_SOURCE_DIR})
    else()
        message(FATAL_ERROR "zstd 源码中没有 build/cmake，也没有根目录 zstd.h")
    endif()

    target_link_libraries(ps_zstd INTERFACE ${_ps_zstd_target})
    if(PS_DEPS_INSTALL)
        install(FILES "$<TARGET_FILE:${_ps_zstd_target}>"
            DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT ps_deps)
        install(FILES
            ${_ps_zstd_include}/zstd.h
            ${_ps_zstd_include}/zstd_errors.h
            ${_ps_zstd_include}/zdict.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            COMPONENT ps_deps)
    endif()
endfunction()

function(ps_resolve_lz4)
    if(PS_DEPS_ROOT)
        find_path(PS_LZ4_INCLUDE_DIR NAMES lz4.h PATHS "${PS_DEPS_ROOT}/include" NO_DEFAULT_PATH)
        find_library(PS_LZ4_LIBRARY NAMES lz4d lz4 lz4_static
            PATHS "${PS_DEPS_ROOT}/lib" NO_DEFAULT_PATH)
        if(PS_LZ4_INCLUDE_DIR AND PS_LZ4_LIBRARY)
            ps_make_interface_target(ps_lz4)
            target_include_directories(ps_lz4 INTERFACE ${PS_LZ4_INCLUDE_DIR})
            target_link_libraries(ps_lz4 INTERFACE ${PS_LZ4_LIBRARY})
            message(STATUS "PlotScope deps: lz4 = ${PS_DEPS_ROOT}")
            return()
        endif()
    endif()

    if(PS_DEPS_PREFER_SYSTEM)
        find_path(PS_LZ4_INCLUDE_DIR NAMES lz4.h)
        find_library(PS_LZ4_LIBRARY NAMES lz4 lz4_static)
        if(PS_LZ4_INCLUDE_DIR AND PS_LZ4_LIBRARY)
            ps_make_interface_target(ps_lz4)
            target_include_directories(ps_lz4 INTERFACE ${PS_LZ4_INCLUDE_DIR})
            target_link_libraries(ps_lz4 INTERFACE ${PS_LZ4_LIBRARY})
            message(STATUS "PlotScope deps: lz4 = ${PS_LZ4_LIBRARY}")
            return()
        endif()
    endif()

    ps_dep_missing("lz4" "lz4")
    _ps_pick_mirror(lz4 _lf _lh _lu)
    message(STATUS "PlotScope deps: lz4 = FetchContent（镜像 ${_lu}）")
    FetchContent_Declare(lz4
        URL "${_lf}"
        URL_HASH SHA256=${_lh}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_GetProperties(lz4)
    if(NOT lz4_POPULATED)
        FetchContent_Populate(lz4)
    endif()

    if(EXISTS ${lz4_SOURCE_DIR}/build/cmake/CMakeLists.txt)
        set(LZ4_BUILD_CLI OFF CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
        add_subdirectory(${lz4_SOURCE_DIR}/build/cmake ${lz4_BINARY_DIR})
        ps_make_interface_target(ps_lz4)
        if(TARGET lz4_static)
            set(_ps_lz4_target lz4_static)
        elseif(TARGET lz4)
            set(_ps_lz4_target lz4)
        else()
            message(FATAL_ERROR "lz4 CMake 完成，但没有生成 lz4_static/lz4 目标")
        endif()
        set(_ps_lz4_include ${lz4_SOURCE_DIR}/lib)
    elseif(EXISTS ${lz4_SOURCE_DIR}/lib/lz4.c)
        # OpenHarmony 镜像不带 CMake，只编译块解压需要的 lz4.c。
        add_library(ps_lz4 STATIC ${lz4_SOURCE_DIR}/lib/lz4.c)
        target_include_directories(ps_lz4 PUBLIC ${lz4_SOURCE_DIR}/lib)
        set_target_properties(ps_lz4 PROPERTIES POSITION_INDEPENDENT_CODE ON)
        set(_ps_lz4_target ps_lz4)
        set(_ps_lz4_include ${lz4_SOURCE_DIR}/lib)
    else()
        message(FATAL_ERROR "lz4 源码中没有 build/cmake，也没有 lib/lz4.c")
    endif()

    target_link_libraries(ps_lz4 INTERFACE ${_ps_lz4_target})
    if(PS_DEPS_INSTALL)
        install(FILES "$<TARGET_FILE:${_ps_lz4_target}>"
            DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT ps_deps)
        install(FILES
            ${_ps_lz4_include}/lz4.h
            ${_ps_lz4_include}/lz4hc.h
            ${_ps_lz4_include}/lz4frame.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            COMPONENT ps_deps)
    endif()
endfunction()

# === sqlite3：rosbag2 读 .db3 需要；系统/vcpkg 优先，缺失时拉官方 amalgamation 编静态库 ===
set(PS_DEPS_SQLITE3_URL "https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip"
    CACHE STRING "sqlite3 amalgamation 源码包")
function(ps_resolve_sqlite3)
    if(PS_DEPS_ROOT)
        find_path(PS_SQLITE3_INCLUDE_DIR NAMES sqlite3.h PATHS "${PS_DEPS_ROOT}/include" NO_DEFAULT_PATH)
        find_library(PS_SQLITE3_LIBRARY NAMES sqlite3 sqlite3_static sqlite
            PATHS "${PS_DEPS_ROOT}/lib" NO_DEFAULT_PATH)
        if(PS_SQLITE3_INCLUDE_DIR AND PS_SQLITE3_LIBRARY)
            ps_make_interface_target(ps_sqlite3)
            target_include_directories(ps_sqlite3 INTERFACE ${PS_SQLITE3_INCLUDE_DIR})
            target_link_libraries(ps_sqlite3 INTERFACE ${PS_SQLITE3_LIBRARY})
            message(STATUS "PlotScope deps: sqlite3 = ${PS_DEPS_ROOT}")
            return()
        endif()
    endif()

    if(PS_DEPS_PREFER_SYSTEM)
        find_package(SQLite3 QUIET)
        if(SQLite3_FOUND)
            ps_make_interface_target(ps_sqlite3)
            target_link_libraries(ps_sqlite3 INTERFACE SQLite::SQLite3)
            message(STATUS "PlotScope deps: sqlite3 = system")
            return()
        endif()
    endif()

    ps_dep_missing("sqlite3" "sqlite3")
    message(STATUS "PlotScope deps: sqlite3 = FetchContent amalgamation")
    FetchContent_Declare(sqlite3
        URL ${PS_DEPS_SQLITE3_URL}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_GetProperties(sqlite3)
    if(NOT sqlite3_POPULATED)
        FetchContent_Populate(sqlite3)
    endif()

    if(NOT EXISTS ${sqlite3_SOURCE_DIR}/sqlite3.c)
        message(FATAL_ERROR "sqlite3 源码包里没有 sqlite3.c：${sqlite3_SOURCE_DIR}")
    endif()
    add_library(ps_sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
    target_include_directories(ps_sqlite3 PUBLIC ${sqlite3_SOURCE_DIR})
    # 默认单线程库够用；rosbag2 只在解析线程内串行读库。
    target_compile_definitions(ps_sqlite3 PUBLIC SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION=1)
    set_target_properties(ps_sqlite3 PROPERTIES POSITION_INDEPENDENT_CODE ON OUTPUT_NAME sqlite3)

    # 与 zlib 一致：由本工程编出的静态库才能保证 MSVC 运行库与顶层目标同为 /MD。
    if(PS_DEPS_INSTALL)
        install(FILES "$<TARGET_FILE:ps_sqlite3>"
            DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT ps_deps)
        install(FILES ${sqlite3_SOURCE_DIR}/sqlite3.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            COMPONENT ps_deps)
    endif()
endfunction()

function(ps_resolve_nanoarrow)
    if(PS_DEPS_PREFER_SYSTEM)
        find_package(nanoarrow ${PS_VER_NANOARROW} CONFIG QUIET)
        if(nanoarrow_FOUND)
            ps_make_interface_target(ps_nanoarrow)
            target_link_libraries(ps_nanoarrow INTERFACE nanoarrow::nanoarrow)
            if(TARGET nanoarrow::nanoarrow_ipc)
                target_link_libraries(ps_nanoarrow INTERFACE nanoarrow::nanoarrow_ipc)
            endif()
            message(STATUS "PlotScope deps: nanoarrow = system")
            return()
        endif()
    endif()

    ps_dep_missing("nanoarrow" "nanoarrow")
    _ps_pick_mirror(nanoarrow _nf _nh _nu)
    message(STATUS "PlotScope deps: nanoarrow = FetchContent ${PS_VER_NANOARROW} (含 IPC, 镜像 ${_nu})")
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    set(NANOARROW_IPC ON CACHE BOOL "" FORCE)
    set(NANOARROW_IPC_WITH_ZSTD OFF CACHE BOOL "" FORCE)
    set(NANOARROW_IPC_WITH_LZ4 OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(nanoarrow
        URL "${_nf}"
        URL_HASH SHA256=${_nh}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(nanoarrow)
    # 上游同时定义 shared 与测试目标；只保留静态库被本工程链接，其余排除在 all 之外。
    if(nanoarrow_SOURCE_DIR)
        set_property(DIRECTORY "${nanoarrow_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL ON)
    endif()
    ps_make_interface_target(ps_nanoarrow)
    target_link_libraries(ps_nanoarrow INTERFACE nanoarrow_static)
    if(TARGET nanoarrow_ipc_static)
        target_link_libraries(ps_nanoarrow INTERFACE nanoarrow_ipc_static)
    endif()
endfunction()

# 只有仓库当前真正使用的依赖才在这里解析。
# zstd / lz4 保留函数体不调用：MDF 只用到 zlib，rosbag2 用 sqlite3；
# 将来某个格式真的用上 zstd/lz4，再在这里加一行调用（不要在没人用时白白拉源码）。
ps_resolve_zlib()
ps_resolve_sqlite3()
ps_resolve_nanoarrow()
