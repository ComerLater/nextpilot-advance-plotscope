#!/usr/bin/env python3
"""Gitee PR 门禁：核对 GitHub Actions 在同一个提交上是否全部通过。

为什么不做本地编译：Gitee Go 的构建镜像只有 CentOS 8.3 / Ubuntu 20.04 两档，
既没有 Qt 6（Ubuntu 20.04 的仓库里只有 Qt 5），也没有 Rust 工具链；装齐一次要
几十 GB 下载，而单个仓库只有 200 分钟免费构建额度。同一提交在 GitHub Actions 上
已经跑过 lint / ci / pack-dryrun，所以这里只做一件事——把那个结论搬进 Gitee 的
合并条件，让「CI 没过就不能点合并」变成平台强制，而不是靠人记得。

用法：
    tools/ci/gitee_pr_gate.py                    # Gitee Go 里：自动取 GITEE_SOURCE_COMMIT
    tools/ci/gitee_pr_gate.py --sha <sha>        # 本地复核某个提交
    tools/ci/gitee_pr_gate.py --wait 600         # CI 还在跑时等它（默认 0，不等）

退出码：0 = 全部通过；1 = 有失败/缺失/仍在跑；2 = 参数缺失或网络不通。
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request
from typing import Any

DEFAULT_REPO = "ComerLater/nextpilot-advance-plotscope"
DEFAULT_WORKFLOWS = ("lint", "ci", "pack-dryrun")
BAD_CONCLUSIONS = frozenset({"failure", "cancelled", "timed_out", "action_required", "stale", "startup_failure"})
API = os.environ.get("PS_GATE_GH_API", "https://api.github.com").rstrip("/")


def _get(url: str, token: str | None) -> Any:
    """GET 一个 GitHub API 地址并解析 JSON。"""
    request = urllib.request.Request(url)
    request.add_header("Accept", "application/vnd.github+json")
    request.add_header("User-Agent", "plotscope-gitee-gate")
    if token:
        request.add_header("Authorization", f"Bearer {token}")
    with urllib.request.urlopen(request, timeout=20) as response:
        return json.loads(response.read().decode("utf-8"))


def fetch_runs(repo: str, sha: str, token: str | None) -> list[dict[str, Any]]:
    """取该提交触发过的全部 workflow run；网络失败重试三次后抛错。"""
    url = f"{API}/repos/{repo}/actions/runs?head_sha={sha}&per_page=100"
    last_error: Exception | None = None
    for attempt in range(3):
        try:
            data = _get(url, token)
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            last_error = exc
            time.sleep(2 * (attempt + 1))
            continue
        runs = data.get("workflow_runs") if isinstance(data, dict) else None
        if not isinstance(runs, list):
            raise ValueError(f"GitHub 返回格式异常：{str(data)[:120]}")
        return [run for run in runs if isinstance(run, dict)]
    raise RuntimeError(f"访问 {API} 失败：{last_error}")


def latest_run(runs: list[dict[str, Any]], name: str) -> dict[str, Any] | None:
    """同一个 workflow 可能被重跑多次，只认最新一次。"""
    matched = [run for run in runs if run.get("name") == name]
    if not matched:
        return None
    return max(matched, key=lambda run: str(run.get("created_at", "")).replace("Z", "+00:00"))


def verdict(run: dict[str, Any] | None) -> tuple[str, str]:
    """把一条 run 归一成 (状态, 说明)；状态取 passed/failed/pending/missing。"""
    if run is None:
        return "missing", "该提交上没有这一套（先推 GitHub 触发一次）"
    if run.get("status") != "completed":
        return "pending", f"仍在跑：{run.get('status')}"
    conclusion = str(run.get("conclusion"))
    if conclusion == "success":
        return "passed", "success"
    if conclusion in BAD_CONCLUSIONS:
        return "failed", conclusion
    return "failed", conclusion


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="核对 GitHub Actions 在当前提交上是否全部通过")
    parser.add_argument("--sha", default=None, help="要核对的提交；默认取 GITEE_SOURCE_COMMIT / GITEE_COMMIT")
    parser.add_argument("--repo", default=os.environ.get("PS_GATE_GH_REPO", DEFAULT_REPO))
    parser.add_argument(
        "--workflows",
        default=os.environ.get("PS_GATE_WORKFLOWS", ",".join(DEFAULT_WORKFLOWS)),
        help="必须通过的 workflow 名，逗号分隔",
    )
    parser.add_argument(
        "--wait",
        type=int,
        default=int(os.environ.get("PS_GATE_WAIT_SECONDS", "0")),
        help="有 run 仍在跑时最多等多少秒（默认 0，直接判不通过）",
    )
    args = parser.parse_args(argv)

    sha = (args.sha or os.environ.get("GITEE_SOURCE_COMMIT") or os.environ.get("GITEE_COMMIT") or "").strip()
    if not sha:
        print("[gitee-gate] 拿不到提交号：PR 触发下需要 GITEE_SOURCE_COMMIT，或显式 --sha", file=sys.stderr)
        return 2

    required = [name.strip() for name in args.workflows.split(",") if name.strip()]
    if not required:
        print("[gitee-gate] --workflows 为空", file=sys.stderr)
        return 2

    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("PS_GATE_GH_TOKEN")
    deadline = time.monotonic() + max(0, args.wait)
    print(f"[gitee-gate] 提交 {sha[:8]}，仓库 {args.repo}，要求全部通过：{', '.join(required)}")

    runs: list[dict[str, Any]] = []
    while True:
        try:
            runs = fetch_runs(args.repo, sha, token)
        except (RuntimeError, ValueError) as exc:
            print(f"[gitee-gate] {exc}", file=sys.stderr)
            print(
                "[gitee-gate] Gitee 构建机通常能出网；若持续失败，请改用人工核对（见 docs/develop/gitee-gate.md）",
                file=sys.stderr,
            )
            return 2

        results = [(name, latest_run(runs, name)) for name in required]
        pending = [name for name, run in results if verdict(run)[0] == "pending"]
        if not pending or time.monotonic() >= deadline:
            break
        print(f"[gitee-gate] {len(pending)} 套仍在跑（{', '.join(pending)}），30 秒后复查")
        time.sleep(30)

    failed: list[str] = []
    missing: list[str] = []
    pending_names: list[str] = []
    width = max(len(name) for name in required)
    for name, run in results:
        state, detail = verdict(run)
        url = run.get("html_url", "") if run else ""
        print(f"  {name.ljust(width)}  {state:<7}  {detail}  {url}")
        if state == "failed":
            failed.append(name)
        elif state == "missing":
            missing.append(name)
        elif state == "pending":
            pending_names.append(name)

    if failed or missing or pending_names:
        print(
            f"[gitee-gate] 未通过：失败 {len(failed)} 项，缺失 {len(missing)} 项，仍在跑 {len(pending_names)} 项",
            file=sys.stderr,
        )
        return 1

    print("[gitee-gate] 全部通过")
    return 0


if __name__ == "__main__":
    sys.exit(main())
