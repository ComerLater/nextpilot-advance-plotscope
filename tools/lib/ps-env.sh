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
# 与 cmake/DepsRoot.cmake 的 ps_deps_root() 同一条优先级（env > -D > 缺省）。
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

# ps_find_tool <命令名> [候选绝对路径...]
# 命中即打印路径；全找不到返回 1（调用方自己决定 skip 还是失败）。
ps_find_tool() {
    _pft_name="$1"
    shift
    if command -v "$_pft_name" >/dev/null 2>&1; then
        command -v "$_pft_name"
        return 0
    fi
    for _pft_cand in "$@"; do
        [ -n "$_pft_cand" ] || continue
        if [ -f "$_pft_cand" ] && [ -x "$_pft_cand" ]; then
            printf '%s\n' "$_pft_cand"
            return 0
        fi
        # MSYS 对 .exe 的 -x 判定不稳，存在即认（能执行由调用方验证）。
        case "$_pft_cand" in
            *.exe | *.cmd | *.bat)
                if [ -f "$_pft_cand" ]; then
                    printf '%s\n' "$_pft_cand"
                    return 0
                fi
                ;;
        esac
    done
    return 1
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
    ps_find_tool ruff ||
        ps_find_tool ruff \
            "$_root/toolchain/python/venv/Scripts/ruff.exe" \
            "$_root/toolchain/python/venv/bin/ruff"
}

# Python 解释器：Linux 习惯 python3，Windows 只有 python / py。
# 找不到返回 1，调用方把对应检查降级为 skip（不假装通过）。
ps_python() {
    ps_find_tool python3 ||
        ps_find_tool python ||
        ps_find_tool py ||
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
