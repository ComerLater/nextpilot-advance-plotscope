#!/usr/bin/env bash
# ADS vendoring 漂移校验：目录内容必须等于基线，差异必须登记在 VENDOR.md。
#
# 用法：
#   tools/ci/check-ads-vendoring.sh            # --check（默认）：离线，只算本地哈希
#   tools/ci/check-ads-vendoring.sh --gen      # 联网取基线 tarball，重算清单
#
# 清单 src/3rdparty/ads/vendor.sha256 由 --gen 生成，勿手改。三类行（路径相对 ADS 目录）：
#   upstream <sha256> <path>   必须与基线逐字节一致
#   patch    <sha256> <path>   本地改写或补丁；哈希取基线的，必须与上游不同
#   local   <path>             上游没有的文件
#
# 判定口径：
#   - 未登记差异 / 未登记新增 / 补丁丢失 / 清单声明了不存在的文件 / 清单与 VENDOR.md 基线不一致 → 失败；
#   - patch 与 local 的路径必须在 VENDOR.md 出现，避免清单与台账各说一套；
#   - 只比对本仓实际 vendored 的文件，不要求包含上游已裁剪的 demo/ doc/ 等。
#
# 权威说明见 src/3rdparty/ads/VENDOR.md。

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT" || exit 1

VENDOR="src/3rdparty/ads"
MANIFEST="vendor.sha256" # 相对 $VENDOR
DOC="VENDOR.md"          # 相对 $VENDOR
UPSTREAM="githubuser0xFFFF/Qt-Advanced-Docking-System"
CACHE="build/ads-vendor-cache"
# 本仓自维护、不要求被 VENDOR.md 提及的条目。
META="VENDOR.md vendor.sha256"

MODE=check
case "${1:-}" in
    ""|--check) MODE=check ;;
    --gen) MODE=gen ;;
    -h|--help) sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "未知参数：$1（见 --help）"; exit 2 ;;
esac

have() { command -v "$1" >/dev/null 2>&1; }
PY=""
if have python3; then PY=python3; elif have python; then PY=python; elif have py; then PY=py; fi

sha() {
    if have sha256sum; then
        sha256sum "$1" | awk '{print $1}'
    elif have shasum; then
        shasum -a 256 "$1" | awk '{print $1}'
    elif [ -n "$PY" ]; then
        "$PY" -c 'import hashlib,sys;print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$1"
    else
        echo "缺少 sha256sum / shasum / python，无法校验哈希" >&2
        return 1
    fi
}

doc_baseline() {
    grep -oE '基线提交：`[0-9a-f]{7,40}`' "$VENDOR/$DOC" 2>/dev/null | head -1 | tr -d '`' | sed 's/基线提交：//'
}

is_meta() {
    for m in $META; do [ "$1" = "$m" ] && return 0; done
    return 1
}

FAIL=0
bad() { printf '[FAIL] %s\n' "$1"; FAIL=$((FAIL + 1)); }
ok() { printf '[ok] %s\n' "$1"; }

# ---------------------------------------------------------------- --check
run_check() {
    [ -f "$VENDOR/$DOC" ] || { bad "缺少 $VENDOR/$DOC"; return 1; }
    [ -f "$VENDOR/$MANIFEST" ] || { bad "缺少 $VENDOR/$MANIFEST（跑 --gen 生成）"; return 1; }

    local baseline mbaseline
    baseline="$(doc_baseline)"
    [ -n "$baseline" ] || bad "$VENDOR/$DOC 里找不到「基线提交：\`<sha>\`」"
    mbaseline="$(awk '/^# baseline /{print $3; exit}' "$VENDOR/$MANIFEST")"
    [ "$mbaseline" = "$baseline" ] ||
        bad "清单基线 $mbaseline 与 VENDOR.md 基线 $baseline 不一致（跑 --gen 重算）"

    mkdir -p "$CACHE"
    local declared="$CACHE/declared.txt"
    : >"$declared"

    local n_up=0 n_patch=0 n_local=0 line kind h p actual
    while IFS= read -r line; do
        case "$line" in ''|'#'*) continue ;; esac
        # shellcheck disable=SC2086
        set -- $line
        case "$1" in
            upstream) kind=upstream; h="$2"; p="$3"; n_up=$((n_up + 1)) ;;
            patch) kind=patch; h="$2"; p="$3"; n_patch=$((n_patch + 1)) ;;
            local) kind=local; p="$2"; n_local=$((n_local + 1)) ;;
            *) bad "清单行无法识别：$line"; continue ;;
        esac

        printf '%s\n' "$p" >>"$declared"
        if [ ! -f "$VENDOR/$p" ]; then
            bad "清单声明但文件不存在：$VENDOR/$p"
            continue
        fi
        actual="$(sha "$VENDOR/$p")" || continue

        case "$kind" in
            upstream) [ "$actual" = "$h" ] || bad "与基线不同且未登记：$VENDOR/$p" ;;
            # 反向校验：补丁必须还在；被上游内容覆盖（哈希相等）说明补丁丢了。
            patch) [ "$actual" != "$h" ] || bad "补丁丢失（内容等于上游）：$VENDOR/$p" ;;
        esac

        # 只有本地改写/补丁与上游没有的文件需要在台账里说明来历。
        if [ "$kind" != "upstream" ] && ! is_meta "$p" && ! grep -qF -- "$p" "$VENDOR/$DOC"; then
            bad "$DOC 未记载该条目：$p"
        fi
    done <"$VENDOR/$MANIFEST"

    # 覆盖性：目录里的文件都必须出现在清单，清单也不能凭空多出文件。
    local tree="$CACHE/tree.txt"
    find "$VENDOR" -type f | sed "s|^$VENDOR/||" | grep -vxF "$MANIFEST" | sort >"$tree"
    sort -u "$declared" >"$declared.sorted"
    while IFS= read -r p; do
        [ -n "$p" ] && bad "未登记新增文件：$VENDOR/$p"
    done < <(comm -13 "$declared.sorted" "$tree")
    while IFS= read -r p; do
        [ -n "$p" ] && bad "清单多出文件（源码已删？）：$VENDOR/$p"
    done < <(comm -23 "$declared.sorted" "$tree")
    rm -f "$declared.sorted"

    [ "$n_up" -gt 0 ] || bad "清单没有任何 upstream 行"
    [ "$FAIL" -eq 0 ] || return 1

    ok "基线 $baseline：$n_up 个文件逐字节一致"
    ok "本地改写/补丁 $n_patch 项、local $n_local 项，均已在 $DOC 登记"
    printf '   patch/local：'
    awk '/^(patch|local) /{printf "%s ", $NF}' "$VENDOR/$MANIFEST"
    printf '\n'
}

# ------------------------------------------------------------------ --gen
run_gen() {
    [ -f "$VENDOR/$DOC" ] || { bad "缺少 $VENDOR/$DOC"; return 1; }
    local baseline tgz updir top
    baseline="$(doc_baseline)"
    [ -n "$baseline" ] || { bad "$VENDOR/$DOC 里找不到基线提交"; return 1; }

    mkdir -p "$CACHE"
    tgz="$CACHE/$baseline.tgz"
    if [ ! -f "$tgz" ]; then
        echo "下载基线 tarball：$baseline"
        if ! curl -fsSL "https://codeload.github.com/$UPSTREAM/tar.gz/$baseline" -o "$tgz.part"; then
            bad "下载失败（--gen 需要网络）：$UPSTREAM@$baseline"
            rm -f "$tgz.part"
            return 1
        fi
        mv "$tgz.part" "$tgz"
    fi
    updir="$CACHE/extract-$baseline"
    if [ ! -d "$updir" ]; then
        mkdir -p "$updir"
        tar xzf "$tgz" -C "$updir" || { bad "解包失败：$tgz"; return 1; }
    fi
    top="$(find "$updir" -mindepth 1 -maxdepth 1 -type d | head -1)"
    [ -n "$top" ] || { bad "tarball 解包后找不到顶层目录"; return 1; }

    local up="$CACHE/up.txt" pa="$CACHE/patch.txt" lo="$CACHE/local.txt"
    : >"$up"; : >"$pa"; : >"$lo"

    local rel ours theirs
    while IFS= read -r rel; do
        [ "$rel" = "$MANIFEST" ] && continue
        if [ -f "$top/$rel" ]; then
            ours="$(sha "$VENDOR/$rel")" || return 1
            theirs="$(sha "$top/$rel")" || return 1
            if [ "$ours" = "$theirs" ]; then
                printf 'upstream %s %s\n' "$ours" "$rel" >>"$up"
            elif is_meta "$rel" || grep -qF -- "$rel" "$VENDOR/$DOC"; then
                printf 'patch %s %s\n' "$theirs" "$rel" >>"$pa"
            else
                bad "与基线不同且 VENDOR.md 未登记：$rel（先补台账再 --gen）"
            fi
        elif is_meta "$rel" || grep -qF -- "$rel" "$VENDOR/$DOC"; then
            printf 'local %s\n' "$rel" >>"$lo"
        else
            bad "上游没有且 VENDOR.md 未登记：$rel"
        fi
    done < <(find "$VENDOR" -type f | sed "s|^$VENDOR/||" | sort)

    [ "$FAIL" -eq 0 ] || { echo "未写入清单：先修上面的登记问题"; return 1; }

    {
        echo "# 由 tools/ci/check-ads-vendoring.sh --gen 生成，勿手改。"
        echo "# baseline $baseline"
        echo "# upstream <sha256> <path>  与基线逐字节一致"
        echo "# patch    <sha256> <path>  本地改写/补丁，哈希取基线的"
        echo "# local   <path>            上游没有的文件"
        sort "$pa"
        sort "$lo"
        sort "$up"
    } >"$VENDOR/$MANIFEST.new"
    mv "$VENDOR/$MANIFEST.new" "$VENDOR/$MANIFEST"
    ok "已写入 $VENDOR/$MANIFEST（$(wc -l <"$VENDOR/$MANIFEST" | tr -d ' ') 行）"
}

if [ "$MODE" = "gen" ]; then
    run_gen || exit 1
else
    run_check || exit 1
fi
