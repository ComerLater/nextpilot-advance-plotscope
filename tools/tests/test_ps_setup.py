"""ps-setup 纯函数与安全边界的最小回归集。

只测不需要网络、不碰用户环境的纯逻辑：版本表达式、产物定位、zip 路径穿越、
以及"钉死才导出"的 exact_versions 规则。跑法：
    python3 -m unittest discover -s tools/tests -p 'test_*.py'
"""

from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOLS))


def load_ps_setup():
    """文件名带连字符，常规 import 不行；按路径载入并登记进 sys.modules（dataclass 需要）。"""
    spec = importlib.util.spec_from_file_location("ps_setup", TOOLS / "ps-setup.py")
    assert spec and spec.loader
    mod = importlib.util.module_from_spec(spec)
    sys.modules["ps_setup"] = mod
    spec.loader.exec_module(mod)
    return mod


ps = load_ps_setup()


class SplitExprTest(unittest.TestCase):
    def test_operators(self):
        self.assertEqual(ps.DepsManifest.split_expr(">=3.24"), (">=", "3.24"))
        self.assertEqual(ps.DepsManifest.split_expr("==6.8.3"), ("=", "6.8.3"))
        self.assertEqual(ps.DepsManifest.split_expr("=6.8.3"), ("=", "6.8.3"))
        # 裸值按下限门禁、安装装这一版：语义只有这一处定义
        self.assertEqual(ps.DepsManifest.split_expr("6.8.3"), (">=", "6.8.3"))

    def test_test_version(self):
        self.assertTrue(ps.DepsManifest.test_version("4.4.4", "3.31.6"))
        self.assertTrue(ps.DepsManifest.test_version("6.8.3", "6.8.3"))
        self.assertFalse(ps.DepsManifest.test_version("6.8.2", "6.8.3"))
        self.assertTrue(ps.DepsManifest.test_version("1.2.0", ">=1.2"))
        self.assertFalse(ps.DepsManifest.test_version(None, ">=3"))
        # "=" 是精确匹配：6.8.3.1 不算满足 6.8.3
        self.assertFalse(ps.DepsManifest.test_version("6.8.3.1", "=6.8.3"))


class ArtifactTest(unittest.TestCase):
    def test_asset_rel_shapes(self):
        py = {"os": "linux", "arch": "x86_64", "version": "3.12.12+20251014", "file": "cpython.tar.zst"}
        self.assertEqual(ps.asset_rel("python", py), "20251014/cpython.tar.zst")
        ninja = {"os": "windows", "arch": "x86_64", "version": "1.12.1"}
        self.assertIn("v1.12.1/ninja-win.zip", ps.asset_rel("ninja", ninja))
        rust = {"os": "linux", "arch": "aarch64", "version": "1.29.0"}
        self.assertEqual(ps.asset_rel("rust", rust), "archive/1.29.0/aarch64-unknown-linux-gnu/rustup-init")

    def test_unknown_tool_rejected(self):
        with self.assertRaises(RuntimeError):
            ps.asset_rel("gcc", {"os": "linux", "arch": "x86_64", "version": "1"})

    def test_entry_artifact_by_platform(self):
        e = {
            "artifacts": [
                {"os": "linux", "arch": "x86_64", "version": "1", "sha256": "a"},
                {"os": "windows", "arch": "x86_64", "version": "1", "sha256": "b"},
            ]
        }
        self.assertEqual(ps.entry_artifact("x", e, "windows", "x86_64")["sha256"], "b")
        with self.assertRaises(RuntimeError):
            ps.entry_artifact("x", e, "linux", "aarch64")

    def test_entry_artifacts_requires_hash(self):
        with self.assertRaises(RuntimeError):
            ps.entry_artifacts("x", {"artifacts": [{"os": "linux", "arch": "x86_64", "version": "1"}]})


class ExtractZipTest(unittest.TestCase):
    def test_zip_slip_skipped(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            z = tmp / "evil.zip"
            with zipfile.ZipFile(z, "w") as f:
                f.writestr("ok.txt", "fine")
                f.writestr("../escaped.txt", "nope")
            dst = tmp / "out"
            ps.extract_zip(z, dst)
            self.assertTrue((dst / "ok.txt").is_file())
            self.assertFalse((tmp / "escaped.txt").exists())


class VersionsTest(unittest.TestCase):
    def test_only_pinned_exported(self):
        vals = ps.exact_versions()
        self.assertEqual(vals["qt"], "6.8.3")
        self.assertEqual(vals["cmake"], "3.31.6")
        self.assertEqual(vals["rust"], "1.93.0")
        # 下限语义的组件不进 CI 导出
        self.assertNotIn("ninja", vals)
        self.assertNotIn("python", vals)

    def test_app_version_from_versions_env(self):
        self.assertRegex(ps.app_version(), r"^\d+\.\d+\.\d+")


class LazyLoadTest(unittest.TestCase):
    def test_help_works_without_manifest(self):
        # 真源缺失时 --help 必须能跑（argparse 不能被模块级加载拖死）
        tc_path = ps.REPO.toolchain_json
        self.assertTrue(tc_path.is_file())
        import subprocess

        r = subprocess.run(
            [sys.executable, str(TOOLS / "ps-setup.py"), "--help"], capture_output=True, text=True, timeout=30
        )
        self.assertEqual(r.returncode, 0)


if __name__ == "__main__":
    unittest.main()
