#!/usr/bin/env sh
# 平台与工具定位的唯一实现（tools/format.sh、tools/ci/local_preflight.sh 共用）。
#
# 为什么单独抽出来：
#   Windows 上 git 的 hook 跑在 Git for Windows 自带的 sh（MSYS）里，
#   但 PATH 不是 Linux 那套约定——没有 python3、没有 /usr/bin/ruff、
#   clang-format 装在 LLVM 或 VS 的目录里而不在 PATH。
#   这些"在哪个平台、工具在哪"的差异集中在这里，
#   格式规则、门禁口径仍然只有一份（format.sh / local_preflight.sh），
#   不允许出现"Windows 版规则"。
#
# 只依赖 POSIX sh；被 source，不单独执行。

ps_platform() {
    case "$(uname -s 2>/dev/null)" in
        MINGW* | MSYS* | CYGWIN*) echo windows ;;
        Linux) echo linux ;;
        Darwin) echo darwin ;;
        *) echo other ;;
    esac
}

ps_is_windows() { [ "$(ps_platform)" = windows ]; }

# 依赖根：环境变量优先，其次平台缺省。
# 与 cmake/DepsRoot.cmake 的 ps_deps_root() 同一口径：CMake 侧 `-D` 还会压过环境变量，
# shell 里看不到 `-D`，所以这里只表达"环境变量 > 缺省"。
ps_deps_root() {
    if [ -n "${PS_DEPS_ROOT:-}" ]; then
        printf '%s\n' "$PS_DEPS_ROOT"
        return 0
    fi
    if ps_is_windows; then
        if command -v cygpath >/dev/null 2>&1; then
            printf '%s\n' "$(cygpath -u "${USERPROFILE:-$HOME}")/.ps-deps"
        else
            printf '%s\n' "${USERPROFILE:-$HOME}/.ps-deps"
        fi
    else
        printf '%s\n' "${HOME:-/tmp}/.ps-deps"
    fi
}

# ps_find_file <候选绝对路径...>
# 只按路径探测（不看 PATH）：存在即可执行（MSYS 对 .exe 的 -x 判定不稳，存在即认）。
# 命中打印路径，全不命中返回 1。ps_find_tool 与"必须先看固定路径"的调用方共用这一份口径。
ps_find_file() {
    for _pff_cand in "$@"; do
        [ -n "$_pff_cand" ] || continue
        if [ -f "$_pff_cand" ] && [ -x "$_pff_cand" ]; then
            printf '%s\n' "$_pff_cand"
            unset _pff_cand
            return 0
        fi
        case "$_pff_cand" in
            *.exe | *.cmd | *.bat)
                if [ -f "$_pff_cand" ]; then
                    printf '%s\n' "$_pff_cand"
                    unset _pff_cand
                    return 0
                fi
                ;;
        esac
    done
    unset _pff_cand
    return 1
}

# ps_find_tool <命令名> [候选绝对路径...]
# 先查 PATH，再按候选路径探测；命中即打印路径，全找不到返回 1（调用方自己决定 skip 还是失败）。
ps_find_tool() {
    _pft_name="$1"
    shift
    if command -v "$_pft_name" >/dev/null 2>&1; then
        command -v "$_pft_name"
        return 0
    fi
    ps_find_file "$@"
}

# clang-format / git-clang-format：Linux 通常在 PATH，Windows 在 LLVM 或 VS 目录。
ps_clang_format() {
    _root="$(ps_deps_root)"
    ps_find_tool "${CLANG_FORMAT:-clang-format}" ||
        ps_find_tool clang-format \
            "$_root/toolchain/llvm/bin/clang-format.exe" \
            "$_root/toolchain/llvm/bin/clang-format" \
            "/c/Program Files/LLVM/bin/clang-format.exe" \
            "/c/Program Files/LLVM/bin/clang-format" ||
        _ps_vs_llvm clang-format.exe
}

ps_git_clang_format() {
    _root="$(ps_deps_root)"
    ps_find_tool "${GIT_CLANG_FORMAT:-git-clang-format}" ||
        ps_find_tool git-clang-format \
            "$_root/toolchain/llvm/bin/git-clang-format" \
            "$_root/toolchain/llvm/bin/git-clang-format.py" \
            "/c/Program Files/LLVM/bin/git-clang-format" \
            "/c/Program Files/LLVM/bin/git-clang-format.py" ||
        _ps_vs_llvm git-clang-format
}

# VS 自带的 LLVM 路径带版本号，只能遍历；只在本平台是 Windows 时有意义。
_ps_vs_llvm() {
    [ -n "${1:-}" ] || return 1
    for _f in "/c/Program Files/Microsoft Visual Studio"/*/*/VC/Tools/Llvm/x64/bin/"$1" \
        "${HOME:-/c/Users}"/*/scoop/apps/llvm/current/bin/"$1"; do
        if [ -f "$_f" ] && [ -x "$_f" ]; then
            printf '%s\n' "$_f"
            return 0
        fi
    done
    return 1
}

# ruff：PATH 里有就用；否则查受管依赖根里的 venv（Windows 在 Scripts/）。
ps_ruff() {
    _root="$(ps_deps_root)"
    if _rf_path="$(ps_find_tool ruff \
        "$_root/toolchain/python/venv/Scripts/ruff.exe" \
        "$_root/toolchain/python/venv/bin/ruff")"; then
        printf '%s\n' "$_rf_path"
        unset _root _rf_path
        return 0
    fi
    unset _root _rf_path
    return 1
}

# ShellCheck（本仓库本地那份来自 venv 里的 shellcheck-py，钉在 requirements.txt）：
# CI 用 apt 装 shellcheck，本地用 venv 二进制；与 CI 同一档，只查 error（-S error），
# 历史脚本的 style warning 不拦。
ps_shellcheck() {
    _root="$(ps_deps_root)"
    if _sc_path="$(ps_find_tool shellcheck \
        "$_root/toolchain/python/venv/Scripts/shellcheck.exe" \
        "$_root/toolchain/python/venv/bin/shellcheck")"; then
        printf '%s\n' "$_sc_path"
        unset _root _sc_path
        return 0
    fi
    unset _root _sc_path
    return 1
}

# Python 解释器：优先受管 venv（mypy / ruff / clang-format 都装在这里，preflight 的
# mypy 检查必须命中它，否则会退到系统 Python 然后静默 skip）；没有再退 PATH。
# 找不到返回 1，调用方把对应检查降级为 skip（不假装通过）。
# 临时变量收尾时 unset（本文件被 source，保持 POSIX sh，不用非标准的 local）。
ps_python() {
    _pp_root="$(ps_deps_root)"
    case "$_pp_root" in
        /*) ;;
        *) if command -v cygpath >/dev/null 2>&1; then _pp_root="$(cygpath -u "$_pp_root")"; fi ;;
    esac
    # 受管 venv 优先，且必须先于 PATH——系统 Python 在 PATH 前面会抢走解释器。
    if _pp_py="$(ps_find_file "$_pp_root/toolchain/python/venv/Scripts/python.exe" "$_pp_root/toolchain/python/venv/bin/python")"; then
        printf '%s\n' "$_pp_py"
        unset _pp_py _pp_root
        return 0
    fi
    if _pp_py="$(ps_find_tool python3)" || _pp_py="$(ps_find_tool python)" || _pp_py="$(ps_find_tool py)"; then
        printf '%s\n' "$_pp_py"
        unset _pp_py _pp_root
        return 0
    fi
    unset _pp_py _pp_root
    return 1
}

# 并行度：Linux nproc，Windows NUMBER_OF_PROCESSORS，再退 4。
ps_jobs() {
    if command -v nproc >/dev/null 2>&1; then
        nproc 2>/dev/null && return 0
    fi
    if [ -n "${NUMBER_OF_PROCESSORS:-}" ]; then
        printf '%s\n' "$NUMBER_OF_PROCESSORS"
        return 0
    fi
    if command -v getconf >/dev/null 2>&1; then
        getconf _NPROCESSORS_ONLN 2>/dev/null && return 0
    fi
    echo 4
}

# 跑仓库内的脚本：Windows 上 NTFS 丢了执行位时用 bash 兜底，
# 避免"hook 静默不跑"这种最难查的失败。
ps_run_script() {
    _prs_script="$1"
    shift
    if [ -x "$_prs_script" ]; then
        "$_prs_script" "$@"
    elif command -v bash >/dev/null 2>&1; then
        bash "$_prs_script" "$@"
    else
        echo "ps-env: $_prs_script 不可执行，且找不到 bash" >&2
        return 127
    fi
}

# 临时目录：Windows 的 Git Bash 有 /tmp，但仍允许外部指定。
ps_tmpdir() {
    printf '%s\n' "${TMPDIR:-/tmp}"
}
