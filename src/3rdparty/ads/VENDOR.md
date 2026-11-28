# ADS vendoring 说明

上游：<https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System>（LGPL-2.1，见 `LICENSE` / `gnu-lgpl-v2.1.md`）。

## 基线

- 基线提交：`edba03d`（2026-10-29，*Guard against null DockManager in floating container events (#877)*）
- 该提交 = tag `5.1.1`（`023ce959`，2026-08-20）之后 6 个提交，其中 5 个改 `src/`：

| 提交 | 内容 |
| ---- | ---- |
| `1c4d855` | Fixed Linux Builds Patch |
| `5f3580b` | #866 原生 dock widget 脱离时的绘制残留 |
| `4f4f602` | #869 消 discards 与废弃警告 |
| `fc7a9cc` | #867 TabsAtBottom 换内容把 tab bar 变成顶层窗口 |
| `087b35a` | #873 Wayland 下不可浮动窗口的拖拽预览被冻结 |
| `edba03d` | #877 浮动容器事件里 `DockManager` 为空 |

上游自 5.1.1 起未发新 tag，所以版本号仍写 5.1.1；**实际内容以上面的基线提交为准**，不以版本号为据。

## 与本仓的差异

唯一改动是 `CMakeLists.txt`：重写为 31 行的嵌入版本（只编共享库以满足 LGPL 可替换要求、Linux 追加 `linux/FloatingWidgetTitleBar.cpp`、直接补 Qt 私有 QPA 头目录以避免拉 `GuiPrivate` 目标）。

其余全部与 `edba03d` 逐字节一致：`src/`（48 个 `.cpp/.h/.qrc`）、`cmake/modules/{Versioning.cmake, ads_version.h.in, FileVersionInfo.rc.in}`、`README.md`、`.clang-format`、`.gitignore`、`LICENSE`、`gnu-lgpl-v2.1.md`。

裁剪掉的上游顶层项：`.github/`、`.gitattributes`、`adsConfig.cmake`、`demo/`、`doc/`、`examples/`、`MANIFEST.in`、`persist/`、`project.py`、`pyproject.toml`、`sip/`。

## 基线自检

改动本目录后，用下面的命令确认 `src/` 没有漂移（把 `<基线>` 换成本文顶部的提交）：

```bash
curl -sL https://codeload.github.com/githubuser0xFFFF/Qt-Advanced-Docking-System/tar.gz/<基线> -o ads.tgz
tar xzf ads.tgz
diff -rq src/3rdparty/ads/src Qt-Advanced-Docking-System-*/
```

输出只应出现「本地补丁」表里登记的文件（当前仅 `src/DockManager.cpp`）。出现其他文件即为未登记的漂移：要么撤回改动，要么补进表里。

## 本地补丁

| 文件 | 改动 | 出处 |
| ---- | ---- | ---- |
| `src/DockManager.cpp` | `~CDockManager()` 起始调用 `removeFromDockManager()`：manager 自身也是容器，先解绑可避免基类析构经 `QPointer<CDockManager>` 对自身做下行转换（`-fsanitize=vptr` 判 UB） | 本仓 IKYP5B；上游 PR [#885](https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System/pull/885) |

上游合并 #885 后，升级时删除这条补丁，不要叠加。

## 升级步骤

1. 取目标上游提交的 tarball，按「基线自检」解包。
2. 覆盖 `src/`、`cmake/modules/`、`README.md`、`.clang-format`、`.gitignore`、`LICENSE`、`gnu-lgpl-v2.1.md`。
3. **不要覆盖 `CMakeLists.txt`**（本仓重写）。若上游 `src/` 新增文件（例如新的平台实现），同步更新其 `file(GLOB)` 与 Linux 源列表。
4. 重放「本地补丁」表；已在上游合并的条目删除。
5. 验证：构建通过；`sanitizer` 的 `asan-ubsan` 至少覆盖 `ps_gui` / `ps_instrument` / `ps_plot_io`；手动跑一次 dock 拖拽/浮动/关闭冒烟。
6. 更新本文的基线提交与提交清单。

## 维护约定

- 新增任何 ADS 本地补丁，必须同时更新「本地补丁」表，并在提交信息里写 `Refs:`。
- 升级 ADS 单独开卡，不与其他功能改动混在一个 PR。
