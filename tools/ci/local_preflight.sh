#!/usr/bin/env bash
# 推送前门禁：本地复现 CI 的「改动行增量」判定，基准统一为 origin/main。
#
# 用法：
#   tools/ci/local_preflight.sh               # quick（默认）：格式 / shell / workflow / mypy / 重排告警
#   tools/ci/local_preflight.sh --full        # 追加 构建 + ctest + tidy 增量 + arch_guard --quick
#   tools/ci/local_preflight.sh --base <ref>  # 换基准，默认 origin/main
#
# 口径：
#   - 只查本次改动（<base>...HEAD），与 CI 的 changed-lines 判定一致；
#   - 工具缺失打印 [skip] 并继续，不假装通过；退出码只由真实失败决定；
#   - 重排告警可用 ALLOW_REFORMAT=1 放行（仅限 owner 批准的独立 style 提交）。
#
# 权威说明见 docs/develop/push-gate.md。

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT" || exit 1

# Windows 上 hook 跑在 Git for Windows 的 sh 里：没有 python3、没有 nproc、
# 生成器也不是 Ninja（MSVC 需要 VS 生成器）。平台差异统一从 ps-env.sh 取。
. "$ROOT/tools/lib/ps-env.sh"

PYTHON="$(ps_python || true)"
JOBS="$(ps_jobs)"
TMP="$(ps_tmpdir)"

MODE=quick
BASE=origin/main
while [ $# -gt 0 ]; do
    case "$1" in
        --quick) MODE=quick; shift ;;
        --full) MODE=full; shift ;;
        --base) shift; BASE="${1:-origin/main}"; shift ;;
        -h|--help) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "未知参数：$1（见 --help）"; exit 2 ;;
    esac
done

FAIL=0
SKIPPED=0
bold() { printf '\n\033[1m== %s ==\033[0m\n' "$1"; }
ok() { printf '  [ok] %s\n' "$1"; }
skip() { printf '  [skip] %s\n' "$1"; SKIPPED=$((SKIPPED + 1)); }
bad() { printf '  [FAIL] %s\n' "$1"; FAIL=$((FAIL + 1)); }
have() { command -v "$1" >/dev/null 2>&1; }
changed() { git diff --name-only --diff-filter=ACMR "$BASE"...HEAD -- "$@" 2>/dev/null; }

if ! git rev-parse --verify --quiet "$BASE" >/dev/null; then
    echo "基准 $BASE 不存在；先 git fetch origin（退化为 HEAD~1）"
    BASE=HEAD~1
fi
BASE_SHA="$(git rev-parse --short "$BASE")"

bold "0) 基准"
echo "  base = $BASE ($BASE_SHA)，head = $(git rev-parse --short HEAD)"
if git merge-base --is-ancestor "$BASE" HEAD; then
    ok "已包含基准"
else
    bad "未 rebase 到 $BASE：先 git fetch origin && git rebase $BASE"
fi

bold "1) 格式（C++ / Markdown / Python 的改动行）"
if [ ! -f tools/format.sh ]; then
    skip "tools/format.sh 不存在"
else
    CPP_FILES="$(changed '*.cpp' '*.h' '*.hpp' '*.cc')"
    MD_FILES="$(changed '*.md')"
    PY_FILES="$(changed '*.py')"
    FMT="ps_run_script tools/format.sh"
    if [ -n "$CPP_FILES" ]; then
        if ps_clang_format >/dev/null 2>&1 && ps_git_clang_format >/dev/null 2>&1; then
            $FMT --check "$BASE" --only cpp && ok "C++ 改动行格式" || bad "C++ 改动行格式（跑 tools/format.sh --staged 修）"
        else
            skip "clang-format 缺失，C++ 格式未查"
        fi
    fi
    if [ -n "$MD_FILES" ]; then
        $FMT --check "$BASE" --only md && ok "Markdown 改动行格式" || bad "Markdown 改动行格式"
        if have npx; then
            npx --yes markdownlint-cli2@0.19.1 $MD_FILES >/dev/null 2>&1 && ok "markdownlint" || bad "markdownlint（看具体文件行）"
        else
            skip "node/npx 缺失，markdownlint 未查"
        fi
    fi
    if [ -n "$PY_FILES" ]; then
        $FMT --check "$BASE" --only py && ok "Python 改动行格式" || bad "Python 改动行格式"
        if [ -n "$PYTHON" ] && "$PYTHON" -c 'import mypy' >/dev/null 2>&1; then
            "$PYTHON" -m mypy --config-file mypy.ini $PY_FILES && ok "mypy" || bad "mypy（无存量豁免）"
        else
            skip "mypy 未安装，Python 类型未查"
        fi
    fi
    if [ -z "$CPP_FILES$MD_FILES$PY_FILES" ]; then
        ok "无 C++/Markdown/Python 改动"
    fi
fi

bold "2) 脚本与 workflow"
SH_FILES="$(changed '*.sh')"
if [ -n "$SH_FILES" ]; then
    for f in $SH_FILES; do bash -n "$f" || bad "bash -n $f"; done
    ok "bash -n"
    if SC="$(ps_shellcheck)"; then
        "$SC" -S error $SH_FILES && ok "shellcheck (error)" || bad "shellcheck (error)"
    else
        skip "shellcheck 未安装（python tools/ps-setup.py install python 装 shellcheck-py）"
    fi
else
    ok "无 shell 改动"
fi
WF_FILES="$(changed '.github/workflows/*')"
if [ -n "$WF_FILES" ]; then
    if have actionlint; then
        actionlint .github/workflows/*.yml && ok "actionlint" || bad "actionlint"
    else
        skip "actionlint 未安装"
    fi
    if [ -n "$PYTHON" ]; then
        "$PYTHON" tools/check-workflow-outputs.py .github/workflows && ok "workflow 输出契约" || bad "workflow 输出契约"
    else
        skip "python 缺失，workflow 输出契约未查"
    fi
else
    ok "无 workflow 改动"
fi
# ps-setup 的纯函数单测：不碰网络与用户环境，秒级，改了内核就跑。
if [ -n "$PYTHON" ] && git diff --name-only "$BASE"...HEAD | grep -qE 'tools/(ps-setup\.py|tests/)'; then
    "$PYTHON" -m unittest discover -s tools/tests -p 'test_*.py' >"$TMP/preflight-unit.log" 2>&1 \
        && ok "ps-setup 单测" || bad "ps-setup 单测红（看 $TMP/preflight-unit.log）"
fi
# 薄壳（ps-setup.sh/.ps1）是产物却入库，改了清单/脚本就必须与之一致，否则手改会长期漂移。
if [ -n "$PYTHON" ] && git diff --name-only "$BASE"...HEAD | grep -qe 'tools/ps-setup.py' -e 'toolchain.json'; then
    "$PYTHON" tools/ps-setup.py gen-shell --check >"$TMP/preflight-shell.log" 2>&1 \
        && ok "薄壳与清单一致" || bad "薄壳漂移（跑 python tools/ps-setup.py gen-shell 后重提交）"
fi

bold "3) 整文件重排告警"
if [ "${ALLOW_REFORMAT:-0}" = "1" ]; then
    skip "ALLOW_REFORMAT=1，跳过"
else
    REORDER=0
    for f in $(changed '*.cpp' '*.h' '*.hpp'); do
        A="$(git diff --numstat "$BASE"...HEAD -- "$f" | awk '{print $1+0}')"
        W="$(git diff -w --numstat "$BASE"...HEAD -- "$f" | awk '{print $1+0}')"
        if [ "${A:-0}" -gt 200 ] && [ "${A:-0}" -gt $((3 * (${W:-0} + 1))) ]; then
            echo "  [FAIL] $f：改动行 $A vs 忽略空白 $W —— 疑似整文件重排"
            REORDER=$((REORDER + 1))
        fi
    done
    if [ "$REORDER" -eq 0 ]; then ok "无整文件重排"; else bad "重排 $REORDER 个文件：拆出独立 style 提交或回退格式改动"; fi
fi

if [ "$MODE" = "full" ]; then
    bold "4) 构建 + ctest（Release 与 Debug 都要）"
    # Linux 的 env.sh 由 ps-setup 生成；Windows 只有 env.ps1，配置里已 import，
    # 这里只兜底：找不到就不 source（Qt 走 PS_QT_DIR / CMAKE_PREFIX_PATH）。
    . "${PS_DEPS_ENV:-$(ps_deps_root)/env.sh}" 2>/dev/null || true
    export RUSTUP_TOOLCHAIN="${RUSTUP_TOOLCHAIN:-stable}" CARGO_NET_OFFLINE="${CARGO_NET_OFFLINE:-true}"

    # 生成器分档：Linux 一律 Ninja 单档；Windows 只有在 MSVC 环境（cl 在 PATH）里
    # 才能用 Ninja，否则退回 VS 多档生成器（否则 configure 直接找不到编译器）。
    NINJA_MODE=1
    if ps_is_windows && ! have cl; then
        NINJA_MODE=0
    fi

    # Debug 不清 NDEBUG，assert 与 pure virtual 之类的崩溃只在这一档现形；只跑 Release 会漏。
    for CFG in Release Debug; do
        DIR="build-preflight-$CFG"
        if [ "$NINJA_MODE" -eq 1 ]; then
            CMAKE_ARGS=(-G Ninja "-DCMAKE_BUILD_TYPE=$CFG")
            BUILD_ARGS=(--build "$DIR" -j "$JOBS")
            CTEST_ARGS=(--test-dir "$DIR" -j "$JOBS")
        else
            CMAKE_ARGS=(-G "Visual Studio 17 2022" -A x64)
            BUILD_ARGS=(--build "$DIR" --config "$CFG" -- "-j$JOBS")
            CTEST_ARGS=(--test-dir "$DIR" -C "$CFG" -j "$JOBS")
        fi
        if ! cmake -S . -B "$DIR" "${CMAKE_ARGS[@]}" -DPS_BUILD_TESTS=ON \
            >"$TMP/preflight-$CFG-cmake.log" 2>&1; then
            bad "$CFG configure 失败（看 $TMP/preflight-$CFG-cmake.log）"
            continue
        fi
        ok "$CFG configure"
        if ! cmake "${BUILD_ARGS[@]}" >"$TMP/preflight-$CFG-build.log" 2>&1; then
            bad "$CFG build 失败（看 $TMP/preflight-$CFG-build.log）"
            continue
        fi
        ok "$CFG build"
        ctest "${CTEST_ARGS[@]}" --output-on-failure >"$TMP/preflight-$CFG-ctest.log" 2>&1 \
            && ok "$CFG ctest 全绿" || bad "$CFG ctest 红（看 $TMP/preflight-$CFG-ctest.log）"
    done

    bold "5) clang-tidy 增量（只断新增行）"
    CPP_LIST="$(changed '*.cpp')"
    if [ -z "$CPP_LIST" ]; then
        ok "无 .cpp 改动，tidy 跳过"
    elif ! have clang-tidy; then
        skip "clang-tidy 未安装"
    elif ps_is_windows && [ "$NINJA_MODE" -eq 0 ]; then
        # 需要 compile_commands.json，而 VS 生成器 + 不在 MSVC 环境里拿不到。
        skip "Windows 无 MSVC 环境（cl 不在 PATH），tidy 未查"
    else
        if cmake -S . -B build-tidy -G Ninja -DPS_BUILD_TESTS=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
            >"$TMP/tidy-cmake.log" 2>&1; then
            : >"$TMP/tidy.log"
            for f in $CPP_LIST; do
                echo "=== $f ===" >>"$TMP/tidy.log"
                clang-tidy -p build-tidy "$f" >>"$TMP/tidy.log" 2>&1
            done
            "$PYTHON" tools/clang-tidy-diff-gate.py --log "$TMP/tidy.log" --base "$BASE" \
                && ok "tidy 新增行 0" || bad "tidy 新增行有告警（看 $TMP/tidy.log）"
        else
            bad "tidy configure 失败（看 $TMP/tidy-cmake.log）"
        fi
    fi

    bold "6) 架构反证"
    if [ -f tools/ci/arch_guard.sh ]; then
        ps_run_script tools/ci/arch_guard.sh --quick >"$TMP/preflight-arch.log" 2>&1 \
            && ok "arch_guard --quick" || bad "arch_guard 红（看 $TMP/preflight-arch.log）"
    else
        skip "arch_guard.sh 缺失"
    fi
fi

printf '\n== 汇总：FAIL=%d SKIP=%d（mode=%s）==\n' "$FAIL" "$SKIPPED" "$MODE"
[ "$FAIL" -eq 0 ] || { echo "有失败项：修完再推（CI 的同一口径会红）"; exit 1; }
echo "通过：可以推送"
