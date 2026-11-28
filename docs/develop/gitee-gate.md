# Gitee PR 门禁

Gitee 侧没有「提交状态回写」API，也没有能装上 Qt 6 + Rust 的构建镜像，所以门禁不做本地编译：它只核对**同一个提交在 GitHub Actions 上的三套结果**，把结论变成 Gitee 上可强制的检查项。

判定口径：`lint` / `ci` / `pack-dryrun` 三套都必须是 `success`。任一失败、缺失或仍在跑，流水线即失败。

## 为什么不在 Gitee 编译

| 事实                                             | 后果                                                   |
| ------------------------------------------------ | ------------------------------------------------------ |
| `POST /repos/{owner}/{repo}/statuses` 返回 405   | 外部 CI 结果无法直接回写，只能用 Gitee Go 自己的流水线 |
| Gitee Go 云端镜像只有 CentOS 8.3 与 Ubuntu 20.04 | 没有 Qt 6（20.04 仓库只有 Qt 5），也没有 Rust 工具链   |
| 单仓 200 分钟免费额度                            | 装齐依赖再全量编译，几次就烧完，且每次结果不可复现     |

GitHub Actions 已在同一 sha 上跑完整构建（`-j$(nproc)`、ccache、Debug/Release 双档），Gitee 侧只需要读取结论。

## 启用步骤

1. 仓库 → DevOps → Gitee Go，开通并新建流水线。
2. 代码视图粘贴 `.workflow/pr-gate.yml`，保存（保存时页面会把 YAML 写回仓库）。
3. 触发事件勾选 **Pull Request**，目标分支 `main`。
4. 保护分支 `main` → 勾选「要求门禁状态成功才能合并」，选中这条流水线的检查项。

## 需要的变量

匿名调用 GitHub API 只有 60 次/小时，构建机出口 IP 是共享的，必须配令牌：

| 变量           | 值                                              |
| -------------- | ----------------------------------------------- |
| `GITHUB_TOKEN` | 只读 fine-grained token，权限仅 `Actions: read` |

配在 Gitee Go 的全局参数里，脚本读环境变量，不落仓库。

可选：`PS_GATE_WAIT_SECONDS`（默认 0，有 run 仍在跑就直接判失败）、`PS_GATE_WORKFLOWS`（默认 `lint,ci,pack-dryrun`）。

## 局限

- PR 刚推送时 GitHub 还没跑完，门禁会红。等 CI 绿了在 Gitee 上点「重新执行」。
- 构建机必须能访问 `api.github.com`。脚本网络失败时退出码 2 并报错，不会假绿。
- 门禁只回答「CI 过没过」，不回答「代码对不对」。生命周期、注册契约这类问题仍需人工审。

## 兜底

Gitee Go 不可用（未开通、额度耗尽、出网失败）时，退回人工：合并前在 PR 里贴 GitHub 三套 run 的链接与截图，确认全绿再合。
