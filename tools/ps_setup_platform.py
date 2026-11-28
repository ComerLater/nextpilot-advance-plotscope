"""平台策略：Windows 与 Linux 的差异只写在这一个文件里。

ps-setup.py 负责"要哪些组件、怎么体检、怎么装"；
凡是"装在哪、叫什么名字、用什么路径、装哪个 ABI、怎么建受管入口"这类
随操作系统变化的事，一律问本模块的 PLATFORM 单例，不在工具类里散写
`if os.name == "nt"`。

两个实现：
    WindowsPlatform —— Program Files 扫描、VS 实例发现（含 Build Tools）、
                       受管入口用复制（无软链特权）、Qt 只认 MSVC ABI
    LinuxPlatform   —— 系统路径已在 PATH、受管入口用软链、Qt 用 gcc ABI
"""

from __future__ import annotations

import os
import platform
import shutil
import subprocess
from pathlib import Path


def machine_arch() -> str:
    """发布包用的架构名：x86_64 / aarch64。

    Windows 报 AMD64 / ARM64，Linux 报 x86_64 / aarch64；rustup 的 archive
    路径只认后一套写法，统一在这里翻译，工具类里不写 arch 判断。
    """
    mach = platform.machine().lower()
    if mach in ("arm64", "aarch64"):
        return "aarch64"
    return "x86_64"


def _capture(cmd: list[object]) -> str:
    """跑一条命令取 stdout；失败返回空串（只为探测，不抛异常）。"""
    try:
        r = subprocess.run(
            [str(c) for c in cmd],
            capture_output=True,
            text=True,
            timeout=60,
        )
    except (OSError, subprocess.SubprocessError):
        return ""
    return (r.stdout or "") + (r.stderr or "")


class Platform:
    """平台策略基类：默认值即 POSIX 语义，Windows 覆盖需要变的部分。"""

    name = "linux"
    is_windows = False
    exe_suffix = ""

    # ---- 已装软件的落点 ----

    def program_roots(self) -> list[Path]:
        """有包管理器/标准前缀的平台不需要扫：PATH 已经覆盖。"""
        return []

    def vs_installs(self) -> list[Path]:
        """Windows 专有：Visual Studio 实例（含 Build Tools）。"""
        return []

    # ---- 编译器 ABI 与工具包名 ----

    def qt_abi_default(self) -> str:
        """aqt 的 arch 名缺省值（无 PS_QT_ABI 覆盖时用）。"""
        return "linux_gcc_64"

    # ---- 受管入口 ----

    def make_executable_link(self, src: Path, dst: Path) -> None:
        """把可执行文件挂进 $PS_DEPS_ROOT/bin。"""
        dst.parent.mkdir(parents=True, exist_ok=True)
        if dst.is_symlink() or dst.exists():
            dst.unlink()
        os.symlink(src, dst)


class WindowsPlatform(Platform):
    """Windows：工具多半不在 PATH 上，靠扫 Program Files / VS 实例补足。"""

    name = "windows"
    is_windows = True
    exe_suffix = ".exe"

    def program_roots(self) -> list[Path]:
        roots = []
        for var in ("ProgramFiles", "ProgramFiles(x86)"):
            v = os.environ.get(var)
            if v:
                roots.append(Path(v))
        la = os.environ.get("LOCALAPPDATA")
        if la:
            roots += [Path(la) / "Programs", Path(la) / "Microsoft" / "WinGet" / "Links"]
        return roots

    def vs_installs(self) -> list[Path]:
        """vswhere 优先（Build Tools 也认）；没有 vswhere 就枚举 Program Files 下的年份/产品。"""
        vswhere = (
            Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
            / "Microsoft Visual Studio"
            / "Installer"
            / "vswhere.exe"
        )
        if vswhere.is_file():
            out = _capture([vswhere, "-all", "-prerelease", "-property", "installationPath"])
            paths = [Path(line.strip()) for line in out.splitlines() if line.strip()]
            if paths:
                return paths
        hits = []
        for var in ("ProgramFiles", "ProgramFiles(x86)"):
            base = os.environ.get(var)
            if not base:
                continue
            for vs in sorted((Path(base) / "Microsoft Visual Studio").glob("*/*"), reverse=True):
                if (vs / "Common7").is_dir():
                    hits.append(vs)
        return hits

    def qt_abi_default(self) -> str:
        # 与 MSVC 配套的官方预编译包；MinGW 那套暂不作为默认（见 docs/develop/building.md）
        return "win64_msvc2022_64"

    def make_executable_link(self, src: Path, dst: Path) -> None:
        """Windows 普通用户没有软链特权，复制一份最稳（体积代价可接受）。"""
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)


def platform_value(v, default=None):
    """清单字段的平台取值：标量两平台共用，dict 按 {"linux"/"windows"} 取。"""
    if isinstance(v, dict):
        return v.get(PLATFORM.name, default)
    return default if v is None else v


class LinuxPlatform(Platform):
    """Linux：系统包管理器把工具都装在 PATH 上，受管入口用软链即可，Qt 走 gcc ABI。"""

    name = "linux"
    is_windows = False
    exe_suffix = ""


PLATFORM: Platform = WindowsPlatform() if os.name == "nt" else LinuxPlatform()
