#!/usr/bin/env bash
# ps-setup.sh — Linux 薄壳，由 gen-shell 生成，勿手改。
# 找到 Python 就把参数原样交给 ps-setup.py。
# 改 Python 版本后重跑：python3 tools/ps-setup.py gen-shell
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PS_DEPS_ROOT="${PS_DEPS_ROOT:-$HOME/.ps-deps}"
TOOL_PY="$ROOT/tools/ps-setup.py"

log() { echo "[ps-setup] $*"; }

# 日常路径：venv 已就绪，直接进内核
VENV_PY="$PS_DEPS_ROOT/toolchain/python/venv/bin/python"
if [ -x "$VENV_PY" ]; then
  exec "$VENV_PY" "$TOOL_PY" "$@"
fi

# venv 没了但解释器还在，先用它进内核（install 会补建 venv）
PBS_PY="$PS_DEPS_ROOT/toolchain/python/pbs/python/bin/python3"
if [ -x "$PBS_PY" ]; then
  exec "$PBS_PY" "$TOOL_PY" "$@"
fi

# 只有 git 走 apt；没免密 sudo 就打印命令让用户自己装
if ! command -v git >/dev/null 2>&1; then
  if command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq git
  else
    log "缺 git 且无免密 sudo，先执行后重跑：sudo apt-get install -y git"
    exit 1
  fi
fi

# 下载 pbs 解释器，解压到环境根，不碰系统 Python
case "$(uname -m)" in
  x86_64) FILE="cpython-3.12.12+20251014-x86_64-unknown-linux-gnu-install_only.tar.gz"; SHA="1ab2b6594d1c3d76cbebea09d6bc3e6ba68d8eb3b6322080375c4cc3dd188f34" ;;
  aarch64|arm64) FILE="cpython-3.12.12+20251014-aarch64-unknown-linux-gnu-install_only.tar.gz"; SHA="d32487b853d6f5709019a471770be5e5d3e6bd2ac507e5629e2d6825565d3e71" ;;
  *) log "不支持的架构 $(uname -m)（受管 Qt/Python 只发布 x86_64/aarch64）"; exit 1 ;;
esac
PBS_DST="$PS_DEPS_ROOT/toolchain/python/pbs"
REL="20251014/$FILE"
TMP="$(mktemp)"; OK=0
for M in "https://registry.npmmirror.com/-/binary/python-build-standalone" "https://python-standalone.org/mirror/astral-sh/python-build-standalone" "https://gh-proxy.com/https://github.com/astral-sh/python-build-standalone/releases/download" "https://ghfast.top/https://github.com/astral-sh/python-build-standalone/releases/download" "https://github.com/astral-sh/python-build-standalone/releases/download"; do
  log "尝试 Python 源：$(echo "$M" | sed -E 's#https?://([^/]+).*#\1#')"
  if curl -fsSL --connect-timeout 8 --max-time 600 --speed-limit 10240 --speed-time 30     --retry 1 -o "$TMP" "${M%/}/$REL"; then
    [ "$(sha256sum "$TMP" | cut -d' ' -f1)" = "$SHA" ] || { log "  sha256 不符，换下一源"; continue; }
    OK=1; break
  fi
done
if [ "$OK" = 0 ]; then
  log "受管 Python 下载失败（$FILE，期望 sha256=$SHA）"
  log "可手动下载 $REL 解压到 $PBS_DST"; exit 1
fi
mkdir -p "$PBS_DST"
tar -xzf "$TMP" -C "$PBS_DST"        # pbs 顶层自带 python/ 目录
rm -f "$TMP"
[ -x "$PBS_PY" ] || { log "Python 解压后不可执行，检查 $PBS_DST"; exit 1; }
log "受管 Python 已就位：$PBS_PY（sha256 校验通过）"
exec "$PBS_PY" "$TOOL_PY" "$@"
