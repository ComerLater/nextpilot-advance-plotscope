# tools — 开发与发布工具

- `ps-plugin-check/`：Qt-free 子进程 `dlopen` 插件做准入检查（坏 .so 崩在子进程里）
- `ps-bench/`：吞吐 / 延迟基准（10M 点 × 20 通道 × 60 fps）
- `ps-pack/`：安装包 + 三方许可清单

- `ps-setup.py`：编译环境统一入口（Linux / Windows）。子命令：`bootstrap` 一键装齐、`install [组件…]` 补齐、`check` / `doctor` 体检（含落点与修法）、`resolve <组件>` 单组件解析、`prepare` configure 门禁一次算完（输出一份 JSON 给 CMake）、`mount` 挂 Git hooks、`uninstall` 撤受管目录。版本与下载源写在各 `Tool` 类里，没有外部清单文件
- `ps_setup_platform.py`：Windows / Linux 差异的唯一一处（扫哪些安装根、怎么找 VS、受管入口用软链还是复制、Qt 用哪个 ABI）。`ps-setup.py` 里不写平台分支
- `requirements.txt`：Python 依赖与格式化 / 检查工具的版本唯一源（含 `clang-format`、`ruff`、`mypy`、`shellcheck-py`），`PythonTool` 直接指向它，装进 `$PS_DEPS_ROOT/toolchain/python/venv`，不污染系统 site-packages
- `format.sh`：统一格式化入口。`--staged` 修暂存区、`--check [BASE]` 校验改动（CI 用）、`--all` 全量（默认 md+py，加 `--cpp` 才整树重排 C/C++）、`--only md|py|cpp` 只跑一类。pre-commit hook 调的就是它
- `mavlink-gen.sh`：从 MAVLink 定义生成代码
- `nmea-metadata.py` / `ubx-metadata.py` / `mavlink-metadata.py`：从协议库/头文件生成 `*_database.json`
- `spikes/`：一次性实验代码，不参与构建
