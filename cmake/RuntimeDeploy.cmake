# 运行期依赖部署（Windows 专用）
#
# ADS（Qt-Advanced-Docking-System）在 Windows 上是动态库：exe 在 gui/Release、
# 测试在 tests/Release，而 ads.dll 在 3rdparty/ads/Release。缺少它时 ctest 直接
# 报 0xc0000135（STATUS_DLL_NOT_FOUND），必须手工往 PATH 里塞目录才能跑。
#
# 这里在构建后把 ads.dll 拷到每个可执行目标自己的输出目录，让"构建完就能跑"
# 成立，不依赖任何环境变量。非 Windows 平台 ADS 是静态库（无 dll），函数直接空转。

function(ps_deploy_windows_runtime target)
    if(NOT WIN32)
        return()
    endif()
    if(NOT TARGET ads)
        return()
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:ads>"
                "$<TARGET_FILE_DIR:${target}>"
        COMMENT "部署 ads.dll -> $<TARGET_FILE_DIR:${target}>")
endfunction()

# ---- Qt 运行库 ----
# Qt 的 DLL 在 <前缀>/bin，exe 找到它靠"找到"之后的第二条命：运行期 PATH。
# 两件事一起做，缺一不可：
#   1. VS_DEBUGGER_ENVIRONMENT：VS 里按 F5 直接能跑，不改用户全局 PATH；
#   2. PS_DEPLOY_QT=ON 时跑 windeployqt 把 DLL/插件拷到输出目录，双击也能跑、
#      ctest 也不依赖环境变量（与 ads.dll 同一套"构建完就能跑"的约定）。
function(ps_deploy_windows_qt target)
    if(NOT WIN32)
        return()
    endif()
    set(_ps_qt_bin "")
    if(DEFINED PS_QT_DIR AND NOT "${PS_QT_DIR}" STREQUAL "")
        set(_ps_qt_bin "${PS_QT_DIR}/bin")
    endif()
    if(_ps_qt_bin AND EXISTS "${_ps_qt_bin}")
        set_property(TARGET ${target} PROPERTY VS_DEBUGGER_ENVIRONMENT "PATH=${_ps_qt_bin};%PATH%")
    endif()
    if(NOT PS_DEPLOY_QT OR NOT _ps_qt_bin)
        return()
    endif()

    find_program(_ps_windeployqt NAMES windeployqt HINTS "${_ps_qt_bin}" NO_DEFAULT_PATH)
    if(NOT _ps_windeployqt)
        find_program(_ps_windeployqt NAMES windeployqt)
    endif()
    if(NOT _ps_windeployqt)
        message(STATUS "[qt] 未找到 windeployqt，跳过 Qt DLL 部署（运行前把 ${_ps_qt_bin} 加进 PATH）")
        return()
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${_ps_windeployqt}" --no-translations --no-compiler-runtime --no-system-d3d-compiler
                "$<TARGET_FILE:${target}>"
        COMMENT "部署 Qt 运行库 -> $<TARGET_FILE_DIR:${target}>")

    # windeployqt 只拷当前平台的 qwindows.dll，测试用 QT_QPA_PLATFORM=offscreen
    # 时找不到 qoffscreen.dll：Qt 会弹"could not find or load the Qt platform plugin"
    # 的模态框，无人值守的 ctest 就永远卡在那里（不是失败，是挂着）。
    # 把 offscreen 插件一并拷过去，测试才能真正跑起来。
    set(_ps_qt_offscreen "${PS_QT_DIR}/plugins/platforms/qoffscreen.dll")
    if(EXISTS "${_ps_qt_offscreen}")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_ps_qt_offscreen}"
                    "$<TARGET_FILE_DIR:${target}>/platforms/qoffscreen.dll"
            # Qt 6 起 offscreen 会去找 <app>/lib/fonts；目录不存在就发一条
            # "Cannot find font directory" 的 qWarning，裸壳冒烟把 warning 当失败。
            # 空目录即可（无字体时用内置回退），别为此塞二进制字体文件。
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/lib/fonts"
            COMMENT "部署 offscreen 平台插件与字体目录（供 QT_QPA_PLATFORM=offscreen 的无人值守测试）")
    endif()
endfunction()

# 把当前目录里已定义的可执行目标全部挂上部署步骤，省得逐个列名。
# 必须在目标定义之后调用（通常放在 CMakeLists 末尾）。
function(ps_deploy_windows_runtime_all)
    if(NOT WIN32)
        return()
    endif()
    get_property(_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t IN LISTS _targets)
        get_target_property(_type ${_t} TYPE)
        if(_type STREQUAL "EXECUTABLE")
            ps_deploy_windows_runtime(${_t})
            ps_deploy_windows_qt(${_t})
        endif()
    endforeach()
endfunction()
