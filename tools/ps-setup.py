#!/usr/bin/env python3
"""开发环境守卫内核，由 tools/ps-setup.sh|ps1|bat 薄壳拉起。

薄壳只负责搞到 Python 解释器，其余（体检、安装、卸载、env 生成、入口挂载）
都在这个文件里，平台无关。子命令见 main() 里的 help。

环境根 PS_DEPS_ROOT（默认 ~/.ps-deps），工具链都在 <root>/toolchain/。
版本默认和下载源收在各 Tool 的 manifest 结构体（见 ToolManifest），仓库里
没有任何版本清单文件：升级工具链就是改代码，没有第二份真源。
"""

from __future__ import annotations

import argparse
import contextlib
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request
import zipfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, cast

# 平台差异只在 ps_setup_platform.py 里写一份：本文件不出现 `if PLATFORM.is_windows` 式分支，
# 需要"装在哪/叫什么/哪个 ABI/怎么建入口"时问 PLATFORM。
from ps_setup_platform import PLATFORM, machine_arch

# ---------- 基础 ----------


def get_deps_root() -> Path:
    """环境根：$PS_DEPS_ROOT 优先，缺省 ~/.ps-deps。所有受管工具都装这里。"""
    return Path(os.environ.get("PS_DEPS_ROOT", str(Path.home() / ".ps-deps"))).expanduser()


class EnvPathLayout:
    """环境根下的文件夹布局，唯一的目录拼装点。

    只提前拼好目录结构（bin / venv_bin / pbs_bin / ccache_bin_dir / …），
    组件按语义取目录，不散写 root / "toolchain" / ...。要改目录结构只动这里。
    self.exe 是平台后缀：具体文件名（python.exe / cmake / env.sh / …）由使用方
    在用到时按目录 + 文件名拼，布局类不持有任何指向文件的属性。

    命令经 prepend_bin_to_path() 注入 PATH 后按名字调用，判受管走 detect_source
    （which 命中 + 入口在根内），都不需要具体文件路径。
    """

    def __init__(self, root: Path):
        self.root = root
        self.exe = ".exe" if PLATFORM.is_windows else ""
        scripts = "Scripts" if PLATFORM.is_windows else "bin"

        # 一级目录
        self.bin = root / "bin"
        self.toolchain = root / "toolchain"
        self.caches = root / "caches"
        self.downloads = self.caches / "downloads"
        self.ccache_cache = self.caches / "ccache"

        # Python：venv 是日常入口，pbs 是独立构建的受管解释器
        self.python = self.toolchain / "python"
        self.venv = self.python / "venv"
        self.venv_bin = self.venv / scripts
        self.pbs = self.python / "pbs"  # 解压目标，包内自带顶层 python/ 目录
        self.pbs_bin = self.pbs / "python" / ("bin" if not PLATFORM.is_windows else "")

        # Ninja
        self.ninja = self.toolchain / "ninja"
        self.ninja_bin = self.ninja / "bin"

        # ccache：受管 bin + 缓存目录 + 策略目录（CCACHE_DIR 可覆盖）
        self.ccache = self.toolchain / "ccache"
        self.ccache_bin_dir = self.ccache / "bin"
        self.ccache_libexec = self.ccache / "libexec"
        self.ccache_lib = self.ccache / "lib"
        self.ccache_conf_dir = Path(os.environ.get("CCACHE_DIR") or self.ccache_cache)

        # Rust：RUSTUP_HOME/CARGO_HOME 钉在环境根内
        self.rust = self.toolchain / "rust"
        self.cargo_home = self.rust / "cargo"
        self.cargo_bin = self.cargo_home / "bin"
        self.rustup_home = self.rust / "rustup"

        # Qt：受管安装根（版本/ABI 子目录由 QtTool 扫描，不写死）
        self.qt = self.toolchain / "qt"

    def prepend_bin_to_path(self) -> None:
        """把受管 bin 目录前置到当前进程 PATH，让所有命令按名字命中受管版本。

        内核被薄壳拉起时 PATH 里没有这些目录，导致探测/安装只能拼绝对路径。
        这里一次性注入，后续 pip/cmake/aqt/cargo/ccache 直接用命令名，既少拼路径，
        也保证「探测到的」和「用户 source env 后用到的」是同一套工具。
        目录即使尚不存在也先加进去——PATH 里的空目录无害，等安装写出二进制后
        which 即刻命中，无需在各组件装完后再刷新。幂等，只影响本进程及子进程。
        """
        dirs = [str(d) for d in (self.bin, self.venv_bin, self.pbs_bin, self.ccache_bin_dir, self.cargo_bin)]
        old = [d for d in os.environ.get("PATH", "").split(os.pathsep) if d and d not in dirs]
        os.environ["PATH"] = os.pathsep.join(dirs + old)

    def download_path(self, name: str) -> Path:
        """下载暂存文件。"""
        return self.downloads / name

    def mkdirs(self) -> None:
        """install 前的一次性目录准备。"""
        for d in (self.bin, self.ccache_cache, self.downloads):
            d.mkdir(parents=True, exist_ok=True)


class RepoPathLayout:
    """代码仓库内的路径布局（与 PS_DEPS_ROOT 无关，只随 __file__ 走）。"""

    def __init__(self, root: Path):
        self.root = root
        self.rust_toml = root / "src" / "kernel" / "rust-toolchain.toml"
        self.qt = root / "deps" / "Qt"  # 仓库自带的 Qt（免安装兜底）
        self.githooks = root / ".githooks"
        self.git = root / ".git"
        self.vscode = root / ".vscode"
        self.settings = self.vscode / "settings.json"
        self.logo = root / "docs" / "assets" / "logo.png"
        self.tools = root / "tools"


REPO = RepoPathLayout(Path(__file__).resolve().parent.parent)


# 清单结构（由各 Tool 类的 manifest 汇总而来，没有外部清单文件）：
#   toolchains — 每个工具「装什么」：各 Tool.manifest 的 version/sha256/extra。
#   sources    — 每个工具「从哪下」：各 Tool.manifest.urls。
#   packages   — 第三方源码包（zlib/zstd 等）不在这里：由 cmake/Dependencies.cmake
#                的 PS_VER_*/PS_MIRRORS_* 常量自管（CMake 自己的依赖，与工具链无关）。
# 版本常量只写在各 Tool 子类里（改某个工具只动它的类），default_manifest() 汇总。


class DepsManifest:
    """工具链版本清单：代码为源，无外部清单文件。

    版本表达式语义与旧 bash 的 ver_split/ver_satisfy 一致：
        ">=3.24"  → (">=", "3.24")   按段比较
        "=6.8.3"  → ("=",  "6.8.3")  精确相等
        "3.24"    → (">=", "3.24")   无运算符默认 >=
    """

    source = "tools/ps-setup.py（各 Tool 类内置默认）"

    def __init__(self):
        self.data = default_manifest()
        self.tc = self.data["toolchains"]

    @staticmethod
    def merge_dicts(base: dict, override: dict) -> dict:
        """深度合并：override 优先，dict 递归合并，其余标量直接覆盖。"""
        out = dict(base)
        for k, v in override.items():
            if isinstance(v, dict) and isinstance(out.get(k), dict):
                out[k] = DepsManifest.merge_dicts(out[k], v)
            else:
                out[k] = v
        return out

    @staticmethod
    def split_expr(expr: str) -> tuple[str, str]:
        """拆版本表达式为 (比较符, 版本)：">=6.8" → (">=", "6.8")；无符号默认 ">="。"""
        expr = expr.strip()
        m = re.match(r"^(>=|==|=)\s*(.+)$", expr)
        if m:
            op = "=" if m.group(1) == "==" else m.group(1)  # == 与 = 同为精确匹配，不降级为 >=
            return op, m.group(2)
        return ">=", expr

    @staticmethod
    def parse_version(v: str) -> tuple[int, ...]:
        """抽取串中所有数字段作版本元组（"1.2.3rc1" → (1,2,3,1)），供逐位比较。"""
        return tuple(int(x) for x in re.findall(r"\d+", v))

    @classmethod
    def test_version(cls, actual: str | None, expr: str) -> bool:
        """实测版本是否满足要求："=" 走精确匹配，其余按数字段逐位比较（缺位补 0）。"""
        if not actual:
            return False
        op, num = cls.split_expr(expr)
        if op == "=":
            return actual == num
        a, b = cls.parse_version(actual), cls.parse_version(num)
        n = max(len(a), len(b))
        a += (0,) * (n - len(a))
        b += (0,) * (n - len(b))
        return a >= b

    def get(self, *keys: str, default=None):
        """按 key 路径读 toolchains 下的值，任一层缺失就返回 default。"""
        d = self.tc
        for k in keys:
            if not isinstance(d, dict) or k not in d:
                return default
            d = d[k]
        return d

    def sources(self, kind: str) -> dict:
        """读取顶层 sources.<kind>（由各 Tool.manifest.urls 汇总，兼容清单覆盖）。"""
        return self.data.get("sources", {}).get(kind, {}) or {}

    @staticmethod
    def extract_diff(base: dict, cur: dict) -> dict:
        """提取 cur 相对 base 的真实差异子集（数组/标量整体比较）。"""
        out: dict = {}
        for k, v in cur.items():
            if isinstance(v, dict) and isinstance(base.get(k), dict):
                res = DepsManifest.extract_diff(base[k], v)
                if res:
                    out[k] = res
            elif k not in base or base[k] != v:
                out[k] = v
        return out


# ---------- 下载 ----------


def test_reachable(url: str, timeout: float = 6.0) -> bool:
    """只探可达性，不下载正文。"""
    try:
        req = urllib.request.Request(url, method="HEAD")
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return 200 <= r.status < 300
    except Exception:
        try:  # 个别源对 HEAD 返回异常，退化为小范围 GET
            req = urllib.request.Request(url, headers={"Range": "bytes=0-0"})
            with urllib.request.urlopen(req, timeout=timeout) as r:
                return 200 <= r.status < 400
        except Exception:
            return False


def download(
    url: str,
    dest: Path,
    want_sha256: str = "",
    connect_timeout: float = 10.0,
    max_time: float = 600.0,
    speed_limit: float = 10240.0,
    speed_time: float = 30.0,
) -> Path:
    """下载 + sha256 校验 + 低速中止。

    speed_limit/speed_time：speed_time 秒内平均吞吐 < speed_limit B/s 视为镜像
    假死（连上但不出数据），立即中止换下一源——语义对齐 bash 的
    curl --speed-limit 10240 --speed-time 30；另有 max_time 硬上限。
    """
    dest.parent.mkdir(parents=True, exist_ok=True)
    # 临时名必须唯一：两个进程同时下同一个文件（多终端 / CI 矩阵）时，
    # 固定的 .part 会互相覆盖，落到 dest 的可能是另一个人写了一半的内容。
    fd, tmp_name = tempfile.mkstemp(prefix=f".{dest.name}.", suffix=".part", dir=str(dest.parent))
    os.close(fd)
    tmp = Path(tmp_name)
    start = time.monotonic()
    last = start
    got = 0
    try:
        with urllib.request.urlopen(url, timeout=connect_timeout) as r, open(tmp, "wb") as f:
            while True:
                chunk = r.read(65536)
                if not chunk:
                    break
                f.write(chunk)
                got += len(chunk)
                now = time.monotonic()
                if now - start > max_time:
                    raise TimeoutError(f"下载超时（>{max_time:.0f}s）：{url}")
                if now - last >= speed_time:
                    n_windows = int((now - start) / speed_time)
                    if got < speed_limit * speed_time * max(n_windows, 1):
                        raise TimeoutError(f"镜像假死（{speed_time:.0f}s 平均 <{speed_limit:.0f}B/s）：{url}")
                    last = now
    except BaseException:
        tmp.unlink(missing_ok=True)
        raise
    if want_sha256:
        h = hashlib.sha256()
        with open(tmp, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        got_sha = h.hexdigest()
        if got_sha != want_sha256:
            tmp.unlink(missing_ok=True)
            raise ValueError(f"sha256 不符：want={want_sha256} got={got_sha}")
    tmp.replace(dest)
    return dest


# ---------- 探测 ----------


def capture(cmd: list[str]) -> str | None:
    """跑命令取 stdout；报错或超时返回 None（探测用，不抛异常打断体检）。"""
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        return r.stdout.strip() or None
    except (OSError, subprocess.TimeoutExpired):
        return None


# ---------- 工具清单结构体 ----------


@dataclass(frozen=True)
class ToolManifest:
    """每个 Tool 持有的同构配置：装什么（version/sha256）+ 从哪下（urls）+
    工具专属字段（extra）。所有工具用同一套字段，差异只在取值。

    name    —— 工具名（与 Tool.name 一致，导出时做 toolchains 的 key）
    version —— 版本表达式（">=3.24" / "=6.8.3"）
    sha256  —— 校验和映射（平台/文件 -> 哈希；无下载的工具留空）
    urls    —— 下载源（kind -> official/mirrors/prefixes；系统组件留空）
    extra   —— 该工具专属键（pbs/configuration/abi/windows_version/…），
               导出时平铺进 toolchains.<name>，与清单外部格式保持一致。
    """

    name: str
    version: str = ""
    sha256: dict = field(default_factory=dict)
    urls: dict = field(default_factory=dict)
    extra: dict = field(default_factory=dict)

    def spec(self) -> dict:
        """还原成清单 toolchains.<name> 段（保持既有外部格式与字段顺序）。"""
        return {"version": self.version, **self.extra, **({"sha256": self.sha256} if self.sha256 else {})}


# ---------- ToolBase 基类与组件注册表（一软件一 class）----------


# 日志流：prepare 要往 stdout 打 JSON，日志临时改走 stderr，免得两边混在一起。
_LOG_STREAM = sys.stdout


def log(msg: str) -> None:
    print(f"[ps-setup] {msg}", file=_LOG_STREAM)


@contextlib.contextmanager
def log_to(stream):
    """临时把日志流换成 stream，退出时复原。

    prepare 要往 stdout 写 JSON，日志必须让到 stderr；用上下文管理器而不是在 main 里
    直接给全局赋值，退出后其它命令（doctor / gen-env）的日志仍回 stdout。
    """
    global _LOG_STREAM
    old = _LOG_STREAM
    _LOG_STREAM = stream
    try:
        yield
    finally:
        _LOG_STREAM = old


def run(cmd: list[object], **kw) -> subprocess.CompletedProcess:
    return subprocess.run([str(c) for c in cmd], **kw)


def make_symlink(src: Path, dst: Path) -> None:
    """受管二进制进 $PS_DEPS_ROOT/bin：软链还是复制由平台决定（见 ps_setup_platform）。"""
    PLATFORM.make_executable_link(src, dst)


def extract_zip(zip_path: Path, dst: Path) -> None:
    """zip 解压，带 zip-slip 防护：逐条解析目标路径，落在 dst 之外就跳过。

    extractall() 在 3.12 之前不做路径检查；包里出现 ..\\ 或绝对路径时能写到 dst 之外。
    """
    dst.mkdir(parents=True, exist_ok=True)
    root = dst.resolve()
    with zipfile.ZipFile(zip_path) as f:
        for info in f.infolist():
            target = (dst / info.filename).resolve()
            if target != root and root not in target.parents:
                log(f"跳过安装包里的可疑成员：{info.filename}")
                continue
            if info.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with f.open(info) as src, open(target, "wb") as out:
                shutil.copyfileobj(src, out)


def extract_tarball(tarball: Path, dst: Path, strip: bool = True) -> None:
    """tar.gz 解压，带路径穿越防护。strip=True 时剥掉一层顶层目录（uv/pbs 惯例）。"""
    import tarfile

    dst.mkdir(parents=True, exist_ok=True)
    with tarfile.open(tarball, "r:gz") as t:
        for member in t.getmembers():
            if strip:
                parts = member.name.split("/", 1)
                if len(parts) < 2 or not parts[1]:
                    continue  # 顶层目录本身跳过
                member.name = parts[1]
            if member.name.startswith("/") or ".." in Path(member.name).parts:
                raise ValueError(f"安装包含可疑路径：{member.name}")
            # 纵深防御：符号链接/硬链接能绕过上面的路径检查（链接本身路径正常，
            # 指向目标却在 dst 之外），安装包里不需要它们，直接不落盘。
            if member.issym() or member.islnk():
                log(f"跳过安装包里的链接成员：{member.name}")
                continue
            # set_attrs 是 3.12 才加的参数；清单声明 >=3.10，低版本不能传。
            # 两个分支都自己补 chmod：set_attrs=False 不落任何模式位，pbs 的 python
            # 会变成 644 不可执行（Linux 上直接判"解压后不可执行"）。
            if sys.version_info >= (3, 12):
                t.extract(member, dst, set_attrs=False, filter="data")
            else:
                t.extract(member, dst)
            out = dst / member.name
            if out.exists() and not out.is_symlink() and (member.isfile() or member.isdir()):
                # 模式取归档里的权限位（0o777 掩掉 setuid/setgid/sticky），
                # 既保住可执行位，也不让安装包往系统里塞特殊位。
                out.chmod(member.mode & 0o777)


def _win_program_roots() -> list[Path]:
    """扫落点用的安装根（Program Files / LOCALAPPDATA/Programs …），实现见平台模块。"""
    return PLATFORM.program_roots()


def _win_vs_installs() -> list[Path]:
    """本机已装的 Visual Studio 实例（含 Build Tools），实现见平台模块。"""
    return PLATFORM.vs_installs()


class ToolBase:
    """组件基类：get_installed_version（探测版本）/ install / get_health_record（体检记录）/ uninstall 四件套。
    uninstall 红线：只删 $PS_DEPS_ROOT/toolchain 与 <root>/bin 下自己的链接，
    绝不碰 env.sh / profile 挂载 / 系统组件。"""

    name = ""  # 注册表键 + 探测命令名（python 按平台覆盖 detect_name）
    manifest = ToolManifest(name="")  # 每个子类用自己的同构实例覆盖
    detect_name = ""  # 探测/判源用的命令名，默认取 name；python 按平台区分（Windows 只有 python.exe）
    check_order: float = -1  # 体检显示顺序（<0 表示不体检，如 system）；float 允许插队（1.5）

    def __init__(self, m: DepsManifest, root: Path):
        self.m = m
        self.root = root
        self.p = EnvPathLayout(root)

    @staticmethod
    def _extract_version(cmd: list[str], pattern: str, group: int = 1) -> str | None:
        """跑 cmd 后用正则从输出里抠版本号，匹配不到返回 None。"""
        out = capture(cmd)
        if not out:
            return None
        m = re.search(pattern, out, re.M)
        return m.group(group) if m else None

    @staticmethod
    def detect_source(name: str, root: Path) -> str:
        """可执行文件在环境根下算 managed，否则算系统自带。

        受管 bin 目录已在入口注入 PATH，which 会优先命中受管版本，
        因此只需判断命中的入口路径是否落在环境根内。用 absolute 不解析软链：
        venv 的 python3 / bin 的 cmake 软链都指向环境外的真身，但入口在根内，
        仍算受管；系统命令（/usr/bin）在根外，算系统自带。
        """
        found = shutil.which(name)
        if not found:
            return "none"
        try:
            return "managed" if Path(found).absolute().is_relative_to(Path(root).absolute()) else "system"
        except (OSError, ValueError):
            return "system"

    def scan_candidates(self) -> list[Path]:
        """PATH 之外、本机已装好的常见落点（Windows 为主：VS 自带工具多半不在 PATH）。
        默认不扫；各工具按自己的生态覆盖。"""
        return []

    def find_executable(self) -> tuple[str | None, str]:
        """先 PATH（受管入口已在最前），再扫已知安装根；返回 (路径, 来源)。

        Windows 上 VS 自带的 cmake/ninja、scoop/choco 的 ccache 常常没进 PATH，
        用户机器上明明有、门禁却判"缺"并下载一份，纯属浪费——所以扫一遍再判。
        """
        name = self.detect_name or self.name
        found = shutil.which(name)
        if found:
            return found, self.detect_source(name, self.root)
        for cand in self.scan_candidates():
            if cand.is_file():
                return str(cand), "scan"
        return None, "none"

    def detect_version(self, root: Path | None = None) -> str | None:
        """探测版本；root 可覆盖探测根（cmake 探系统版本时传 Path("/")）。"""
        raise NotImplementedError

    def get_installed_version(self) -> str | None:
        """当前工具版本，未安装返回 None。"""
        return self.detect_version(self.root)

    def applicable(self) -> bool:
        """该组件在当前平台是否有意义：不适用则不体检、也不参与整体 ok 判定。"""
        return True

    def get_health_record(self) -> dict:
        """一条体检记录，字段即 --json 输出。"""
        expr = self.m.get(self.name, "version", default="")
        v = self.get_installed_version()
        ok = DepsManifest.test_version(v, expr)
        _, src = self.find_executable() if v else (None, "none")
        return {"tool": self.name, "require": expr, "actual": v or "缺", "source": src, "ok": ok}

    def resolve_path(self) -> str | None:
        """该组件在磁盘上的落点：命令类工具给 which/扫描命中的入口，前缀类工具（qt）覆盖本方法。"""
        return self.find_executable()[0]

    def resolve(self) -> dict:
        """一条解析记录 = 体检记录 + 落盘路径。doctor / resolve 子命令 / CMake 门禁共用同一份发现。"""
        rec = self.get_health_record()
        rec["path"] = self.resolve_path() or ""
        return rec

    def install(self) -> None:
        """子类实现安装；系统组件（gcc）不支持自动装。"""
        raise NotImplementedError(f"{self.name} 无自动安装")

    # 卸载目标对应的 EnvPathLayout 属性名（单路径）；复合目标在 uninstall() 里传入
    uninstall_attr = ""

    def uninstall(self) -> None:
        """删 uninstall_attr 指向的目标；多路径工具覆盖此方法并调 _remove(list)。"""
        self._remove([getattr(self.p, self.uninstall_attr)] if self.uninstall_attr else [])

    def _remove(self, paths: list[Path]) -> None:
        """校验所有路径都在环境根内再删，防软链逃逸误删系统文件。"""
        root = Path(os.path.abspath(self.root))
        for p in paths:
            # 用 abspath（不解析软链）：bin/ 下指向系统工具的软链删除本身安全；
            # 若 toolchain 子目录被换成逃逸软链，rmtree 会拒绝符号链接，同样安全。
            if not Path(os.path.abspath(p)).is_relative_to(root):
                raise RuntimeError(f"卸载拒绝：{p} 不在环境根 {root} 下（红线）")
        for p in paths:
            if p.is_symlink() or p.is_file():
                p.unlink(missing_ok=True)
                log(f"已删除 {p}")
            elif p.is_dir():
                shutil.rmtree(p, ignore_errors=True)
                log(f"已删除 {p}/")


class PipInstaller:
    """pip 安装封装：选可达 index + 证书问题逐级降级（CA bundle → trusted-host）。"""

    @staticmethod
    def select_index(m: DepsManifest) -> str:
        """轮询 sources.pypi 选第一个可达 index；探测失败回显第一个（让安装阶段报真实错误）。
        mirrors/official 均为完整 index URL（已含 /simple/）。"""
        src = m.sources("pypi")
        mirrors = [*src.get("mirrors", []), src.get("official", "")]
        for u in mirrors:
            if u and test_reachable(u.rstrip("/") + "/"):
                return u
        return mirrors[0] if mirrors else ""

    @staticmethod
    def _ca_bundle() -> Path | None:
        """系统 CA bundle 的常见落点，按发行版顺序探测——不写死某一条：
        Debian/Ubuntu、RHEL/Fedora（新旧两处）、openSUSE、macOS/BSD、Homebrew。
        都没有就返回 None，调用方直接跳过这一层，不去猜路径。"""
        for p in (
            "/etc/ssl/certs/ca-certificates.crt",
            "/etc/pki/tls/certs/ca-bundle.crt",
            "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
            "/etc/ssl/ca-bundle.pem",
            "/etc/ssl/cert.pem",
            "/usr/local/etc/openssl@3/cert.pem",
        ):
            if Path(p).is_file():
                return Path(p)
        return None

    @staticmethod
    def install(m: DepsManifest, *args: str, env_extra: dict | None = None) -> None:
        """先原样装；确为证书问题时改用系统 CA bundle（保留校验）；仍失败才降级 --trusted-host，
        且只信任当前 index 的 host。pip 由入口注入的 PATH 解析，不拼绝对路径。"""
        index = PipInstaller.select_index(m)
        base: list[object] = ["pip", "install", "--quiet", "--disable-pip-version-check"]
        if index:
            base += ["--index-url", index]
        r = run(base + list(args), capture_output=True, text=True, env={**os.environ, **(env_extra or {})})
        if r.returncode == 0:
            return
        err = (r.stderr or "") + (r.stdout or "")
        if re.search(
            r"CERTIFICATE_VERIFY_FAILED|certificate verify failed|unable to get local issuer certificate", err, re.I
        ):
            ca = PipInstaller._ca_bundle()
            if ca:
                log(f"pip 证书校验失败，尝试系统 CA bundle：{ca}（保留校验）")
                r2 = run(
                    base + ["--cert", str(ca), *args],
                    capture_output=True,
                    text=True,
                    env={**os.environ, **(env_extra or {})},
                )
                if r2.returncode == 0:
                    return
                err = (r2.stderr or "") + (r2.stdout or "")
            host = ""
            if index:
                mm = re.match(r"^[a-z]+://([^/]*)", index)
                host = mm.group(1) if mm else ""
            if host:
                log(f"系统 CA 仍失败，降级为仅信任当前源 host：{host}（关闭该 host 校验，仅此环境）")
                env3 = {**os.environ, **(env_extra or {})}
                r3 = run(base + ["--trusted-host", host, *args], capture_output=True, text=True, env=env3)
                if r3.returncode == 0:
                    return
                err = (r3.stderr or "") + (r3.stdout or "")
        # 走 log 而不是直接 print：prepare 模式下日志流已被换成 stderr，
        # 直接 print 会绕过这个约定，和 stdout 上的 JSON 抢同一条流。
        if err.strip():
            log(f"pip 安装错误详情：\n{err.strip()}")
        raise RuntimeError(f"pip 安装失败：{' '.join(args)}")


class GccTool(ToolBase):
    name = "gcc"
    manifest = ToolManifest(name="gcc", version=">=9")  # 系统组件，apt 源见 SystemTool
    check_order = 0

    def applicable(self) -> bool:
        """Windows 没有 gcc：这条永远红会把整体体检判死（MSVC 由 CMake 的
        project() 编译探测兜底），因此该平台不显示、不参与 ok 判定。"""
        return not PLATFORM.is_windows

    def detect_version(self, root: Path | None = None) -> str | None:
        which, _ = self.find_executable()
        return capture([which, "-dumpversion"]) if which else None

    def install(self) -> None:
        raise RuntimeError("gcc 属系统组件（不进受管目录）：sudo apt-get install -y build-essential")

    def uninstall(self) -> None:
        raise RuntimeError("gcc 属系统组件，uninstall 不碰系统（红线）")


class CmakeTool(ToolBase):
    name = "cmake"
    manifest = ToolManifest(
        name="cmake",
        version=">=3.24",
        urls={
            "pypi": {
                "official": "https://pypi.org/simple",
                "mirrors": [
                    "https://mirrors.aliyun.com/pypi/simple/",
                    "https://pypi.tuna.tsinghua.edu.cn/simple",
                ],
            }
        },
    )
    check_order = 1

    def detect_version(self, root: Path | None = None) -> str | None:
        """受管 bin/venv 已在 PATH 最前，which 命中即受管版本；否则扫本机落点（VS 自带 CMake）。"""
        which, _ = self.find_executable()
        return ToolBase._extract_version([which, "--version"], r"cmake version (\S+)") if which else None

    def scan_candidates(self) -> list[Path]:
        """装了但没进 PATH 的 CMake：独立安装（Program Files/LOCALAPPDATA），
        以及 VS 自带的 CMake 组件（在 Common7 下，永远不在 PATH 上）。"""
        if not PLATFORM.is_windows:
            return []
        out = [r / "CMake" / "bin" / "cmake.exe" for r in _win_program_roots()]
        for vs in _win_vs_installs():
            out.append(
                vs / "Common7" / "IDE" / "CommonExtensions" / "Microsoft" / "CMake" / "CMake" / "bin" / "cmake.exe"
            )
        return out

    def install(self) -> None:
        """cmake 从 pip 装 wheel 再软链到 bin；系统自带的若达标就不动。"""
        p, m = self.p, self.m
        expr = m.get("cmake", "version", default=">=3.24")
        want = DepsManifest.split_expr(expr)[1]
        cmake_link = p.bin / f"cmake{p.exe}"

        if cmake_link.is_file() or cmake_link.is_symlink():
            return  # 幂等
        vcm = self.detect_version(self.root)
        if vcm and DepsManifest.test_version(vcm, expr):
            return  # 受管/系统已达标
        if shutil.which("pip"):
            pkg = f"cmake=={want}" if "=" in DepsManifest.split_expr(expr)[0] else "cmake"
            PipInstaller.install(m, pkg)
            src = shutil.which("cmake")
            if not src:
                raise RuntimeError("cmake 装完却找不到可执行文件")
            make_symlink(Path(src), cmake_link)
            log(f"cmake 由 pip 装入 venv：{cmake_link}（{self.detect_version(self.root)}）")
            return
        raise RuntimeError(f"cmake 未就绪（需 {expr}）；先跑 install-python 再重跑 install-cmake")

    def uninstall(self) -> None:
        self._remove([self.p.bin / f"cmake{self.p.exe}"])
        if shutil.which("pip"):
            run(["pip", "uninstall", "-y", "cmake"], capture_output=True, text=True)
            log("venv 内 pip 包 cmake 已卸载")


class NinjaTool(ToolBase):
    name = "ninja"
    manifest = ToolManifest(
        name="ninja",
        version=">=1.10",
        sha256={"windows-x86_64": "f550fec705b6d6ff58f2db3c374c2277a37691678d6aba463adcbb129108467a"},
        urls={"github": {"prefixes": ["https://gh-proxy.com/", "https://ghfast.top/", "https://ghproxy.net/"]}},
        extra={"windows_version": "1.12.1"},  # 平台专属钉版（Windows 下直接用二进制）
    )
    check_order = 2

    def detect_version(self, root: Path | None = None) -> str | None:
        which, _ = self.find_executable()
        return capture([which, "--version"]) if which else None

    def scan_candidates(self) -> list[Path]:
        """VS 自带 ninja 在 Common7/IDE/.../CMake/Ninja 下，不在 PATH —— 这是
        Windows 上"机器明明有 ninja 却判缺"最常见的一处。"""
        if not PLATFORM.is_windows:
            return []
        out = [r / "ninja" / "ninja.exe" for r in _win_program_roots()]
        for vs in _win_vs_installs():
            out.append(vs / "Common7" / "IDE" / "CommonExtensions" / "Microsoft" / "CMake" / "Ninja" / "ninja.exe")
        return out

    def install(self) -> None:
        """Windows 用 GitHub 二进制；Linux 系统有就用，没有提示装。"""
        p, m = self.p, self.m
        exe, _src = self.find_executable()
        if exe:
            v = capture([exe, "--version"])
            if v and DepsManifest.test_version(v, m.get("ninja", "version", default="")):
                log(f"ninja {v} 已就位（沿用本机安装）：{exe}")
                return
        if not PLATFORM.is_windows:
            raise RuntimeError("缺 ninja：sudo apt-get install -y ninja-build 后重跑")
        ver = DepsManifest.split_expr(m.get("ninja", "version", default="1.12.1"))[1].lstrip("v")
        url_gh = f"https://github.com/ninja-build/ninja/releases/download/v{ver}/ninja-win.zip"
        want = m.get("ninja", "sha256", "windows-x86_64", default="")
        z = p.download_path("ninja-win.zip")
        for base in [*m.sources("github").get("prefixes", []), ""]:
            url = (base.rstrip("/") + "/" + url_gh) if base else url_gh
            try:
                download(url, z, want_sha256=want)
                break
            except (OSError, ValueError, TimeoutError) as e:
                log(f"ninja 源失败：{e}")
        else:
            raise RuntimeError("ninja 下载失败")
        p.ninja_bin.mkdir(parents=True, exist_ok=True)
        extract_zip(z, p.ninja_bin)
        make_symlink(p.ninja_bin / "ninja.exe", p.bin / f"ninja{p.exe}")
        log(f"ninja {ver} 已就位：{p.ninja_bin}")

    def uninstall(self) -> None:
        self._remove([self.p.ninja, self.p.bin / "ninja.exe", self.p.bin / "ninja"])


class PythonTool(ToolBase):
    name = "python"
    detect_name = "python" if PLATFORM.is_windows else "python3"
    # python-build-standalone（pbs）：CPython 官方独立构建，钉死构建标识跨机器一致。
    # 薄壳/内核直接下载解压（系统自带 tar），不碰系统 Python、可整体卸载。
    manifest = ToolManifest(
        name="python",
        version=">=3.10",
        # pbs 各平台 install_only 包：文件名 + sha256（平台名是 key，按 uname 映射）。
        # 扁平结构放 extra，键名与清单 toolchains.python.pbs 一致。
        extra={
            "pbs_build": "20251014",
            "pbs": {
                "x86_64-unknown-linux-gnu": {
                    "file": "cpython-3.12.12+20251014-x86_64-unknown-linux-gnu-install_only.tar.gz",
                    "sha256": "1ab2b6594d1c3d76cbebea09d6bc3e6ba68d8eb3b6322080375c4cc3dd188f34",
                },
                "aarch64-unknown-linux-gnu": {
                    "file": "cpython-3.12.12+20251014-aarch64-unknown-linux-gnu-install_only.tar.gz",
                    "sha256": "d32487b853d6f5709019a471770be5e5d3e6bd2ac507e5629e2d6825565d3e71",
                },
                "x86_64-pc-windows-msvc": {
                    "file": "cpython-3.12.12+20251014-x86_64-pc-windows-msvc-install_only.tar.gz",
                    "sha256": "2d670beb3b930d30e3a13cc909923a001dbdfcb5537692d5da40b6b41643ce1c",
                },
            },
            "requirements": "tools/requirements.txt",
        },
        urls={
            # pbs 受管解释器下载：完整 base（拼 /<tag>/<file> 即最终 URL）
            "python": {
                "official": "https://github.com/astral-sh/python-build-standalone/releases/download",
                "mirrors": [
                    "https://registry.npmmirror.com/-/binary/python-build-standalone",
                    "https://python-standalone.org/mirror/astral-sh/python-build-standalone",
                    "https://gh-proxy.com/https://github.com/astral-sh/python-build-standalone/releases/download",
                    "https://ghfast.top/https://github.com/astral-sh/python-build-standalone/releases/download",
                ],
            },
            # requirements.txt 里的 pip 包从 pypi 装。
            "pypi": {
                "official": "https://pypi.org/simple",
                "mirrors": [
                    "https://mirrors.aliyun.com/pypi/simple/",
                    "https://pypi.tuna.tsinghua.edu.cn/simple",
                ],
            },
        },
    )
    check_order = 3
    uninstall_attr = "python"

    def detect_version(self, root: Path | None = None) -> str | None:
        """PATH 顺序即优先级：venv（日常入口）→ pbs 受管解释器 → 系统 python。"""
        which, _ = self.find_executable()
        return capture([which, "-c", "import sys;print('%d.%d.%d'%sys.version_info[:3])"]) if which else None

    def scan_candidates(self) -> list[Path]:
        """装了 Python 但没勾"加入 PATH" 的机器（Windows 官方安装器的默认行为）：
        扫 Program Files / LOCALAPPDATA/Programs 下的 Python3*，新版本在前。"""
        if not PLATFORM.is_windows:
            return []
        out = []
        for r in _win_program_roots():
            out += [d / "python.exe" for d in sorted(r.glob("Python3*"), reverse=True)]
        return out

    @staticmethod
    def install_pbs(root: Path) -> Path:
        """下载 python-build-standalone 受管解释器到 <root>/toolchain/python/pbs/python/。
        幂等：解释器已存在直接返回。四源轮询（清单 mirrors）+ 官方兜底 + sha256 校验。
        不经 uv、不碰系统 Python；卸载即删 toolchain/python，干净。"""
        import tarfile

        p = EnvPathLayout(root)
        m = DepsManifest()
        tag = m.get("python", "pbs_build", default="")
        # 架构名统一由 ps_setup_platform.machine_arch() 归一化（AMD64/ARM64 与
        # x86_64/aarch64 都收成发布包用的那两种），这里只拼后缀，不再自带一份映射。
        arch = machine_arch()
        triple = f"{arch}-pc-windows-msvc" if PLATFORM.is_windows else f"{arch}-unknown-linux-gnu"
        if not tag or not triple:
            raise RuntimeError(f"不支持的架构/清单缺 python.pbs_build（tag={tag!r} triple={triple!r}）")
        info = m.get("python", "pbs", triple, default=None)
        if not info:
            raise RuntimeError(f"清单缺 toolchains.python.pbs.{triple}")
        fname, want = info["file"], info["sha256"]
        pbs_exe = p.pbs_bin / ("python.exe" if p.exe else "python3")
        if pbs_exe.is_file() and capture([str(pbs_exe), "-c", "import sys;print(sys.version_info[:2])"]):
            return pbs_exe

        tarball = p.download_path(fname)
        py_src = m.sources("python")
        mirrors = [*py_src.get("mirrors", []), py_src.get("official", "")]  # 官方直连兜底
        rel = f"{tag}/{fname}"
        ok = False
        for base in mirrors:
            url = base.rstrip("/") + "/" + rel
            log(f"尝试 Python 源：{base.split('/')[2] if '/' in base else base}")
            try:
                download(url, tarball, want_sha256=want, connect_timeout=10, max_time=600)
                ok = True
                break
            except (OSError, ValueError, TimeoutError) as e:
                log(f"  失败：{e}")
        if not ok:
            raise RuntimeError(
                f"受管 Python 下载失败（{fname}，期望 sha256={want}）；可手动下载后放入 {tarball.parent}"
            )

        try:
            extract_tarball(tarball, p.pbs, strip=False)  # 保留顶层 python/ 目录
        except (tarfile.TarError, ValueError) as e:
            shutil.rmtree(p.pbs, ignore_errors=True)
            raise RuntimeError(f"Python 解压失败（安装包可能损坏）：{e}；清理后重跑重试") from e
        tarball.unlink(missing_ok=True)
        if not pbs_exe.is_file() or not capture([str(pbs_exe), "-c", "import sys;print(sys.version_info[:2])"]):
            shutil.rmtree(p.pbs, ignore_errors=True)
            raise RuntimeError("Python 解压后不可执行，重跑重试")
        log(f"受管 Python 已就位：{p.pbs}（sha256 校验通过）")
        return pbs_exe

    def install(self) -> None:
        """下载 pbs 解释器、建 venv、装 requirements。
        venv 已就绪就只补 requirements。"""
        p, m = self.p, self.m
        pip_exe = p.venv_bin / f"pip{p.exe}"
        if pip_exe.is_file():
            log(f"venv 已存在：{p.venv}")
        else:
            # 缺解释器就下载解压，sha256 校验
            pbs = self.install_pbs(self.root)
            # pbs 的 install_only 包自带 ensurepip，可直接建 venv
            p.venv.parent.mkdir(parents=True, exist_ok=True)
            log(f"创建 venv：{p.venv}")
            r = run([str(pbs), "-m", "venv", str(p.venv)])
            if r.returncode != 0 or not pip_exe.is_file():
                shutil.rmtree(p.venv, ignore_errors=True)
                raise RuntimeError(f"venv 创建失败：{p.venv}（pbs 解释器 {pbs}）")

        req = REPO.root / (m.get("python", "requirements", default="tools/requirements.txt") or "")
        if req.is_file():
            PipInstaller.install(m, "-r", str(req))
            log(f"pip 依赖已安装：{req}")


class CcacheTool(ToolBase):
    name = "ccache"
    manifest = ToolManifest(
        name="ccache",
        version=">=4.5",
        sha256={"windows-x86_64": "d0836518cfdb74cc765a6bc6c353009367d8fc15dbbec4899ebdbeb750a07f77"},
        urls={"github": {"prefixes": ["https://gh-proxy.com/", "https://ghfast.top/", "https://ghproxy.net/"]}},
        extra={"warm_min_mb": 12, "windows_version": "4.9.1"},  # warm_min_mb 供预热阈值判断
    )
    check_order = 4
    uninstall_attr = "ccache"

    # ccache 的纯策略正文。用户可能手工改过（比如调 max_size），所以只有"内容与模板一致"
    # 时才重写；改过就保留，绝不静默覆盖。
    CONF_BODY = (
        "# 由 tools/ps-setup.py 生成；纯策略，与路径无关。\n"
        "# 改这里对命令行 / IDE / CI 同时生效（不要用 shell export 覆盖）。\n"
        "max_size = 5G\n"
        "# depend mode：用编译器 -MD 依赖信息判命中，跳过完整预处理（实测命中路径 90ms→65ms）。\n"
        "# GCC 支持；CMake 默认给 CXX 加 -MD -MT，无需额外参数。\n"
        "depend_mode = true\n"
        "# hash_dir=false：工作目录不进哈希。仅当源码用相对路径的 __FILE__ 时才有意义，\n"
        "# 本项目不用 __FILE__（已核查），设为 false 可让不同 CWD 的构建共享缓存。\n"
        "hash_dir = false\n"
        "# PCH 共存所需；源码不用 __TIME__/__DATE__，这两项无正确性风险。\n"
        "# 切勿加 include_file_mtime/ctime：会让改过头文件的 TU 命中旧缓存、编出过期代码。\n"
        "sloppiness = pch_defines,time_macros\n"
        "# 缓存压缩，省磁盘；ccache 4.x 默认已开，显式声明以求各版本一致。\n"
        "compression = true\n"
        "compression_level = 5\n"
    )

    @staticmethod
    def write_conf(root: Path) -> Path:
        """把纯策略写进 $CCACHE_DIR/ccache.conf（与路径无关、持久、所有入口共享）。
        与 shell 环境变量的分工：conf 承载 max_size/depend_mode/hash_dir/sloppiness 纯策略，
        shell 只暴露 CCACHE_DIR 物理位置；conf 优先级低于环境变量，shell 里绝不能 export 策略。"""
        d = EnvPathLayout(root).ccache_conf_dir
        conf = d / "ccache.conf"
        d.mkdir(parents=True, exist_ok=True)
        if conf.is_file():
            old = conf.read_text(encoding="utf-8", errors="ignore")
            if old.strip() != CcacheTool.CONF_BODY.strip():
                log(f"保留已有 ccache 策略（与模板不同，视为手工定制）：{conf}")
                return conf
        conf.write_text(CcacheTool.CONF_BODY, encoding="utf-8")
        log(f"已写入 ccache 策略 {conf}")
        return conf

    def detect_version(self, root: Path | None = None) -> str | None:
        """受管 ccache/bin 已在 PATH 最前，which 命中即受管版本，否则回落系统。"""
        which, _ = self.find_executable()
        return ToolBase._extract_version([which, "--version"], r"ccache version (\S+)") if which else None

    def scan_candidates(self) -> list[Path]:
        """scoop / chocolatey / 独立解压的 ccache 常不在 PATH（scoop 的 shims 才在）。"""
        if not PLATFORM.is_windows:
            return []
        out = [r / "ccache" / "ccache.exe" for r in _win_program_roots()]
        home = os.environ.get("USERPROFILE")
        if home:
            out.append(Path(home) / "scoop" / "shims" / "ccache.exe")
        pd = os.environ.get("ProgramData")
        if pd:
            out.append(Path(pd) / "chocolatey" / "bin" / "ccache.exe")
        return out

    def install(self) -> None:
        """Windows 用 GitHub 二进制；Linux 优先系统版本，再固化到持久目录。"""
        p, m = self.p, self.m
        exe, src = self.find_executable()
        if exe and src == "managed":
            log(f"ccache 已就位：{exe}")
            return
        if exe and PLATFORM.is_windows:
            # Windows 上机器里已经有一份达标的 ccache（scoop/choco/独立安装）就沿用，
            # 不再下载一份到 .ps-deps；Linux 仍按原设计把系统 ccache 固化进受管目录。
            v = ToolBase._extract_version([exe, "--version"], r"ccache version (\S+)")
            if v and DepsManifest.test_version(v, m.get("ccache", "version", default="")):
                log(f"ccache {v} 已就位（沿用本机安装）：{exe}")
                return
        if PLATFORM.is_windows:
            ver = DepsManifest.split_expr(m.get("ccache", "version", default="4.9.1"))[1]
            url_gh = f"https://github.com/ccache/ccache/releases/download/v{ver}/ccache-{ver}-windows-x86_64.zip"
            want = m.get("ccache", "sha256", "windows-x86_64", default="")
            z = p.download_path(f"ccache-{ver}-windows-x86_64.zip")
            for base in [*m.sources("github").get("prefixes", []), ""]:
                url = (base.rstrip("/") + "/" + url_gh) if base else url_gh
                try:
                    download(url, z, want_sha256=want)
                    break
                except (OSError, ValueError, TimeoutError) as e:
                    log(f"ccache 源失败：{e}")
            else:
                raise RuntimeError("ccache 下载失败")
            extract_zip(z, p.ccache)
            inner = p.ccache / f"ccache-{ver}-windows-x86_64"
            if inner.is_dir():  # zip 带顶层目录：摊平到 toolchain/ccache
                for f_ in inner.iterdir():
                    shutil.move(str(f_), p.ccache / f_.name)
                inner.rmdir()
            log(f"ccache {ver} 已就位：{p.ccache}")
            return
        # Linux：系统有 ccache 就固化（连依赖库一起拷，环境重置不丢）；没有则提示
        sys_cc = shutil.which("ccache")
        if not sys_cc:
            raise RuntimeError("缺 ccache：sudo apt-get install -y ccache 后重跑 install-ccache")
        shutil.rmtree(p.ccache, ignore_errors=True)
        p.ccache_bin_dir.mkdir(parents=True)
        p.ccache_libexec.mkdir(parents=True)
        p.ccache_lib.mkdir(parents=True)
        shutil.copy2(sys_cc, p.ccache_libexec / "ccache-real")
        ldd = capture(["ldd", sys_cc]) or ""
        for line in ldd.splitlines():
            mm = re.search(r"=> (/\S+)", line)
            if not mm:
                continue
            base = Path(mm.group(1)).name
            if re.match(r"libc\.so\.|libm\.so\.|ld-linux", base):
                continue
            src_l = Path(mm.group(1))
            if src_l.is_file():
                target = p.ccache_lib / base
                if not target.exists():
                    shutil.copy2(src_l, target)
        wrapper = p.ccache_bin_dir / "ccache"
        wrapper.write_text(
            "#!/bin/bash\n"
            'SELF_DIR=$(cd "$(dirname "$0")" && pwd)\n'
            'export LD_LIBRARY_PATH="$SELF_DIR/../lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\n'
            'exec -a ccache "$SELF_DIR/../libexec/ccache-real" "$@"\n',
            encoding="utf-8",
        )
        wrapper.chmod(0o755)
        (p.ccache_libexec / "ccache-real").chmod(0o755)
        log(f"ccache 固化到 {p.ccache}")


class RustTool(ToolBase):
    name = "rust"
    detect_name = "cargo"  # 版本探测与落点都走 cargo，命令名不叫 rust
    manifest = ToolManifest(
        name="rust",
        version="=1.93",
        extra={"configuration": "src/kernel/rust-toolchain.toml", "rustup_version": "1.29.0"},
        # rustup 安装器只从 archive/<版本>/<目标>/ 取：那是不可变路径，能把 sha256 钉死。
        # sh.rustup.rs / win.rustup.rs 是随版本滚动的短链，钉了哈希下次发版就必然失效，
        # 而不钉哈希等于把任意下载物直接以用户权限执行（供应链投毒）。
        sha256={
            "windows": {
                "x86_64": "86478e53f769379d7f0ebfa7c9aa97cb76ca92233f79aa2cc0dbee2efaac73c7",
                "aarch64": "3af309e6c3062aa11df0e932954f69d13b734d8a431e593812f3ecd9ff9e6ef6",
            },
            "linux": {
                "x86_64": "4acc9acc76d5079515b46346a485974457b5a79893cfb01112423c89aeb5aa10",
                "aarch64": "9732d6c5e2a098d3521fca8145d826ae0aaa067ef2385ead08e6feac88fa5792",
            },
        },
        urls={
            "rust": {
                # update_root 是 archive 与发行站的共同根；dist_server 供工具链下载
                "official": {
                    "dist_server": "https://static.rust-lang.org",
                    "update_root": "https://static.rust-lang.org/rustup",
                },
                "mirrors": [
                    {
                        "dist_server": "https://mirrors.ustc.edu.cn/rust-static",
                        "update_root": "https://mirrors.ustc.edu.cn/rust-static/rustup",
                    }
                ],
            }
        },
    )
    check_order = 5
    uninstall_attr = "rust"

    @staticmethod
    def select_source(m: DepsManifest) -> dict:
        """轮询 sources.rust（dist_server/update_root 两件套），全不通回显 official。"""
        src = m.sources("rust")
        mirrors = [*src.get("mirrors", []), src.get("official", {})]
        if not mirrors:
            return {}
        for x in mirrors:
            root = x.get("update_root")
            if root and test_reachable(root.rstrip("/") + "/release-stable.toml"):
                return x
        return mirrors[0]

    def rustup_installer(self, picked: dict) -> tuple[str, str]:
        """rustup-init 的下载 URL 与期望 sha256；缺哈希直接报错，绝不静默跳过校验。"""
        ver = self.m.get("rust", "rustup_version", default="")
        arch = machine_arch()
        want = (self.m.get("rust", "sha256", default={}) or {}).get(PLATFORM.name, {}).get(arch, "")
        if not ver or not want:
            raise RuntimeError(
                f"清单缺 rustup 安装器校验信息（rustup_version={ver!r}，"
                f"sha256[{PLATFORM.name}][{arch}] 为空）：拒绝执行未校验的安装器"
            )
        triple = f"{arch}-pc-windows-msvc" if PLATFORM.is_windows else f"{arch}-unknown-linux-gnu"
        name = "rustup-init.exe" if PLATFORM.is_windows else "rustup-init"
        root = picked.get("update_root") or "https://static.rust-lang.org/rustup"
        return f"{root.rstrip('/')}/archive/{ver}/{triple}/{name}", want

    def detect_version(self, root: Path | None = None) -> str | None:
        """工程钉的 channel 优先（读 rust-toolchain.toml + rustup toolchain 目录），
        不能拿 default toolchain 充数，否则体检会假阳性。
        找 toolchain 时既要看 $RUSTUP_HOME，也要看受管 root——受管 rustup 把 1.93 装在
        <root>/toolchain/rust/rustup 下，只查系统 ~/.rustup 会把已装好的判成缺。"""
        channel = ""
        if REPO.rust_toml.is_file():
            m = re.search(r'channel\s*=\s*"([^"]+)"', REPO.rust_toml.read_text(encoding="utf-8"))
            channel = m.group(1) if m else ""
        if channel:
            homes = []
            env_home = os.environ.get("RUSTUP_HOME", "")
            if env_home:
                homes.append(Path(env_home))
            homes.extend([Path.home() / ".rustup", self.p.rustup_home])
            for rustup_home in homes:
                toolchains = rustup_home / "toolchains"
                if not toolchains.is_dir():
                    continue
                for d in sorted(toolchains.glob(f"{channel}-*")):
                    cargo = d / "bin" / ("cargo.exe" if PLATFORM.is_windows else "cargo")
                    if cargo.is_file():
                        return ToolBase._extract_version([str(cargo), "--version"], r"cargo (\S+)")
        which, _src = self.find_executable()  # 受管 cargo_bin 已在 PATH，系统 cargo 一般也在
        return ToolBase._extract_version([which, "--version"], r"cargo (\S+)") if which else None

    def scan_candidates(self) -> list[Path]:
        """rustup 的默认落点（%USERPROFILE%\\.cargo\\bin）——PATH 被重置后仍能找到。"""
        if not PLATFORM.is_windows:
            return []
        return [Path(os.environ.get("USERPROFILE", str(Path.home()))) / ".cargo" / "bin" / "cargo.exe"]

    def get_channel(self) -> str:
        """从工程 rust-toolchain.toml 读钉住的 channel；没有则返回空串。"""
        toml = REPO.root / (self.m.get("rust", "configuration", default="src/kernel/rust-toolchain.toml") or "")
        if toml.is_file():
            mm = re.search(r'channel\s*=\s*"([^"]+)"', toml.read_text(encoding="utf-8"))
            if mm:
                return mm.group(1)
        return ""

    def install(self) -> None:
        """装 rustup 和工程 channel，RUSTUP_HOME/CARGO_HOME 钉在环境根内。"""
        p, m = self.p, self.m
        want = self.get_channel() or DepsManifest.split_expr(m.get("rust", "version", default=""))[1]

        if self.detect_source("cargo", self.root) == "managed":
            if self.get_health_record()["ok"]:
                log(f"rust 已存在：{capture(['cargo', '--version'])}")
                return
            # 受管 rustup 已在，但工程钉的 channel 没装：只补工具链，不重下 rustup。
            rustup = p.cargo_bin / ("rustup.exe" if PLATFORM.is_windows else "rustup")
            if rustup.is_file():
                log(f"补装 rust 工具链 {want}（受管 rustup 已在）")
                env = {**os.environ, "RUSTUP_HOME": str(p.rustup_home), "CARGO_HOME": str(p.cargo_home)}
                r = run([str(rustup), "toolchain", "install", want], env=env)
                if r.returncode == 0 and self.get_health_record()["ok"]:
                    log(f"rust 工具链就绪：{want}")
                    return
        p.rust.mkdir(parents=True, exist_ok=True)
        picked = self.select_source(m)
        env = {
            **os.environ,
            "RUSTUP_HOME": str(p.rustup_home),
            "CARGO_HOME": str(p.cargo_home),
        }
        if picked.get("dist_server"):
            env["RUSTUP_DIST_SERVER"] = picked["dist_server"]
        if picked.get("update_root"):
            env["RUSTUP_UPDATE_ROOT"] = picked["update_root"]
        args = ["-y", "--no-modify-path", "--default-toolchain", want]
        # 两个平台统一走 archive 的 rustup-init 二进制：Windows 是 .exe，Linux 是可执行
        # 文件。不再执行 sh.rustup.rs 的 shell 脚本——那种"下载来直接跑"的路径没法钉哈希。
        init_url, want_sha = self.rustup_installer(picked)
        # 防御性二次断言：rustup_installer 已经在缺哈希时抛错，这里再挡一道——
        # 任何分支都不得把未校验的可执行文件直接跑起来。
        if not want_sha:
            raise RuntimeError("rustup-init 缺 sha256：拒绝执行未校验的安装器")
        init = p.download_path("rustup-init.exe" if PLATFORM.is_windows else "rustup-init")
        log(f"安装 rustup + {want} 到 {p.rust}（installer={init_url}，sha256 校验）")
        download(init_url, init, want_sha256=want_sha, connect_timeout=10, max_time=120)
        if not PLATFORM.is_windows:
            init.chmod(0o755)
        r = run([str(init), *args], env=env)
        if r.returncode != 0 or self.detect_source("cargo", self.root) != "managed":
            raise RuntimeError("rustup 安装失败（见上方输出）")
        log(f"rust 已安装：{capture(['cargo', '--version'])}")

    def get_health_record(self) -> dict:
        """以工程钉的 channel 为准（清单版本仅兜底），实测须精确等于该 channel。"""
        req = self.get_channel() or DepsManifest.split_expr(self.m.get("rust", "version", default=""))[1]
        v = self.detect_version(self.root)
        src = self.find_executable()[1] if v else "none"
        return {"tool": "rust", "require": req, "actual": v or "缺", "source": src, "ok": bool(v) and v == req}


class MsvcTool(ToolBase):
    """Windows 的 MSVC（VS 2022 / VS 2022 Build Tools）。

    为什么当成"组件"来管：Qt 的 aqt arch 必须与机器上真实的编译器配套
    （MSVC 只能配 win64_msvc2022_64）。所以顺序是"先定编译器 ABI，再定装哪份 Qt"。
    没有 VS 的机器用 winget 装只含 C++ 生成工具的最小 VS，用户不用去官网点安装器。
    """

    name = "msvc"
    manifest = ToolManifest(name="msvc", version=">=14.0")  # VS2015 起的 VC 工具集都是 14.x
    check_order = 1.5

    def applicable(self) -> bool:
        return PLATFORM.is_windows

    @staticmethod
    def _vc_version(vs: Path) -> str | None:
        """VS 实例的 VC 工具集版本：VC\\Auxiliary\\Build\\Microsoft.VCToolsVersion.default.txt"""
        f = vs / "VC" / "Auxiliary" / "Build" / "Microsoft.VCToolsVersion.default.txt"
        try:
            txt = f.read_text(encoding="utf-8", errors="ignore").strip()
        except OSError:
            return None
        return txt or None

    def _find(self) -> tuple[Path, str] | None:
        for vs in _win_vs_installs():
            v = self._vc_version(vs)
            if v:
                return vs, v
        return None

    def detect_version(self, root: Path | None = None) -> str | None:
        hit = self._find()
        return hit[1] if hit else None

    def resolve_path(self) -> str | None:
        hit = self._find()
        return str(hit[0]) if hit else None

    def get_health_record(self) -> dict:
        """来源固定 system：MSVC 只可能来自系统装的 VS，我们不往 .ps-deps 里装编译器。"""
        expr = self.m.get("msvc", "version", default="")
        v = self.get_installed_version()
        return {
            "tool": self.name,
            "require": expr,
            "actual": v or "缺",
            "source": "system" if v else "none",
            "ok": DepsManifest.test_version(v, expr),
        }

    def install(self) -> None:
        """winget 装 VS 2022 Build Tools 最小负载（MSVC + Windows SDK + 自带 CMake/Ninja）。"""
        if not PLATFORM.is_windows:
            raise RuntimeError("msvc 只在 Windows 上有意义")
        winget = shutil.which("winget")
        if not winget:
            raise RuntimeError(
                "缺 winget，无法自动装 MSVC；手工装 VS 2022 Build Tools（勾“使用 C++ 的桌面开发”）："
                "https://aka.ms/vs/17/release/vs_buildtools.exe"
            )
        log("winget 安装 VS 2022 Build Tools（MSVC + Windows SDK，约 1-2 GB，视网络 5-15 分钟）...")
        r = run(
            [
                winget,
                "install",
                "--id",
                "Microsoft.VisualStudio.2022.BuildTools",
                "-e",
                "--accept-package-agreements",
                "--accept-source-agreements",
                "--silent",
                "--override",
                "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended",
            ]
        )
        if r.returncode != 0:
            raise RuntimeError(
                f"VS Build Tools 安装失败（winget 返回 {r.returncode}）；"
                "手工装法见 https://aka.ms/vs/17/release/vs_buildtools.exe"
            )
        if not self.get_installed_version():
            raise RuntimeError("winget 报告成功但找不到 VC 工具集；装完请新开终端（环境未刷新）后重跑")
        log(f"MSVC {self.get_installed_version()} 已就位：{self.resolve_path()}")


class QtTool(ToolBase):
    name = "qt"
    manifest = ToolManifest(
        name="qt",
        version=">=6.8.3",
        # aqt 的 arch 名随平台变：Linux 是 linux_gcc_64（6.8 起，落盘目录仍是 gcc_64），
        # Windows 是 win64_msvc2022_64。写成映射而不是单值，否则 Windows 会拿 Linux 名去装。
        # 版本写下限：install 取 6.8.3 去装，外部自备 Qt（实测 6.12）只要不低于它就算过。
        # ABI 不在这里存第二份：aqt 的 arch 名只由 ps_setup_platform 的 qt_abi_default()
        # 提供（Windows 是 win64_msvc2022_64，Linux 是 linux_gcc_64），双份常量必然漂移。
        urls={
            # Qt 官方在线安装源（aqt 拉 metadata/包）
            "qt": {
                "official": "https://download.qt.io",
                "mirrors": ["https://mirrors.aliyun.com/qt"],
            },
            # aqtinstall 本身从 pypi 装。
            "pypi": {
                "official": "https://pypi.org/simple",
                "mirrors": [
                    "https://mirrors.aliyun.com/pypi/simple/",
                    "https://pypi.tuna.tsinghua.edu.cn/simple",
                ],
            },
        },
    )
    check_order = 6

    def get_qt_dir(self) -> Path | None:
        """abi 只喂 aqt（6.8 起 linux_gcc_64），落盘目录名未必跟随，
        按「版本目录下含 Qt6Config.cmake 的子目录」扫描，对命名漂移免疫。"""
        ver = DepsManifest.split_expr(self.m.get("qt", "version", default=""))[1]
        for base in (self.p.qt, REPO.qt):
            vdir = base / ver
            if not vdir.is_dir():
                continue
            for d in sorted(vdir.iterdir()):
                if (d / "lib" / "cmake" / "Qt6" / "Qt6Config.cmake").is_file():
                    return d
        return None

    def get_installed_version(self) -> str | None:
        """返回扫描到的 Qt 目录；Qt 没有版本号命令，用目录存在与否判安装。"""
        q = self.get_qt_dir()
        return str(q) if q else None

    def abi_name(self) -> str:
        """aqt 的 arch 名 = 与机器上真实编译器配套的那份 Qt。

        Linux 固定 linux_gcc_64；Windows 按实际编译器走：有 MSVC 就用
        win64_msvc2022_64，没有就报"先装 msvc"，绝不静默装一份 ABI 不配套的
        Qt（这种包能下载成功，但链接期才炸成 LNK2038）。PS_QT_ABI 可显式覆盖。
        """
        override = os.environ.get("PS_QT_ABI", "").strip()
        if override:
            return override
        if PLATFORM.is_windows and not MsvcTool(self.m, self.root).get_installed_version():
            raise RuntimeError(
                "没找到 MSVC：Windows 的 Qt 预编译包必须与编译器 ABI 配套。\n"
                "先装编译器：python tools/ps-setup.py install msvc"
                "（用 winget 装 VS 2022 Build Tools 最小负载），或手工装 VS 后重跑。"
            )
        return PLATFORM.qt_abi_default()

    def install(self) -> None:
        """用 aqt 装 Qt。失败要清掉半截目录，否则下次会误判成已装。
        成功后的 env.sh 刷新由编排器统一做（组件间副作用显式化，不在组件里藏）。"""
        p, m = self.p, self.m
        qver = DepsManifest.split_expr(m.get("qt", "version", default=""))[1]
        host = PLATFORM.name
        abi = self.abi_name()
        if host == "windows" and not abi.startswith("win"):
            raise RuntimeError(f"qt.abi={abi} 不是 Windows 的 aqt arch 名（可用 PS_QT_ABI 覆盖，如 win64_msvc2022_64）")
        archives = m.get("qt", "archives", default=["qtbase", "qtsvg", "qttools", "icu"])
        if existing := self.get_qt_dir():
            log(f"Qt {qver} 已存在：{existing}")
            return
        if not shutil.which("aqt"):
            if (p.venv_bin / f"pip{p.exe}").is_file():
                PipInstaller.install(m, "aqtinstall")
            if not shutil.which("aqt"):
                raise RuntimeError("缺 aqt：先跑 install-python（venv 里装 aqtinstall）")

        qt_root = p.qt
        qt_root.mkdir(parents=True, exist_ok=True)
        qt_src = m.sources("qt")
        for base in [*qt_src.get("mirrors", []), qt_src.get("official", ""), ""]:
            log(f"尝试 Qt 源：{base or '官方'}")
            cmd = ["aqt", "install-qt", host, "desktop", qver, abi, "-O", qt_root]
            for a in archives:
                cmd += ["--archives", a]
            if base:
                cmd += ["--base", base]
            r = run(cmd)
            # 成功判据 = 退出码 0 且 Qt6Config.cmake 真的落盘：
            # aqt 偶发下载中断却静默 exit 0，残留半个目录会被当成“已安装”（实测踩过）。
            if r.returncode == 0 and self.get_qt_dir():
                break
            log(f"该源不可用或安装不完整（exit={r.returncode}），清理残留后换下一个")
            shutil.rmtree(qt_root / qver, ignore_errors=True)
        else:
            raise RuntimeError("Qt 下载失败（检查网络与 aqt 源）")
        installed_dir = self.get_qt_dir()
        if not installed_dir:
            raise RuntimeError(f"Qt 安装后仍找不到 {qver}/{abi}")
        log(f"Qt 已安装：{installed_dir}")

    def uninstall(self) -> None:
        ver = DepsManifest.split_expr(self.m.get("qt", "version", default=""))[1]
        self._remove([self.p.qt / ver])

    @staticmethod
    def _is_prefix(d: Path) -> bool:
        """认前缀的唯一判据：含 Qt6 的 CMake 包配置。目录名/版本号一律不作数。"""
        return (d / "lib" / "cmake" / "Qt6" / "Qt6Config.cmake").is_file()

    @staticmethod
    def _prefix_version(qdir: Path) -> str | None:
        """从 Qt 的 CMake 包里抠版本。Qt6 把版本号写在 Qt6Core 子目录的
        Qt6CoreConfigVersionImpl.cmake（set(PACKAGE_VERSION "6.12.0")），
        不在 lib/cmake/Qt6/Qt6ConfigVersion.cmake——读错路径会一直"缺版本"。"""
        for f in (
            qdir / "lib" / "cmake" / "Qt6Core" / "Qt6CoreConfigVersionImpl.cmake",
            qdir / "lib" / "cmake" / "Qt6Core" / "Qt6CoreConfigVersion.cmake",
            qdir / "lib" / "cmake" / "Qt6" / "Qt6ConfigVersion.cmake",
        ):
            if not f.is_file():
                continue
            txt = f.read_text(encoding="utf-8", errors="replace")
            m = re.search(r'(?:set\()?PACKAGE_VERSION\s+"([^"]+)"', txt)
            if m:
                return m.group(1)
        return None

    @staticmethod
    def _scan_roots() -> list[Path]:
        """已知安装根：只扫这些顶层目录的两级子目录（版本/架构），不遍历整盘。
        Windows 上软件落点天然分散，但也别为了发现去扫 C 盘——扫盘一次几十秒，
        用户还得怀疑为什么 configure 卡住。"""
        if PLATFORM.is_windows:
            env = os.environ
            roots = [Path("C:/Qt"), Path("D:/Qt"), Path("E:/Qt")]
            for var in ("ProgramFiles", "ProgramFiles(x86)", "USERPROFILE"):
                if env.get(var):
                    roots.append(Path(env[var]) / "Qt")
            if env.get("LOCALAPPDATA"):
                roots.append(Path(env["LOCALAPPDATA"]) / "Programs" / "Qt")
            # 自定义安装目录（实测 D:\program\Qt 这种很常见）：只看盘符下一层的
            # "<任意目录>/Qt"，不递归。列一层目录是毫秒级，扫盘才会几十秒。
            for drive in ("C:/", "D:/", "E:/"):
                try:
                    for child in Path(drive).iterdir():
                        if child.is_dir() and (child / "Qt").is_dir():
                            roots.append(child / "Qt")
                except (OSError, PermissionError):
                    continue
            return roots
        home = Path(os.environ.get("HOME") or Path.home())
        return [home / "Qt", Path("/opt/Qt")]

    @staticmethod
    def _msvc_build(qdir: Path) -> bool:
        """Windows 上只有 MSVC 构建的 Qt 能与 MSVC 编译器配套：import 库是 Qt6Core.lib。
        MinGW 构建只有 lib/libQt6Core.a，配 MSVC 能 configure 通过、链接期才炸
        （LNK2038），所以在发现阶段就排掉。非 Windows 一律算通过。"""
        if not PLATFORM.is_windows:
            return True
        return (qdir / "lib" / "Qt6Core.lib").is_file()

    def _scan_external(self) -> list[Path]:
        """在已知根里找所有合法 Qt 前缀：<root>/<版本>/<架构>/lib/cmake/Qt6。"""
        hits: list[Path] = []
        for root in self._scan_roots():
            if not root.is_dir():
                continue
            for ver_dir in sorted(root.iterdir()):
                if not ver_dir.is_dir():
                    continue
                for arch_dir in sorted(ver_dir.iterdir()):
                    if self._is_prefix(arch_dir):
                        hits.append(arch_dir)
        return hits

    def _scan_pkgmgr(self) -> list[Path]:
        """vcpkg 装出来的 Qt：installed/<triplet>/lib/cmake/Qt6。只读探测，不驱动包管理器。"""
        candidates = []
        if os.environ.get("VCPKG_ROOT"):
            candidates.append(Path(os.environ["VCPKG_ROOT"]))
        candidates += [Path("C:/vcpkg"), Path("D:/vcpkg"), Path(os.environ.get("HOME") or "") / "vcpkg"]
        hits: list[Path] = []
        for c in candidates:
            installed = c / "installed"
            if not installed.is_dir():
                continue
            for triplet in sorted(installed.iterdir()):
                if triplet.is_dir() and self._is_prefix(triplet):
                    hits.append(triplet)
        return hits

    def resolve_prefix(self) -> tuple[Path, str] | None:
        """Qt 前缀的唯一发现链，返回 (目录, 来源)。顺序固定，来源供 doctor/CMake 打印：
        cache/env（用户显式指定） → 受管前缀（install 装出来的） → 已知安装根 → 包管理器。"""
        for var, src in (("PS_QT_DIR", "env"), ("QTDIR", "env")):
            val = os.environ.get(var)
            if val and self._is_prefix(Path(val)):
                return Path(val), src
        for entry in os.environ.get("CMAKE_PREFIX_PATH", "").split(os.pathsep):
            if entry and self._is_prefix(Path(entry)):
                return Path(entry), "env"
        managed = self.get_qt_dir()
        if managed:
            return managed, ("managed" if managed.is_relative_to(self.root) else "repo-deps")
        # 多个扫描命中时取版本最高的：装了 6.8 和 6.12 就该用 6.12，而不是字典序第一个。
        scanned = self._scan_external() + self._scan_pkgmgr()
        if PLATFORM.is_windows:
            kept = [q for q in scanned if self._msvc_build(q)]
            if len(kept) != len(scanned):
                # 常见场景：机器上只有 Qt 在线安装器的 MinGW 组件。不猜、不硬配，
                # 直接当没装——后续 install 会装一份 MSVC 版。
                log(f"忽略 {len(scanned) - len(kept)} 份非 MSVC 构建的 Qt（与 MSVC 编译器 ABI 不配套）")
            scanned = kept
        if scanned:

            def key(p: Path) -> tuple:
                return (DepsManifest.parse_version(self._prefix_version(p) or "0"), str(p))

            best = max(scanned, key=key)
            return best, "scan"
        return None

    def resolve_path(self) -> str | None:
        hit = self.resolve_prefix()
        return str(hit[0]) if hit else None

    def get_health_record(self) -> dict:
        """受管 Qt 按钉版判（我们装的，必须确定）；外部自备 Qt 按同一条版本表达式判——
        manifest 写的是 `>=6.8.3`，所以 6.12 这种更新的合法安装不会被判死。"""
        expr = self.m.get("qt", "version", default="")
        qver = DepsManifest.split_expr(expr)[1]
        hit = self.resolve_prefix()
        if hit is None:
            return {"tool": "qt", "require": expr, "actual": "缺", "source": "none", "ok": False}
        qdir, src = hit
        if src in ("managed", "repo-deps"):
            return {"tool": "qt", "require": qver, "actual": qver, "source": src, "ok": True}
        v = self._prefix_version(qdir)
        return {
            "tool": "qt",
            "require": expr,
            "actual": v or "缺版本",
            "source": src,
            "ok": DepsManifest.test_version(v, expr),
        }


class SystemTool(ToolBase):
    """系统包层的下载源（apt），不体检、不安装——系统包由人工按提示装。

    manifest 只用于给清单导出提供 apt 源默认值；本类不进 INSTALL_ORDER，
    也不体检（check_order=-1），gcc/ninja/ccache 缺了是提示用户 apt-get，不走本类安装。
    """

    name = "system"
    check_order = -1  # 不体检：系统组件不归本内核装
    manifest = ToolManifest(
        name="system",
        urls={
            "apt": {
                "official": "http://archive.ubuntu.com/ubuntu",
                "mirrors": [
                    "https://mirrors.ustc.edu.cn/ubuntu",
                    "https://mirrors.aliyun.com/ubuntu",
                ],
            }
        },
    )


# ---------- 注册表与编排 ----------

TOOL_CLASSES = (GccTool, CmakeTool, NinjaTool, PythonTool, CcacheTool, RustTool, MsvcTool, QtTool, SystemTool)
# 全量编排顺序：先编译器 ABI（msvc）再 Qt——Windows 上选哪份 Qt 由实际编译器决定。
INSTALL_ORDER = ("python", "cmake", "ninja", "ccache", "msvc", "rust", "qt")

# 汇总后的 key 顺序即对外可见顺序（体检表/文档按它排，勿随意调整）。
MANIFEST_ORDER = ("gcc", "cmake", "ninja", "ccache", "python", "msvc", "rust", "qt")
SOURCE_ORDER = ("pypi", "python", "github", "qt", "rust", "apt")


def default_manifest() -> dict:
    """汇总内置默认清单：toolchains 取各 Tool.manifest.spec()，sources 取各 manifest.urls。

    版本/sha256/平台文件名与下载源都收在各类持有的同构 ToolManifest 里（改工具只动一处），
    这里只做汇总：DepsManifest 的唯一来源就是它，没有外部文件可覆盖。
    """
    specs = {t.name: t.manifest.spec() for t in TOOL_CLASSES}
    toolchains = {name: json.loads(json.dumps(specs[name])) for name in MANIFEST_ORDER if name in specs}
    sources: dict = {}
    for tool in TOOL_CLASSES:
        for kind, cfg in tool.manifest.urls.items():
            fresh = json.loads(json.dumps(cfg))
            sources[kind] = DepsManifest.merge_dicts(sources[kind], fresh) if kind in sources else fresh
    ordered = {name: sources[name] for name in SOURCE_ORDER if name in sources}
    return {"toolchains": toolchains, "sources": ordered}


# ---------- 体检汇总与表格 ----------


class CommandRunner:
    """命令编排层：体检 / 安装 / 卸载 / 引导 / 系统依赖 / 表格渲染。
    组件实现在各 XXTool 类，这里只负责流程和跨组件副作用（如装完 Qt 刷新 env）。"""

    @staticmethod
    def build_tools(m: DepsManifest, root: Path) -> dict[str, ToolBase]:
        """按注册表实例化全部工具，返回 {工具名: 实例}。"""
        return {t.name: t(m, root) for t in TOOL_CLASSES}

    @staticmethod
    def check(m: DepsManifest, root: Path, only: str | None = None) -> tuple[list[dict], bool]:
        """逐项体检，返回 (records, all_ok)。record 的字段就是 --json 的输出。
        only 指定单项时只出该行（git-hook 除外）。"""
        tools = CommandRunner.build_tools(m, root)
        ordered = sorted(
            (t for t in tools.values() if t.check_order >= 0 and t.applicable()), key=lambda t: t.check_order
        )
        rows = [t.get_health_record() for t in ordered]
        hook_ok, hook_cur = EntryPointMounter.check_git_hooks()
        rows.append({"tool": "git-hook", "require": ".githooks", "actual": hook_cur, "source": "local", "ok": hook_ok})
        if only:
            if only == "git-hook":
                rows = [r for r in rows if r["tool"] == "git-hook"]
            elif only in tools:
                rows = [r for r in rows if r["tool"] == only]
            else:
                raise RuntimeError(f"未知组件 {only}（可选：{'/'.join(tools)} 或 git-hook）")
        return rows, all(r["ok"] for r in rows)

    @staticmethod
    def resolve(m: DepsManifest, root: Path, name: str) -> dict:
        """单个组件的解析记录（体检 + 路径）。CMake 门禁在 configure 期消费它，
        --json 输出必须纯净：不带日志行、不带表格。"""
        tools = CommandRunner.build_tools(m, root)
        if name == "git-hook":
            ok, cur = EntryPointMounter.check_git_hooks()
            return {"tool": "git-hook", "require": ".githooks", "actual": cur, "source": "local", "ok": ok, "path": ""}
        if name not in tools:
            raise RuntimeError(f"未知组件 {name}（可选：{'/'.join(tools)} 或 git-hook）")
        return tools[name].resolve()

    @staticmethod
    def doctor(m: DepsManifest, root: Path) -> tuple[list[dict], bool]:
        """自检：逐项打印 实测 / 要求 / 来源 / 落点路径，失败项直接给可复制的修法。
        与 check 共用同一份发现（resolve -> resolve_prefix / which），不另写一套判定。"""
        tools = CommandRunner.build_tools(m, root)
        ordered = sorted(
            (t for t in tools.values() if t.check_order >= 0 and t.applicable()), key=lambda t: t.check_order
        )
        rows = [t.resolve() for t in ordered]
        return rows, all(r["ok"] for r in rows)

    @staticmethod
    def render_doctor(rows: list[dict]) -> str:
        """doctor 的文本版：先一张表，再逐项列落点路径，最后给失败项的修法。"""
        lines = [CommandRunner.render_table(rows), "", "  落点："]
        for r in rows:
            lines.append("    {:<8} {:<10} {}".format(r["tool"], f"({r['source']})", r["path"] or "（未找到）"))
        bad = [r for r in rows if not r["ok"]]
        if bad:
            lines += ["", "  修法："]
            for r in bad:
                name = r["tool"]
                if name == "qt":
                    fix = "python tools/ps-setup.py install qt   # 或 cmake -DPS_QT_DIR=<Qt 前缀（含 lib/cmake/Qt6）>"
                elif name == "rust":
                    fix = "python tools/ps-setup.py install rust   # 或 rustup toolchain install <钉住的版本>"
                elif name == "gcc":
                    fix = "sudo apt-get install -y build-essential   # gcc 归系统包，内核不代装"
                else:
                    fix = f"python tools/ps-setup.py install {name}"
                lines.append(f"    {name:<8} {fix}")
        return "\n".join(lines)

    @staticmethod
    def render_table(rows: list[dict]) -> str:
        """把体检记录渲染成对齐文本表。"""
        lines = ["  {:<8} {:<10} {:<12} {:<10} {}".format("工具", "实测", "要求", "来源", "结果")]
        for r in rows:
            lines.append(
                "  {:<8} {:<10} {:<12} {:<10} {}".format(
                    r["tool"], r["actual"], r["require"], r["source"], "✓" if r["ok"] else "✗"
                )
            )
        return "\n".join(lines)

    # ---- prepare：一次算出 CMake configure 需要的全部结论（JSON 交接）----

    @staticmethod
    def _ccache_hits(ccache: str) -> int:
        """ccache 的累计命中次数（直连命中 + 预处理后命中）；拿不到统计就当 0。"""
        out = capture([ccache, "--print-stats"])
        if not out:
            return 0
        hits = 0
        for key in ("direct_cache_hit", "preprocessed_cache_hit"):
            mm = re.search(key + r"\s+(\d+)", out)
            if mm:
                hits += int(mm.group(1))
        return hits

    @classmethod
    def _ccache_probe(cls, ccache: str, cc: str) -> dict:
        """用当前编译器实编两次验缓存：首次 CCACHE_RECACHE 强制写，第二次必须命中。

        只有 CMake 知道生成器最终选了哪个编译器，所以 cc 由门禁传进来。
        cl.exe 必须在 vcvars 环境里才能跑，configure 期没有那个环境，就如实标 skip
        （缓存目录与禁用开关仍照常校验），不假装通过。
        """
        if Path(cc).name.lower() in ("cl", "cl.exe", "clang-cl", "clang-cl.exe"):
            return {
                "probe": "skip",
                "hits_before": 0,
                "hits_after": 0,
                "reason": "cl.exe 需要 vcvars 环境，configure 期无法直接调用；命中情况留到首次编译验证",
            }
        before = cls._ccache_hits(ccache)
        with tempfile.TemporaryDirectory(prefix="ps-ccache-probe-") as tmp:
            src = Path(tmp) / "probe.c"
            src.write_text("int ps_ccache_probe(void) { return 0; }\n", encoding="utf-8")
            obj = Path(tmp) / ("probe.obj" if PLATFORM.is_windows else "probe.o")
            args = ["/c", f"/Fo{obj}"] if PLATFORM.is_windows else ["-c", "-o", str(obj)]
            env = dict(os.environ)
            env["CCACHE_RECACHE"] = "1"
            first = run([ccache, cc, *args, str(src)], capture_output=True, text=True, env=env)
            if first.returncode != 0:
                tail = (first.stderr or first.stdout).strip()[:300]
                return {
                    "probe": "fail",
                    "hits_before": before,
                    "hits_after": before,
                    "reason": f"用 {cc} 编译探针失败：{tail}",
                }
            second = run([ccache, cc, *args, str(src)], capture_output=True, text=True)
            if second.returncode != 0:
                tail = (second.stderr or second.stdout).strip()[:300]
                return {
                    "probe": "fail",
                    "hits_before": before,
                    "hits_after": before,
                    "reason": f"第二次编译探针失败：{tail}",
                }
        after = cls._ccache_hits(ccache)
        if after <= before:
            return {
                "probe": "fail",
                "hits_before": before,
                "hits_after": after,
                "reason": f"实测没有命中（hits {before} -> {after}）；查 CCACHE_DIR 权限与 CCACHE_DISABLE",
            }
        return {"probe": "ok", "hits_before": before, "hits_after": after, "reason": ""}

    @classmethod
    def _ccache_state(cls, root: Path, rows: list[dict], cc: str | None) -> dict:
        """ccache 可用性结论：找得到 + 缓存目录可写 + 没被 CCACHE_DISABLE 关掉 + 有编译器时实编命中。"""
        p = EnvPathLayout(root)
        path = next((r["path"] for r in rows if r["tool"] == "ccache" and r["ok"] and r["path"]), "")
        state = {
            "ok": False,
            "path": path,
            "dir": str(p.ccache_conf_dir),
            "probe": "skip",
            "hits_before": 0,
            "hits_after": 0,
            "reason": "",
        }
        if not path:
            state["reason"] = "未找到 ccache（python tools/ps-setup.py install ccache）"
            return state
        disable = os.environ.get("CCACHE_DISABLE", "")
        if disable and disable != "0":
            state["reason"] = f"CCACHE_DISABLE={disable}，缓存被显式关闭"
            return state
        try:
            p.ccache_conf_dir.mkdir(parents=True, exist_ok=True)
            probe_file = p.ccache_conf_dir / ".ps-write-probe"
            probe_file.write_text("ok", encoding="utf-8")
            probe_file.unlink()
        except OSError as e:
            state["reason"] = f"CCACHE_DIR 不可写（{p.ccache_conf_dir}）：{e}"
            return state
        state["ok"] = True
        if cc:
            state.update(cls._ccache_probe(path, cc))
            if state["probe"] == "fail":
                state["ok"] = False
        return state

    @classmethod
    def prepare(
        cls,
        m: DepsManifest,
        root: Path,
        cc: str | None = None,
        allow_install: bool = True,
        qt_from_cli: bool = False,
    ) -> dict:
        """configure 期的唯一入口：体检 → 缺件补装 → 复查 → 算环境变量 → Qt → hooks → ccache。

        判定口径与 doctor 完全一致（同一个 resolve），这里只是把 doctor / install / resolve /
        mount 串成一次调用，并把结果整理成 CMake 能直接落地的形状——CMake 只读这份 JSON，
        不再自己扫盘、读 git config 或各装各的组件。
        """
        EnvPathLayout(root).mkdirs()
        tools = cls.build_tools(m, root)
        ordered = sorted(
            (t for t in tools.values() if t.check_order >= 0 and t.applicable()), key=lambda t: t.check_order
        )
        messages: list[str] = []
        warnings: list[str] = []
        installed: list[str] = []
        override = os.environ.get("RUSTUP_TOOLCHAIN", "")

        def scan() -> list[dict]:
            recs = [t.resolve() for t in ordered]
            if override:
                # 显式 RUSTUP_TOOLCHAIN 与 src/CMakeLists 的"覆盖即跳过钉版校验"同一口径，
                # 否则每次 configure 都会去下钉住的那个版本。
                for rec in recs:
                    if rec["tool"] == "rust" and not rec["ok"]:
                        rec["ok"] = True
                        rec["note"] = f"RUSTUP_TOOLCHAIN={override} 显式覆盖，跳过钉版校验"
            return recs

        rows = scan()
        missing = [r["tool"] for r in rows if not r["ok"]]
        if missing and allow_install:
            messages.append(f"缺少 [{', '.join(missing)}]，按 INSTALL_ORDER 自动安装")
            fails = cls.install_tools(m, root, names=missing)
            if fails:
                warnings.append(f"{fails} 个组件安装失败（日志见上），安装后仍缺请按 doctor 的修法补齐")
            installed = list(missing)
            EnvPathLayout(root).prepend_bin_to_path()
            rows = scan()
            missing = [r["tool"] for r in rows if not r["ok"]]
        if missing:
            messages.append(f"仍有缺失：{', '.join(missing)}（python tools/ps-setup.py doctor 看修法）")

        hook_ok, hook_cur = EntryPointMounter.check_git_hooks()
        hook_action = "already"
        if not hook_ok:
            try:
                EntryPointMounter.mount_hooks(root)
                hook_action = "mounted"
            except Exception as e:
                messages.append(f"Git hooks 挂载失败：{e}")
            hook_ok, hook_cur = EntryPointMounter.check_git_hooks()
        if not hook_ok:
            messages.append("Git hooks 仍未就位（python tools/ps-setup.py mount）")

        qt_tool = cast(QtTool, tools["qt"])
        qt_row = next(
            (r for r in rows if r["tool"] == "qt"), {"ok": False, "path": "", "actual": "缺", "source": "none"}
        )
        abi = ""
        if qt_row["path"]:
            try:
                abi = qt_tool.abi_name()
            except RuntimeError as e:
                warnings.append(f"Qt ABI 无法确定：{e}")
        qt: dict[str, Any] = {
            "ok": qt_row["ok"],
            "path": qt_row["path"],
            "actual": qt_row["actual"],
            "source": qt_row["source"],
            "abi": abi,
        }
        if qt_from_cli and qt["path"]:
            # 前缀由命令行（-DPS_QT_DIR）给出：来源标 cache，CMake 侧据此不覆盖用户的 -D。
            qt["source"] = "cache"
            qt_row["source"] = "cache"
        if qt["path"] and cc:
            dirname = Path(qt["path"]).name.lower()
            msvc_cc = Path(cc).name.lower() in ("cl", "cl.exe", "clang-cl", "clang-cl.exe")
            if msvc_cc and "msvc" not in dirname:
                warnings.append(
                    f"Qt 前缀目录名 {dirname} 不像 MSVC ABI（期望 msvc*_64），而当前编译器是 MSVC，"
                    "链接期可能报 LNK2038/runtime mismatch；换一份 msvc 的 Qt：-DPS_QT_DIR=<…/msvc2022_64>"
                )
            elif not msvc_cc and "msvc" in dirname:
                warnings.append(
                    f"Qt 前缀目录名 {dirname} 是 MSVC ABI，但当前编译器不是 MSVC（同一份 Qt 不能跨 ABI 复用）"
                )

        p = EnvPathLayout(root)
        scanned: list[str] = []
        for rec in rows:
            if rec["ok"] and rec["source"] == "scan" and rec["tool"] != "qt" and rec["path"]:
                parent = str(Path(rec["path"]).parent)
                if parent not in scanned:
                    scanned.append(parent)
        head = [str(d) for d in (p.bin, p.venv_bin, p.pbs_bin, p.ccache_bin_dir, p.cargo_bin)] + scanned
        tail = [d for d in os.environ.get("PATH", "").split(os.pathsep) if d and d not in head]
        env = {
            "PS_DEPS_ROOT": str(root),
            "PATH": os.pathsep.join(head + tail),
            "CCACHE_DIR": str(p.ccache_conf_dir),
        }
        if qt["path"]:
            env["PS_QT_DIR"] = qt["path"]
            old_prefix = [d for d in os.environ.get("CMAKE_PREFIX_PATH", "").split(os.pathsep) if d and d != qt["path"]]
            env["CMAKE_PREFIX_PATH"] = os.pathsep.join([qt["path"], *old_prefix])
            if not PLATFORM.is_windows:
                lib = str(Path(qt["path"]) / "lib")
                old_ld = [d for d in os.environ.get("LD_LIBRARY_PATH", "").split(os.pathsep) if d and d != lib]
                env["LD_LIBRARY_PATH"] = os.pathsep.join([lib, *old_ld])
        for rec in rows:
            if rec["tool"] == "rust" and rec["source"] == "managed":
                env["RUSTUP_HOME"] = str(p.rustup_home)
                env["CARGO_HOME"] = str(p.cargo_home)

        ccache = cls._ccache_state(root, rows, cc)
        if ccache["reason"]:
            messages.append(f"ccache：{ccache['reason']}")
        elif ccache["probe"] == "ok":
            messages.append(
                f"ccache 实测命中：hits {ccache['hits_before']} -> {ccache['hits_after']}（CCACHE_DIR={ccache['dir']}）"
            )

        ok = all(r["ok"] for r in rows) and qt["ok"] and bool(qt["path"]) and hook_ok
        if ok and not missing:
            messages.insert(0, f"环境自检通过（{len(rows)} 项）")
        return {
            "ok": ok,
            "platform": "windows" if PLATFORM.is_windows else "linux",
            "ps_deps_root": str(root),
            "missing": missing,
            "installed": installed,
            "tools": rows,
            "env": env,
            "qt": qt,
            "git_hook": {"ok": hook_ok, "actual": hook_cur, "action": hook_action},
            "ccache": ccache,
            "messages": messages,
            "warnings": warnings,
        }

    @staticmethod
    def ensure_git() -> None:
        """缺 git 时按平台给安装指引；仅 Linux 有免密 sudo 时才代装。"""
        if shutil.which("git"):
            return
        if PLATFORM.is_windows:
            raise RuntimeError(
                "缺 git：winget install --id Git.Git 安装后重跑，或 https://git-scm.com/download/win 手动下载"
            )
        if not shutil.which("sudo") or run(["sudo", "-n", "true"]).returncode != 0:
            raise RuntimeError("缺 git 且无免密 sudo，先执行后重跑：sudo apt-get install -y git")
        r = run(["sudo", "-n", "apt-get", "install", "-y", "-qq", "git"])
        if r.returncode != 0:
            raise RuntimeError("git 安装失败，请手动执行：sudo apt-get install -y git")

    @staticmethod
    def install_system_deps() -> None:
        """Linux：Qt 6 编译运行所需 GL/X11/xcb 开发包，按缺失检测后补装（免密 sudo 才动手）。"""
        if PLATFORM.is_windows:
            return
        missing = []
        for p in APT_PACKAGES:
            r = run(["dpkg-query", "-W", "-f=${Status}", p], capture_output=True, text=True)
            if "install ok installed" not in r.stdout:
                missing.append(p)
        if not missing:
            log("系统 apt 依赖齐备")
            return
        if not shutil.which("sudo") or run(["sudo", "-n", "true"]).returncode != 0:
            raise RuntimeError(f"缺系统依赖 {missing}，先执行：sudo apt-get install -y {' '.join(missing)}")
        log(f"apt 安装：{' '.join(missing)}")
        r = run(["sudo", "-n", "env", "DEBIAN_FRONTEND=noninteractive", "apt-get", "install", "-y", "-qq", *missing])
        if r.returncode != 0:
            raise RuntimeError("apt 安装失败，按上方提示手动补齐")

    @classmethod
    def install_tools(
        cls, m: DepsManifest, root: Path, names: list[str] | None = None, skip_system: bool = False
    ) -> int:
        """按 INSTALL_ORDER 装组件；Qt 装完要刷新 env.sh，否则 PS_QT_DIR 会缺失。
        names=None 走全量；指定 names 时只装列出的组件（顺序仍按 INSTALL_ORDER）。"""
        tools = cls.build_tools(m, root)
        order = [n for n in INSTALL_ORDER if not names or n in names]
        unknown = [n for n in (names or []) if n not in tools]
        if unknown:
            raise RuntimeError(f"未知组件 {'/'.join(unknown)}（可选：{'/'.join(tools)}）")
        fails = 0
        if not PLATFORM.is_windows and not skip_system and os.environ.get("PS_SKIP_SYSTEM") != "1":
            try:
                cls.install_system_deps()
            except Exception as e:
                log(f"系统依赖：{e}（可手动补齐后重跑）")
                fails += 1
        for name in order:
            try:
                tools[name].install()
            except Exception as e:
                log(f"{name} 安装失败：{e}")
                fails += 1
        # 单项补装后 env.sh 可能是“无 Qt”时生成的，刷新让 PS_QT_DIR/CMAKE_PREFIX_PATH 跟上
        if (not names or "qt" in names) and QtTool(m, root).get_qt_dir() and (root / "env.sh").is_file():
            EnvFileWriter(m, root).write_sh()
        return fails

    @classmethod
    def install(cls, root: Path | None = None, skip_system: bool = False) -> None:
        """装齐系统依赖和全部组件，最后生成 env、注册 hooks 并复检。"""
        root = root or get_deps_root()
        m = DepsManifest()
        EnvPathLayout(root).mkdirs()
        fails = cls.install_tools(m, root, skip_system=skip_system)
        EnvFileWriter(m, root).write_sh()
        CcacheTool.write_conf(root)
        try:
            EntryPointMounter.mount_hooks()
        except Exception as e:
            log(f"hooks：{e}")
            fails += 1
        rows, ok = cls.check(m, root)
        print(cls.render_table(rows))
        if fails or not ok:
            raise RuntimeError(f"完成，但有 {fails or '体检未过'} 项失败：按上方提示补齐后重跑 install")
        EntryPointMounter.mount_all(root)
        log("完成。新开终端 / VSCode / Windows Terminal 即已就绪，可直接 cmake --preset default")

    @classmethod
    def bootstrap(cls, root: Path | None = None) -> None:
        """先备好 git 和受管 Python，再走全量安装。"""
        root = root or get_deps_root()
        log("bootstrap 第 1/2 步：git")
        cls.ensure_git()
        log("bootstrap 第 2/2 步：受管 Python + venv（pbs 独立构建，不碰系统 Python）")
        cls.install(root)

    @classmethod
    def uninstall(cls, m: DepsManifest, root: Path, names: list[str], force: bool) -> None:
        """只删 toolchain 和 bin 链接；env.sh、profile 挂载、git hooks 都留着。
        系统 Python / 系统 cmake。非交互场景必须 --force；交互场景给确认提示。"""
        tools = cls.build_tools(m, root)
        if names == ["all"]:
            names = [n for n in INSTALL_ORDER if tools[n].get_installed_version() is not None]
            if not names:
                log("没有已安装的受管组件可卸载")
                return
        unknown = [n for n in names if n not in tools]
        if unknown:
            raise RuntimeError(f"未知组件 {'/'.join(unknown)}（可选：{'/'.join(tools)} 或 all）")
        if not force:
            if not sys.stdin.isatty():
                raise RuntimeError(f"非交互卸载需显式 --force：uninstall {' '.join(names)} --force")
            r = input(f"确认卸载受管组件 {' '.join(names)}？（env.sh/挂载不受影响）[y/N] ")
            if r.strip().lower() not in ("y", "yes"):
                log("已取消")
                return
        for n in names:
            tools[n].uninstall()
        log("卸载完成。env.sh 可能含已卸载组件的路径，重跑 gen-env 刷新（挂载与系统组件不受影响）")


# ---------- env 生成 ----------


class EnvFileWriter:
    """env 文件生成：三平台共用的环境变量文本 + sh/ps1/bat 三个落盘入口。
    格式被门禁和各 shell 挂载点依赖，改动前先确认两边。"""

    def __init__(self, m: DepsManifest, root: Path):
        self.m = m
        self.root = root
        self.p = EnvPathLayout(root)

    @staticmethod
    def _find_vcvars64() -> Path | None:
        """找一份可用的 vcvars64.bat；VS 选择、路径拼接只在这里做一次。"""
        if not PLATFORM.is_windows:
            return None
        for vs in _win_vs_installs():
            candidate = vs / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
            if candidate.is_file():
                return candidate
        return None

    @staticmethod
    def _msvc_env() -> dict[str, str]:
        """Windows：运行 vcvars64 并取回它设置的完整环境（PATH / INCLUDE / LIB / LIBPATH 等）。
        Linux 没有 MSVC，返回空；找不到 VS 也返回空，由 check 报缺，不在生成 env 时抛异常。"""
        vcvars = EnvFileWriter._find_vcvars64()
        if vcvars is None:
            return {}
        try:
            # 直接给 cmd /c 传带引号的长命令会被 /s 的引号规则吃掉；落一个临时 .bat
            # 再执行，路径里有空格或括号也稳。
            with tempfile.NamedTemporaryFile("w", suffix=".bat", delete=False, encoding="mbcs", errors="replace") as fh:
                fh.write("@echo off\n")
                fh.write(f'call "{vcvars}" >nul 2>&1\n')
                fh.write("if errorlevel 1 exit /b 1\n")
                fh.write("set\n")
                bat = fh.name
            try:
                r = subprocess.run(
                    [os.environ.get("ComSpec", "cmd.exe"), "/d", "/c", bat],
                    capture_output=True,
                    text=True,
                    timeout=60,
                )
            finally:
                Path(bat).unlink(missing_ok=True)
        except (OSError, UnicodeError, subprocess.TimeoutExpired):
            return {}
        if r.returncode != 0:
            return {}
        env: dict[str, str] = {}
        for line in r.stdout.splitlines():
            if "=" not in line:
                continue
            k, v = line.split("=", 1)
            if k and not k.startswith("="):
                env[k] = v
        # vcvars 的 `set` 会把当前进程的整份环境都吐出来（CUA_*、PSModulePath 等）。
        # 只留编译器/链接器真正需要的键，避免 env 文件被无关变量污染。
        keep = {
            "PATH",
            "INCLUDE",
            "LIB",
            "LIBPATH",
            "VCINSTALLDIR",
            "VCTOOLSINSTALLDIR",
            "VCTOOLSVERSION",
            "VSINSTALLDIR",
            "WINDOWSSDKDIR",
            "WINDOWSSDKVERSION",
            "UCRTCONTENTROOT",
            "UNIVERSALCRTSDKDIR",
            "WINDOWSSDKBINPATH",
            "VSCMD_ARG_TGT_ARCH",
            "VSCMD_ARG_HOST_ARCH",
            "VSCMD_VER",
        }
        env = {k: v for k, v in env.items() if k.upper() in keep}
        path_key = next((k for k in env if k.lower() == "path"), "")
        if path_key:
            orig = [d for d in os.environ.get("PATH", "").split(os.pathsep) if d]
            parts = [d for d in env[path_key].split(os.pathsep) if d]
            # vcvars 是在现有 PATH 前面插入自己的一组目录；尾部若是原始 PATH，
            # 就去掉，避免 env 文件里再抄一遍整条系统 PATH。
            if orig and len(parts) > len(orig):
                tail = [d.lower() for d in parts[-len(orig) :]]
                if tail == [d.lower() for d in orig]:
                    parts = parts[: -len(orig)]
            env[path_key] = os.pathsep.join(parts)
        if path_key:
            return env
        return {}

    @staticmethod
    def _ps_quote(value: str) -> str:
        """PowerShell 单引号字面量：内部单引号写两遍。"""
        return "'" + value.replace("'", "''") + "'"

    def _qt_dir(self) -> Path | None:
        """受管或外部 Qt 前缀（get_qt_dir 只管受管目录，外部安装会漏）。
        生成 env 与 IDE 配置必须用 resolve_prefix，和 prepare 同一口径。"""
        hit = QtTool(self.m, self.root).resolve_prefix()
        return hit[0] if hit else None

    def build_rust_lines(self) -> list[str]:
        """只有受管 cargo 才覆盖 RUSTUP_HOME/CARGO_HOME，
        否则用系统 ~/.cargo（覆盖成空目录会让 rustup shim 找不到 toolchain）。"""
        p = self.p
        src = ToolBase.detect_source("cargo", self.root)
        if src == "managed":
            return [
                f'export RUSTUP_HOME="{p.rustup_home}"',
                f'export CARGO_HOME="{p.cargo_home}"',
                'export PATH="' + str(p.cargo_bin) + ':$PATH"',
            ]
        if src == "system":
            return ['export PATH="' + str(Path.home() / ".cargo" / "bin") + ':$PATH"']
        return []

    def build_env_body(self) -> str:
        """shell 要导出的环境变量文本。"""
        p, root = self.p, self.root
        lines = [f'export PS_DEPS_ROOT="{root}"']
        lines.append(f'export PATH="{p.bin}:{p.venv_bin}:{p.ccache_bin_dir}:$PATH"')
        qt = self._qt_dir()
        if qt:
            lines.append(f'export PS_QT_DIR="{qt}"')
            lines.append('export CMAKE_PREFIX_PATH="$PS_QT_DIR${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"')
            lines.append('export LD_LIBRARY_PATH="$PS_QT_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"')
        lines.append(f'export CCACHE_DIR="{p.ccache_cache}"')
        lines.extend(self.build_rust_lines())
        return "\n".join(lines)

    def write_sh(self) -> Path:
        """写 env.sh（临时文件 + 原子替换），同一 shell 重复 source 只在首次生效。"""
        root = self.root
        f = root / "env.sh"
        tmp = root / "env.sh.tmp"
        root.mkdir(parents=True, exist_ok=True)
        body = (
            f'# 由 tools/ps-setup.py 生成；非交互 shell 用 . "{f}" 引入。\n'
            "# 同一个 shell 里重复 source 只在首次生效，不层层追加 PATH。\n"
            'if [ -z "${PS_DEPS_ENV_LOADED:-}" ]; then\n' + self.build_env_body() + "\nexport PS_DEPS_ENV_LOADED=1\n"
            "fi\n"
        )
        tmp.write_text(body, encoding="utf-8")
        tmp.replace(f)
        return f

    def write_ps1(self) -> Path:
        """生成 Windows PowerShell 版 env，结构与 env.sh 一致。
        除受管工具链外，把 vcvars64 的编译器环境也一并落进来：project() 在
        DevEnvCheck 之前就要探测编译器，只准备 PATH/Qt 会在 configure 第一步失败。"""
        p, root = self.p, self.root
        qt = self._qt_dir()
        msvc = self._msvc_env()
        msvc_path = next((v for k, v in msvc.items() if k.lower() == "path"), "")
        f = root / "env.ps1"
        root.mkdir(parents=True, exist_ok=True)
        base_path = f"{p.bin};{p.venv_bin};{p.ccache_bin_dir}"
        lines = [
            "# 由 tools/ps-setup.py 生成；PowerShell 用 . <路径> 引入。",
            "# 同一个会话里重复加载只生效一次，不层层追加 PATH。",
            "if (-not $env:PS_DEPS_ENV_LOADED) {",
            f'  $env:PS_DEPS_ROOT = "{root}"',
            f"  $env:PATH = {self._ps_quote(base_path + (';' + msvc_path if msvc_path else ''))} + ';' + $env:PATH",
        ]
        for key, value in msvc.items():
            if key.lower() in ("path", "ps_deps_env_loaded"):
                continue
            lines.append(f"  $env:{key} = {self._ps_quote(value)}")
        if qt:
            lines += [
                f'  $env:PS_QT_DIR = "{qt}"',
                '  $env:CMAKE_PREFIX_PATH = "$env:PS_QT_DIR"',
                # 这里不写 LD_LIBRARY_PATH：Windows 的动态链接器不读它，且 Qt 的 DLL 在
                # bin 不在 lib。运行库由 cmake/RuntimeDeploy.cmake 的 windeployqt 负责。
            ]
        lines += [
            f'  $env:CCACHE_DIR = "{p.ccache_cache}"',
            '  $env:PS_DEPS_ENV_LOADED = "1"',
            "}",
        ]
        f.write_text("\n".join(lines) + "\n", encoding="utf-8-sig")
        return f

    def write_bat(self) -> Path:
        """生成 cmd 版 env，给双击/批处理用。先 call vcvars64，再叠加受管 PATH；
        调用方在同一 cmd 里 call 本文件，之后直接跑 cmake。"""
        p, root = self.p, self.root
        qt = self._qt_dir()
        vcvars = EnvFileWriter._find_vcvars64()
        vcvars_arg = str(vcvars) if vcvars else ""
        lines = [
            "@echo off",
            "rem 由 tools/ps-setup.py 生成；cmd 用 call 引入。",
            "if defined PS_DEPS_ENV_LOADED goto :eof",
        ]
        if vcvars_arg:
            lines.append(f'call "{vcvars_arg}" >nul 2>&1')
        lines += [
            f'set "PS_DEPS_ROOT={root}"',
            f'set "PATH={p.bin};{p.venv_bin};{p.ccache_bin_dir};%PATH%"',
        ]
        if qt:
            lines += [f'set "PS_QT_DIR={qt}"', 'set "CMAKE_PREFIX_PATH=%PS_QT_DIR%"']
        lines += [f'set "CCACHE_DIR={p.ccache_cache}"', "set PS_DEPS_ENV_LOADED=1"]
        f = root / "env.bat"
        f.write_text("\n".join(lines) + "\r\n", encoding="utf-8")
        return f


# ---------- 系统依赖（Linux：Qt 6 编译运行所需）----------

APT_PACKAGES = [
    "build-essential",
    "libgl1-mesa-dev",
    "libegl1-mesa-dev",
    "libxkbcommon-dev",
    "libxcb-cursor0",
    "libxcb-icccm4",
    "libxcb-image0",
    "libxcb-keysyms1",
    "libxcb-randr0",
    "libxcb-render-util0",
    "libxcb-shape0",
    "libxcb-xinerama0",
    "libxkbcommon-x11-0",
    "libfontconfig1",
    "libdbus-1-3",
]


# ---------- P3：入口挂载（hooks / shell 启动文件 / WT fragment / .vscode）----------

MARK_BEGIN = "# >>> ps-setup env >>>"
MARK_END = "# <<< ps-setup env <<<"


class EntryPointMounter:
    """入口挂载：git hooks / shell 启动文件 / Windows Terminal fragment / .vscode。
    全部幂等、只追加可卸载，不覆盖用户已有配置。"""

    @staticmethod
    def check_git_hooks() -> tuple[bool, str]:
        """core.hooksPath 指向本仓库 .githooks 且 hook 可执行。"""
        try:
            inside = (
                subprocess.run(
                    ["git", "-C", str(REPO.root), "rev-parse", "--is-inside-work-tree"], capture_output=True, text=True
                ).stdout.strip()
                == "true"
            )
        except OSError:
            return True, "skipped"
        if not inside or not REPO.githooks.is_dir():
            return True, "skipped"  # 非工作树跳过（不报错），对齐 bash
        cur = ""
        try:
            cur = subprocess.run(
                ["git", "-C", str(REPO.root), "config", "--get", "core.hooksPath"], capture_output=True, text=True
            ).stdout.strip()
        except OSError:
            pass
        if not cur:
            return False, "未设置"
        cur_abs = Path(os.path.abspath(os.path.join(REPO.root, cur) if not os.path.isabs(cur) else cur))
        want = REPO.githooks.resolve()
        ok = (
            cur_abs.resolve() == want
            and (REPO.githooks / "pre-commit").is_file()
            and (REPO.githooks / "pre-push").is_file()
        )
        return ok, cur

    @staticmethod
    def in_git_worktree() -> bool:
        """判断仓库根是否在 git 工作树内（挂载 hooks 的前置条件）。"""
        try:
            r = run(["git", "-C", str(REPO.root), "rev-parse", "--is-inside-work-tree"], capture_output=True, text=True)
            return r.stdout.strip() == "true"
        except OSError:
            return False

    @classmethod
    def mount_hooks(cls, root: Path | None = None) -> bool:
        """把 core.hooksPath 指向仓库 .githooks，并保证 hook 可执行。"""
        if not REPO.git.exists() and not cls.in_git_worktree():
            log("非 Git 工作树，跳过 Git hooks 安装")
            return True
        if not REPO.githooks.is_dir():
            log("未发现 .githooks（独立环境安装），跳过 Git hooks 安装")
            return True
        for h in ("pre-commit", "pre-push"):
            if not (REPO.githooks / h).is_file():
                raise RuntimeError(f"缺 hook 文件：{REPO.githooks / h}")
        r = run(
            ["git", "-C", str(REPO.root), "config", "--local", "core.hooksPath", ".githooks"],
            capture_output=True,
            text=True,
        )
        if r.returncode != 0:
            raise RuntimeError(f"git config core.hooksPath 设置失败：{r.stderr.strip()}")
        (REPO.githooks / "pre-commit").chmod(0o755)
        (REPO.githooks / "pre-push").chmod(0o755)
        log("已注册 Git hooks：core.hooksPath=.githooks")
        return True

    @staticmethod
    def build_marker_block(posix: bool, root: Path) -> str:
        """标记块内用实际 PS_DEPS_ROOT 展开（根路径可覆盖原则，不硬编码 HOME）。"""
        if posix:
            f = root / "env.sh"
            return f'{MARK_BEGIN}\n[ -f "{f}" ] && . "{f}"\n{MARK_END}\n'
        f = root / "env.ps1"
        return f'{MARK_BEGIN}\nif (Test-Path "{f}") {{ . "{f}" }}\n{MARK_END}\n'

    @staticmethod
    def _powershell_profiles() -> list[Path]:
        """Windows PowerShell 与 pwsh 的 $PROFILE。不能只读进程里的 PROFILE：
        ps-setup 常由 cmd / 薄壳拉起，那里没有 PROFILE 变量。"""
        found: list[Path] = []
        if os.environ.get("PROFILE"):
            found.append(Path(os.environ["PROFILE"]))
        for exe in ("powershell.exe", "pwsh.exe"):
            path = shutil.which(exe)
            if not path:
                continue
            out = capture([path, "-NoProfile", "-Command", "$PROFILE"])
            if out:
                found.append(Path(out))
        if PLATFORM.is_windows and not found:
            # 兜底：Windows PowerShell 5.1 的固定位置（OneDrive 文档目录除外）。
            found.append(Path.home() / "Documents" / "WindowsPowerShell" / "Microsoft.PowerShell_profile.ps1")
        seen: set[str] = set()
        out_list: list[Path] = []
        for f in found:
            key = str(f).lower()
            if key not in seen:
                seen.add(key)
                out_list.append(f)
        return out_list

    @classmethod
    def mount_profile(cls, posix: bool | None = None, root: Path | None = None) -> list[str]:
        """往 shell 启动文件追加标记块（Linux .bashrc / Windows $PROFILE），重复调用不会叠加。
        只追加、可卸载，不覆盖用户已有配置。"""
        root = root or get_deps_root()
        posix = (not PLATFORM.is_windows) if posix is None else posix
        if posix:
            targets = [Path.home() / ".bashrc"]
            block = cls.build_marker_block(True, root)
            begin = MARK_BEGIN
        else:
            targets = cls._powershell_profiles()
            block = cls.build_marker_block(False, root)
            begin = "# >>> ps-setup env >>>"
        done = []
        for f in targets:
            f.parent.mkdir(parents=True, exist_ok=True)
            text = f.read_text(encoding="utf-8", errors="ignore") if f.is_file() else ""
            if begin in text:
                log(f"标记块已存在，跳过：{f}")
                done.append(str(f))
                continue
            with open(f, "a", encoding="utf-8") as fh:
                fh.write("\n" + block)
            log(f"已写入 {f}")
            done.append(str(f))
        return done

    @staticmethod
    def _wt_settings_paths() -> list[Path]:
        """Windows Terminal 的 settings.json：Store 版与 unpackaged 版都可能存在。"""
        base = Path(os.environ.get("LOCALAPPDATA", ""))
        return [
            p
            for p in (
                base / "Packages" / "Microsoft.WindowsTerminal_8wekyb3d8bbwe" / "LocalState" / "settings.json",
                base / "Microsoft" / "Windows Terminal" / "settings.json",
            )
            if p.is_file()
        ]

    @staticmethod
    def mount_wt_default(guid: str) -> None:
        """把 WT 的 defaultProfile 指到 PlotScope Dev，新开窗口直接进已备好环境的终端。
        只改这一个键，保留用户其它配置；解析失败就跳过，不覆盖整个 settings.json。"""
        for f in EntryPointMounter._wt_settings_paths():
            try:
                text = f.read_text(encoding="utf-8")
            except OSError as e:
                log(f"读 {f} 失败（{e}），跳过 WT 默认终端")
                continue
            m = re.search(r'"defaultProfile"\s*:\s*"([^"]+)"', text)
            if m:
                current = m.group(1)
                if current.lower() == guid.lower():
                    log(f"WT 默认终端已是 PlotScope Dev：{f}")
                else:
                    log(f"WT 已有默认终端（{current}），保留用户选择；可在设置里手动选 PlotScope Dev")
                continue
            new, n = re.subn(r"\{", '{\n    "defaultProfile": "' + guid + '",', text, count=1)
            if n != 1:
                log(f"{f} 未找到可替换的 defaultProfile，跳过")
                continue
            try:
                f.write_text(new, encoding="utf-8")
            except OSError as e:
                log(f"写 {f} 失败（{e}），跳过 WT 默认终端，可在设置里手动选 PlotScope Dev")
                continue
            log(f"WT 新窗口默认进入 PlotScope Dev：{f}")

    @staticmethod
    def mount_wt_fragment() -> str | None:
        """Windows Terminal fragments（官方挂载点，不碰用户 settings.json；未装 WT 静默降级）。
        profile 的 commandline 直接加载 env.ps1，不依赖用户是否启用了 PowerShell profile。"""
        if not PLATFORM.is_windows:
            return None  # Linux 无对应物，$PROFILE 已覆盖
        base = Path(os.environ.get("LOCALAPPDATA", "")) / "Microsoft" / "Windows Terminal" / "Fragments" / "PlotScope"
        # Store 版 WT 的 settings.json 在 Packages 下，unpackaged 版才在 Microsoft\Windows Terminal；
        # 不能只看后者目录是否存在，否则 Store 版会被误判成“未安装”。
        if not EntryPointMounter._wt_settings_paths() and shutil.which("wt.exe") is None:
            log("未检测到 Windows Terminal，跳过 fragment（$PROFILE 已覆盖 WT 标签页）")
            return None
        base.mkdir(parents=True, exist_ok=True)
        frag = base / "fragment.json"
        guid = "{3a6f2d4c-9b1e-5c7a-8d2f-0e4b6c8a1d3f}"
        env_ps1 = get_deps_root() / "env.ps1"
        profile = {
            "guid": guid,
            "name": "PlotScope Dev",
            "commandline": f'powershell.exe -NoLogo -NoExit -Command ". {EnvFileWriter._ps_quote(str(env_ps1))}"',
            "startingDirectory": str(REPO.root),
        }
        logo = REPO.logo
        if logo.is_file():  # 无图标资源就不写该键，别留 icon:null
            profile["icon"] = str(logo)
        frag.write_text(json.dumps({"profiles": [profile]}, ensure_ascii=False, indent=2), encoding="utf-8")
        log(f"Windows Terminal fragment：{frag}")
        EntryPointMounter.mount_wt_default(guid)
        return str(frag)

    @staticmethod
    def _vscode_user_settings() -> Path:
        """VSCode 用户级 settings.json。写这里而不是仓库 .vscode/settings.json：
        后者是受版本控制的团队配置，不能塞进本机绝对路径和临时环境变量。"""
        if PLATFORM.is_windows:
            return Path(os.environ.get("APPDATA", "")) / "Code" / "User" / "settings.json"
        return Path.home() / ".config" / "Code" / "User" / "settings.json"

    @classmethod
    def mount_vscode(cls) -> str:
        """在 VSCode 用户设置里注册一个默认终端 profile，打开终端即加载 env.ps1。

        只改受管键，幂等；不碰仓库里的 .vscode/settings.json。CMake Tools 由用户在
        已备好环境的终端里启动，或直接用终端跑 cmake；不再往用户设置里灌 MSVC
        全量环境，避免影响其它工作区。
        """
        f = cls._vscode_user_settings()
        profile_key = (
            "terminal.integrated.profiles.windows" if PLATFORM.is_windows else "terminal.integrated.profiles.linux"
        )
        default_key = (
            "terminal.integrated.defaultProfile.windows"
            if PLATFORM.is_windows
            else "terminal.integrated.defaultProfile.linux"
        )
        if PLATFORM.is_windows:
            # 用实际环境根，兼容 PS_DEPS_ROOT 自定义；单引号字面量避免路径注入。
            env_ps1 = get_deps_root() / "env.ps1"
            command = f"$p = {EnvFileWriter._ps_quote(str(env_ps1))}; if (Test-Path $p) {{ . $p }}"
            profile = {"path": "powershell.exe", "args": ["-NoLogo", "-NoExit", "-Command", command]}
        else:
            profile = {"path": "bash", "args": ["-l"]}
        cur: dict = {}
        if f.is_file():
            try:
                cur = json.loads(f.read_text(encoding="utf-8"))
                if not isinstance(cur, dict):
                    raise ValueError("顶层非对象")
            except (OSError, ValueError) as e:
                log(f"{f} 解析失败（{e}），跳过 VSCode 终端注册，请手动加 profile: PlotScope Dev")
                return str(f)
        profiles = cur.get(profile_key)
        if not isinstance(profiles, dict):
            profiles = {}
        profiles["PlotScope Dev"] = profile
        cur[profile_key] = profiles
        if not cur.get(default_key):
            cur[default_key] = "PlotScope Dev"
        f.parent.mkdir(parents=True, exist_ok=True)
        text = json.dumps(cur, ensure_ascii=False, indent=2) + "\n"
        if not f.is_file() or f.read_text(encoding="utf-8") != text:
            f.write_text(text, encoding="utf-8")
            log(f"VSCode 默认终端已注册：{f}")
        else:
            log(f"VSCode 配置已就位，跳过：{f}")
        return str(f)

    @classmethod
    def mount_all(cls, root: Path | None = None) -> None:
        """挂载各入口（shell 启动文件 / WT / .vscode），并生成对应平台的 env 文件。"""
        root = root or get_deps_root()
        EnvFileWriter(DepsManifest(), root).write_sh()
        if PLATFORM.is_windows:
            EnvFileWriter(DepsManifest(), root).write_ps1()
            EnvFileWriter(DepsManifest(), root).write_bat()
        cls.mount_profile()
        cls.mount_wt_fragment()
        cls.mount_vscode()
        log("挂载完成。新开终端 / VSCode 重开仓库即生效")


# ---------- 薄壳生成 ----------
# 薄壳里的版本常量从清单渲染，不手写，避免多处漏改。
# 改了版本跑：python3 tools/ps-setup.py gen-shell


SHELL_SH_TEMPLATE = """#!/usr/bin/env bash
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
  x86_64) FILE="{file_linux_x86}"; SHA="{sha_linux_x86}" ;;
  aarch64|arm64) FILE="{file_linux_a64}"; SHA="{sha_linux_a64}" ;;
  *) log "不支持的架构 $(uname -m)（受管 Qt/Python 只发布 x86_64/aarch64）"; exit 1 ;;
esac
PBS_DST="$PS_DEPS_ROOT/toolchain/python/pbs"
REL="{tag}/$FILE"
TMP="$(mktemp)"; OK=0
for M in {mirrors_bash}; do
  log "尝试 Python 源：$(echo "$M" | sed -E 's#https?://([^/]+).*#\\1#')"
  if curl -fsSL --connect-timeout 8 --max-time 600 --speed-limit 10240 --speed-time 30 \
    --retry 1 -o "$TMP" "${M%/}/$REL"; then
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
"""

SHELL_PS1_TEMPLATE = """# ps-setup.ps1 — Windows 薄壳，由 gen-shell 生成，勿手改。
# 找到 Python 就把参数原样交给 ps-setup.py。
# ExecutionPolicy 拦 .ps1 时：powershell -ExecutionPolicy Bypass -File tools\\ps-setup.ps1
# 或直接用 tools\\ps-setup.bat。
# 改 Python 版本后重跑：python3 tools/ps-setup.py gen-shell
$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$DepsRoot = if ($env:PS_DEPS_ROOT) { $env:PS_DEPS_ROOT } else { "$env:USERPROFILE\\.ps-deps" }
$ToolPy = Join-Path $RepoRoot "tools\\ps-setup.py"
$File = "{file_win_x86}"
$Sha = "{sha_win_x86}"
$Tag = "{tag}"

function Write-Log($msg) { Write-Host "[ps-setup] $msg" }

# venv 已就绪，直接进内核
$VenvPy = Join-Path $DepsRoot "toolchain\\python\\venv\\Scripts\\python.exe"
if (Test-Path $VenvPy) {
    & $VenvPy $ToolPy @args
    exit $LASTEXITCODE
}

# venv 没了但解释器还在，先用它进内核
$PbsDst = Join-Path $DepsRoot "toolchain\\python\\pbs"
$PbsPy = Join-Path $PbsDst "python\\python.exe"
if (Test-Path $PbsPy) {
    & $PbsPy $ToolPy @args
    exit $LASTEXITCODE
}

# 下载 pbs 解释器并解压；Win10 1803+ 自带 tar.exe，不用装第三方工具
$Rel = "$Tag/$File"
$Tmp = Join-Path $env:TEMP $File
$Got = $false
foreach ($M in ({mirrors_ps})) {
    $Url = $M.TrimEnd("/") + "/" + $Rel
    Write-Log "尝试 Python 源：$([uri]$Url | Select-Object -ExpandProperty Host)"
    try {
        Invoke-WebRequest -Uri $Url -OutFile $Tmp -UseBasicParsing -TimeoutSec 600
        $hash = (Get-FileHash $Tmp -Algorithm SHA256).Hash.ToLower()
        if ($hash -ne $Sha) { Write-Log "  sha256 不符，换下一源"; continue }
        $Got = $true
        break
    } catch { Write-Log "  失败：$($_.Exception.Message)" }
}
if (-not $Got) {
    Write-Log "受管 Python 下载失败（$File，期望 sha256=$Sha）"
    Write-Log "可手动下载 $Rel 解压到 $PbsDst"
    exit 1
}
New-Item -ItemType Directory -Force -Path $PbsDst | Out-Null
tar -xf $Tmp -C $PbsDst        # pbs 顶层自带 python/ 目录；Win10 1803+ 自带 tar.exe
Remove-Item $Tmp -Force
if (-not (Test-Path $PbsPy)) { Write-Log "Python 解压后不可执行，检查 $PbsDst"; exit 1 }
Write-Log "受管 Python 已就位：$PbsPy（sha256 校验通过）"
& $PbsPy $ToolPy @args
exit $LASTEXITCODE
"""


class ShellGenerator:
    """薄壳渲染：常量（pbs 文件名/sha256/镜像/tag）全部来自清单，人不再手改薄壳。"""

    @staticmethod
    def build_template_consts(m: DepsManifest) -> dict:
        """从清单抽出薄壳模板所需的 pbs 常量与镜像列表，按 sh/ps1 语法分别拼好。"""
        pbs = m.get("python", "pbs", default={}) or {}
        py_src = m.sources("python")
        mirrors = [*py_src.get("mirrors", []), py_src.get("official", "")]  # 官方直连兜底
        return {
            "tag": m.get("python", "pbs_build", default=""),
            "file_linux_x86": pbs.get("x86_64-unknown-linux-gnu", {}).get("file", ""),
            "sha_linux_x86": pbs.get("x86_64-unknown-linux-gnu", {}).get("sha256", ""),
            "file_linux_a64": pbs.get("aarch64-unknown-linux-gnu", {}).get("file", ""),
            "sha_linux_a64": pbs.get("aarch64-unknown-linux-gnu", {}).get("sha256", ""),
            "file_win_x86": pbs.get("x86_64-pc-windows-msvc", {}).get("file", ""),
            "sha_win_x86": pbs.get("x86_64-pc-windows-msvc", {}).get("sha256", ""),
            "mirrors_bash": " ".join(f'"{u}"' for u in mirrors),
            "mirrors_ps": ", ".join(f'"{u}"' for u in mirrors),  # PowerShell 数组须逗号分隔
        }

    @staticmethod
    def render(tpl: str, c: dict) -> str:
        """占位符替换（不用 str.format：ps1 模板含字面 { }，会误伤）。"""
        for k, v in c.items():
            tpl = tpl.replace("{" + k + "}", str(v))
        return tpl

    def generate(self, target: str | None = None, out_dir: Path | None = None) -> list[Path]:
        """渲染 ps-setup.sh / .ps1 到 tools/；pbs 常量缺失时拒绝生成。"""
        m = DepsManifest()
        c = self.build_template_consts(m)
        if not all([c["tag"], c["file_linux_x86"], c["file_win_x86"], c["sha_linux_x86"], c["sha_win_x86"]]):
            raise RuntimeError("清单缺 python.pbs 常量（pbs_build/文件名/sha256），无法生成薄壳")
        d = out_dir or REPO.tools
        targets: list[tuple[str, str]] = []
        if target in (None, "sh", "all"):
            targets.append(("ps-setup.sh", self.render(SHELL_SH_TEMPLATE, c)))
        if target in (None, "ps1", "all"):
            targets.append(("ps-setup.ps1", self.render(SHELL_PS1_TEMPLATE, c)))
        written = []
        for name, body in targets:
            f = d / name
            # Windows PowerShell 5.1 只有带 BOM 才按 UTF-8 读 .ps1。
            enc = "utf-8-sig" if name.endswith(".ps1") else "utf-8"
            f.write_text(body, encoding=enc)
            if name.endswith(".sh"):
                f.chmod(0o755)
            log(f"已生成 {f}")
            written.append(f)
        return written


# ---------- 入口 ----------

LEGACY_INSTALL_ALIASES = {
    "install-python": "python",
    "install-cmake": "cmake",
    "install-ninja": "ninja",
    "install-ccache": "ccache",
    "install-rust": "rust",
    "install-qt": "qt",
}


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="ps-setup", description="开发环境守卫内核（issue IKYGHE + Tool 化重构）")
    ap.add_argument(
        "command",
        nargs="?",
        default="check",
        choices=[
            "check",
            "resolve",
            "doctor",
            "prepare",
            "gen-env",
            "install",
            "uninstall",
            "bootstrap",
            "mount",
            "gen-shell",
            *LEGACY_INSTALL_ALIASES,
        ],
        help=(
            "check [name]：体检（默认）；resolve <name>：单项解析（给 CMake 门禁消费，含落点路径）；"
            "doctor：全量自检（表 + 路径 + 修法）；prepare：configure 门禁一次算完（JSON，见 --out）；"
            "gen-env：生成 env.sh/env.ps1/env.bat；"
            "install [name ...]：全量/指定组件安装；uninstall [name|all]：卸载受管组件；"
            "bootstrap：一键引导；mount：入口挂载；gen-shell [sh|ps1|all]：重生成薄壳常量；"
            "install-<name>：单项安装（旧写法兼容）"
        ),
    )
    ap.add_argument(
        "names",
        nargs="*",
        default=[],
        help="check/install/uninstall 的组件名，可给多个（如 cmake ninja ccache / qt / all）",
    )
    ap.add_argument("--json", action="store_true", help="以 JSON 输出体检结果（供门禁/CI 消费）")
    ap.add_argument("--force", action="store_true", help="uninstall：跳过确认")
    ap.add_argument("--skip-system", action="store_true", help="install：跳过系统 apt 依赖")
    ap.add_argument("--cc", default="", help="prepare：CMake 选中的 C 编译器路径（ccache 实编验收用）")
    ap.add_argument("--out", default="", help="prepare：把 JSON 结论写到这个文件（CMake 侧读文件，不怕日志串台）")
    ap.add_argument("--no-install", action="store_true", help="prepare：只体检不补装")
    ap.add_argument(
        "--root", default="", help="环境根；缺省按 $PS_DEPS_ROOT 或 ~/.ps-deps（门禁会把 -DPS_DEPS_ROOT 的值传进来）"
    )
    ap.add_argument(
        "--qt-dir", default="", help="Qt 前缀（等价 -DPS_QT_DIR：命令行给出的前缀按 cache 处理，不被自动发现覆盖）"
    )
    ap.add_argument(
        "--ccache-dir", default="", help="ccache 缓存目录（等价 -DCCACHE_DIR；缺省 $PS_DEPS_ROOT/caches/ccache）"
    )
    args = ap.parse_args(argv)

    # 旧写法兼容：install-python → install python
    names = list(args.names)
    cmd = args.command
    if cmd in LEGACY_INSTALL_ALIASES:
        names = [LEGACY_INSTALL_ALIASES[cmd]]
        cmd = "install"

    root = Path(args.root).expanduser() if args.root else get_deps_root()
    # -D 的下沉：命令行给出的值优先于环境变量，这里先落进本进程环境，
    # 后面的 EnvPathLayout / QtTool 就按"用户显式指定"处理，不用各写一套分支。
    if args.ccache_dir:
        os.environ["CCACHE_DIR"] = str(Path(args.ccache_dir).expanduser())
    if args.qt_dir:
        os.environ["PS_QT_DIR"] = str(Path(args.qt_dir).expanduser())
    EnvPathLayout(root).prepend_bin_to_path()  # 受管 bin 目录入进程 PATH，命令一律按名字解析
    m = DepsManifest()
    # --json / prepare 的输出必须纯净（供门禁/CI 消费），日志一律让路。
    if not ((args.json and cmd in ("check", "resolve", "doctor", "prepare")) or cmd == "prepare"):
        log(f"工具链版本：{m.source}")

    try:
        if cmd == "check":
            if len(names) > 1:
                raise RuntimeError(f"check 只接受一个组件名，实际给了 {' '.join(names)}")
            rows, ok = CommandRunner.check(m, root, only=names[0] if names else None)
            if args.json:
                print(json.dumps({"ok": ok, "ps_deps_root": str(root), "tools": rows}, ensure_ascii=False, indent=2))
            else:
                print(CommandRunner.render_table(rows))
            return 0 if ok else 1

        if cmd == "resolve":
            if len(names) != 1:
                raise RuntimeError("用法：resolve <name>（单个组件，如 qt / rust / cmake）")
            rec = CommandRunner.resolve(m, root, names[0])
            if args.json:
                print(json.dumps(rec, ensure_ascii=False, indent=2))
            else:
                print(f"{rec['tool']}: {rec['path'] or '（未找到）'}（{rec['source']}，{rec['actual']}）")
            return 0 if rec["ok"] else 1

        if cmd == "doctor":
            rows, ok = CommandRunner.doctor(m, root)
            if args.json:
                print(json.dumps({"ok": ok, "ps_deps_root": str(root), "tools": rows}, ensure_ascii=False, indent=2))
            else:
                print(CommandRunner.render_doctor(rows))
            return 0 if ok else 1

        if cmd == "prepare":
            # JSON 走 stdout 或 --out 文件；日志（含子进程输出）让到 stderr，两边不串。
            with log_to(sys.stderr):
                rep = CommandRunner.prepare(
                    m,
                    root,
                    cc=args.cc or None,
                    allow_install=not args.no_install,
                    qt_from_cli=bool(args.qt_dir),
                )
                text = json.dumps(rep, ensure_ascii=False, indent=2)
                if args.out:
                    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
                    Path(args.out).write_text(text + "\n", encoding="utf-8")
                    log(f"门禁结论：{args.out}")
                else:
                    print(text)
                    sys.stdout.flush()
            return 0 if rep["ok"] else 1

        if cmd == "gen-env":
            print(f"[ps-setup] 环境文件：{EnvFileWriter(m, root).write_sh()}")
            if PLATFORM.is_windows:
                print(f"[ps-setup] {EnvFileWriter(m, root).write_ps1()}")
                print(f"[ps-setup] {EnvFileWriter(m, root).write_bat()}")
            return 0

        if cmd == "uninstall":
            if not names:
                raise RuntimeError("用法：uninstall <name|all>（可选：python/cmake/ninja/ccache/rust/qt/all）")
            CommandRunner.uninstall(m, root, names, force=args.force)
            return 0

        if cmd == "install":
            EnvPathLayout(root).mkdirs()
            if names:
                fails = CommandRunner.install_tools(m, root, names=names, skip_system=args.skip_system)
                return 1 if fails else 0
            CommandRunner.install(root, skip_system=args.skip_system)
            return 0

        if cmd == "bootstrap":
            CommandRunner.bootstrap(root)
            return 0

        if cmd == "mount":
            EntryPointMounter.mount_all(root)
            return 0

        if cmd == "gen-shell":
            ShellGenerator().generate(names[0] if names else None)
            return 0

    except Exception as e:
        # 不只 RuntimeError：解压损坏包会抛 zipfile.BadZipFile、落盘失败抛 OSError，
        # 这些逃到顶层就是裸 traceback。统一成一行 [ps-setup]；KeyboardInterrupt 不受影响
        # （BaseException 不经过这里）。PS_SETUP_DEBUG=1 时仍重抛，保留完整栈供排查。
        print(f"[ps-setup] {type(e).__name__}: {e}", file=sys.stderr)
        if os.environ.get("PS_SETUP_DEBUG"):
            raise
        return 1

    return 2


if __name__ == "__main__":
    sys.exit(main())
