# tools — 开发、门禁与发布工具

## 从这里开始

日常只用三个入口：

| 场景               | 命令                                                                   | 文件                         |
| ------------------ | ---------------------------------------------------------------------- | ---------------------------- |
| Windows 构建与运行 | 只记 `cmake --preset default` / `cmake --build --preset default -j`    | 无包装命令                   |
| 环境安装与体检     | `tools\ps-setup.bat bootstrap` / `check`；Linux 用 `tools/ps-setup.sh` | `ps-setup.*` → `ps-setup.py` |
| 推前门禁           | `tools/ci/local_preflight.sh`，C++ 或推分支用 `--full`                 | `ci/local_preflight.sh`      |

Windows 的编译环境（MSVC、Qt、受管 PATH）只由 `ps-setup.py` 准备，并生成 `%USERPROFILE%\.ps-deps\env.ps1`，注册到 PowerShell profile、Windows Terminal 和 VSCode 默认终端；不提供 `dev.bat build/test/run` 这类第二套命令。手动兜底只有 `%USERPROFILE%\.ps-deps\env.bat` 一个入口。

其余是内部工具，命令与参数不承诺稳定；改调用方前先查本表。

## 入口

### ps-setup.py 及其薄壳

编译环境统一入口。子命令：

- `bootstrap`：一键装齐
- `install [组件…]`：补齐指定组件
- `check` / `doctor`：体检，含落点与修法
- `resolve <组件>`：单组件解析
- `prepare`：configure 门禁，输出一份 JSON 给 CMake
- `mount`：生成 env 文件，并注册 Git hooks、PowerShell profile、Windows Terminal、VSCode 默认终端
- `uninstall`：撤受管目录

版本与下载源写在各 `Tool` 类里，没有外部清单文件。`ps-setup.bat` / `.ps1` / `.sh` 是薄壳，负责准备受管 Python 后转给内核。

### format.sh

统一格式化入口。`--staged` 修暂存区、`--check [BASE]` 校验改动、`--all` 全量（默认 md+py，加 `--cpp` 才整树重排 C/C++）、`--only md|py|cpp` 只跑一类。pre-commit hook 调的就是它。

### ci/local_preflight.sh

推前门禁。quick 检查格式、脚本、workflow、mypy 与重排；`--full` 加 Release/Debug 双构建、双 ctest、clang-tidy 增量与 `arch_guard`。

## 内部工具

### 环境

- `ps_setup_platform.py`：Windows / Linux 差异的唯一一处，扫安装根、找 VS、决定受管入口用软链还是复制、Qt 用哪个 ABI
- `requirements.txt`：Python 依赖与格式化 / 检查工具版本唯一源，装进 `$PS_DEPS_ROOT/toolchain/python/venv`

### 门禁

- `ci/resolve-base.sh`：解析改动行基准，format / markdown / python / tidy 共用
- `ci/arch_guard.sh`：分层与依赖守卫
- `check-workflow-outputs.py`：校验 workflow 里 `steps.<id>.outputs.<key>` 的输出契约
- `clang-tidy-diff-gate.py`：只断 diff 新增行的 clang-tidy 告警

### 代码生成（`tools/codegen/`）

- `mavlink-gen.sh`：从 MAVLink 定义生成代码；产物已提交，构建不调用
- `mavlink-metadata.py` / `nmea-metadata.py` / `ubx-metadata.py`：从协议库或头文件生成 `*_database.json`
- `gen-seed-flights.py`：生成脚本自动化用的 seed CSV；构建与 CI 不调用

### 打包

- `pack-linux.sh`：Linux 便携 tar.gz，也是 deb / AppImage 的 stage 基础
- `pack-deb.sh`：Debian / Ubuntu `.deb`
- `pack-appimage.sh`：Linux AppImage
- `pack-macos.sh`：macOS 包
- `pack-windows.ps1`：Windows 包

打包脚本由 `.github/workflows/build-packages.yml` 与 `pack-dryrun.yml` 调用。

## 归档

- `spikes/`：一次性实验代码，不参与构建
