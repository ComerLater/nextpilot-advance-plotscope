# 编译与构建

## 依赖

| 组件       | 要求                                                      |
| ---------- | --------------------------------------------------------- |
| CMake      | ≥ 3.24                                                    |
| Qt         | ≥ 6.8.3（Core / Widgets / Gui / Svg）                     |
| C++ 编译器 | C++17（Linux: GCC ≥ 9；Windows: MSVC 2022 x64）           |
| Rust       | `cargo` 可用，channel 见 `src/kernel/rust-toolchain.toml` |
| Python     | 3.10+（只有 `tools/` 下的脚本需要）                       |

版本写在哪：

- 工具链（cmake / ninja / ccache / python / rust / qt）：`tools/ps-setup.py`，每个组件一个类，改版本、换镜像都只改这里。
- 三方库（zlib / zstd / lz4 / nanoarrow）：`cmake/Dependencies.cmake` 顶部的 `PS_VER_*` 与 `PS_MIRRORS_*`。镜像按 `URL|SHA256` 排列，构建时按顺序探测，第一个可用的即用。

默认安装到 `~/.ps-deps`（Windows 为 `%USERPROFILE%\.ps-deps`），改到别处按优先级取第一个有值的：`-DPS_DEPS_ROOT=<path>` → 环境变量 `PS_DEPS_ROOT` → 平台缺省。命令行最高。

`PS_DEPS_ROOT` 只管**环境根**（`toolchain/`、`bin/`、`caches/`、`env.*`），不管第三方库。想用已经预编译好的 zlib / sqlite3 / zstd / lz4，另用 `-DPS_LIBS_PREFIX=<含 include 与 lib 的目录>`，缺省为空（走系统 / vcpkg / FetchContent）。两个变量互不影响，换工具链根不会顺带改库搜索路径。

## 环境组合速查

configure 的固定顺序是"体检 → 用现成的 → 缺了才装"，所以同一套命令能吃下下面这些机器。"自动"= 不用加任何参数。

| 机器现状                                          | Linux                                                                                                   | Windows                                                                                                      | 要你动手吗         |
| ------------------------------------------------- | ------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------ | ------------------ |
| 新系统，什么都没装                                | 受管 Python / CMake / Qt / Rust 装进 `$PS_DEPS_ROOT`；gcc、ninja、ccache 走 apt，缺了打印命令不代装     | 受管 Python / ninja / ccache / Qt 装进 `$PS_DEPS_ROOT`；MSVC 缺失时 `winget` 装 VS 2022 Build Tools 最小负载 | 自动，首次要下载   |
| 发行版工具链齐全（CMake ≥3.24、Python ≥3.10）     | 直接用系统的，不装副本                                                                                  | 直接用系统的，不装副本                                                                                       | 自动               |
| 版本低于下限（CMake 3.16 / Python 3.9 / Qt 6.5）  | 受管副本装进 `$PS_DEPS_ROOT`，PATH 与 Qt 前缀以受管优先                                                 | 同上                                                                                                         | 自动，旧版本不动   |
| Qt 已装但不在 `PATH`（如 `D:\program\Qt\6.12.0`） | 扫 `~/Qt`、`/opt/Qt`；命中即用                                                                          | 扫 `C/D/E:\Qt`、各盘符下 `<任意目录>\Qt`、`Program Files\Qt`、vcpkg；多份取版本最高                          | 自动               |
| Qt 版本低于 6.8.3                                 | 视为不可用，补装受管 6.8.3                                                                              | 同上                                                                                                         | 自动               |
| Windows 只有 MinGW 版 Qt                          | —                                                                                                       | 该 Qt 被忽略（ABI 不配套，配 MSVC 会在链接期炸），改装 MSVC 版                                               | 自动，日志会说明   |
| 没有免密 sudo                                     | 系统包不代装，打印 `sudo apt-get install …`；受管组件照常装进用户目录                                   | —                                                                                                            | 复制命令自己跑     |
| 内网 / 外网不通                                   | 镜像按顺序轮询（USTC / 阿里 / 清华）；全不通时把装好的 `$PS_DEPS_ROOT` 整份拷过去，门禁只认磁盘上的文件 | 同左（GitHub Releases 走 npmmirror / gh-proxy 等镜像）                                                       | 首台机器联网装一次 |
| 同一组件装了好几份                                | Qt 取版本最高；其余按 `PATH` 先后                                                                       | 同左                                                                                                         | 自动               |
| 已有旧的 `build/` 目录                            | 换过生成器就删掉重建；只换 Qt 用 `-DPS_QT_DIR` 覆盖                                                     | 同左                                                                                                         | 视情况             |

显式指定永远优先：`-D` > 环境变量（`PS_DEPS_ROOT` / `PS_QT_DIR` / `CCACHE_DIR` / `PS_LIBS_PREFIX`）> 缓存里的旧值 > 平台缺省。显式指定了低于下限的 Qt 也不会被放过，报错里会直接说是哪一项不满足。

## Linux / 云主机

```bash
tools/ps-setup.sh bootstrap        # 装齐 cmake / ninja / ccache / python / qt / rust
tools/ps-setup.sh check            # 体检：实测 / 要求 / 来源
tools/ps-setup.sh install qt       # 只补某一样
```

新机器上系统 cmake 低于 3.24 时先跑 `bootstrap`：它会下载一份受管的 Python 与 cmake，装进 `$PS_DEPS_ROOT`，不碰系统包。

装完后新开终端即可构建，不需要任何 `-D` 参数：

```bash
cmake --preset default                          # Release，落 build/
cmake --build --preset default -j"${PS_JOBS:-$(nproc)}"
ctest --test-dir build --output-on-failure      # 无显示器环境自动 offscreen

cmake --preset fast                             # 不编测试目标，改代码只编主程序
cmake --preset debug                            # Debug + 断言，落 build-debug/
```

并行度默认按本机核数；小内存机器收窄：`PS_JOBS=2 cmake --build --preset default`。

三个 preset 都用 Ninja。若旧目录是别的生成器建的（Makefiles 等），删掉 `build/` 重建——CMake 不允许在生成器不同的目录上 configure。

Windows 上 Ninja 还要求 PATH 中有 `cl.exe`。`tools\ps-setup.bat bootstrap` 会把 MSVC 环境写进 `env.ps1` 并注册到终端，重开终端后直接 configure 即可；临时排查也可以 `call %USERPROFILE%\.ps-deps\env.bat` 后再跑 cmake。

configure 会跑一次 `tools/ps-setup.py prepare` 做门禁：缺什么自动补装，装完复查，仍不过才终止并给出手动命令；只想体检不改机器就 `prepare --no-install`。

## Windows

```powershell
tools\ps-setup.bat bootstrap   # 装齐并生成 %USERPROFILE%\.ps-deps\env.ps1
# 关闭当前终端，重新开一个（或 VSCode 里新开终端）
cmake --preset default
cmake --build --preset default -j
ctest --test-dir build --output-on-failure
```

`bootstrap` 结束时会把环境注册到 PowerShell profile、Windows Terminal 的 `PlotScope Dev` 终端和 VSCode 默认终端，新开终端自动加载 `env.ps1`，不需要手动 source，也没有 `dev.bat build/test/run` 这类第二套命令。

想体检就 `tools\ps-setup.bat check`；想手动兜底就在 cmd 里 `call %USERPROFILE%\.ps-deps\env.bat`，之后同样只用原生 cmake 命令。

bat / ps1 薄壳只做一件事：找不到受管 Python 就下载一份独立的 python-build-standalone 到 `$PS_DEPS_ROOT\toolchain\python\`，然后把参数交给 `ps-setup.py`。系统 Python 不被改动。受管安装落 `$PS_DEPS_ROOT`，清单：

| 项               | 说明                                                                                                                    |
| ---------------- | ----------------------------------------------------------------------------------------------------------------------- |
| Python           | python-build-standalone（钉版本 + sha256），系统自带 `tar.exe` 解压                                                     |
| ninja / ccache   | GitHub Releases（钉版本 + sha256）                                                                                      |
| MSVC             | 优先用机器上已装的 VS / Build Tools；都没有时 `winget` 装 VS 2022 Build Tools 最小负载（`VCTools`），不用去官网点安装器 |
| Qt               | aqt `windows desktop`，ABI 跟随实际编译器（MSVC 环境取 `win64_msvc2022_64`，可用 `PS_QT_ABI` 覆盖）                     |
| git              | 缺失时提示 `winget install --id Git.Git`，不静默安装                                                                    |
| 环境文件         | `env.ps1` / `env.bat`                                                                                                   |
| Shell / IDE 入口 | PowerShell `$PROFILE` 标记块、Windows Terminal fragment、`.vscode/settings.json`                                        |

### 已装好的工具不必重装

Windows 上软件常常不在统一目录，所以 configure 会先找现成的、找不到才装受管副本。

查找分两步，都写在 `tools/ps-setup.py`：

1. 先看 `PATH`（受管 `$PS_DEPS_ROOT\bin` 在最前，然后是系统）。
2. 不在 `PATH` 的再扫已知落点：`Program Files`、`Program Files (x86)`、`%LOCALAPPDATA%\Programs`、WinGet 的 `Links`，以及 Visual Studio 实例目录（vswhere 优先）。VS 自带的 cmake / ninja、scoop 的 ccache、手工装 Python 都在这一层找到。

`doctor` 的"来源"列会显示 `system` / `scan` / `managed`：`scan` 就是"装着但没进 PATH"，configure 会把它的目录前置进 `PATH`，后续不再重复安装。

想用自己的 Qt，任选一种指定方式：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DPS_QT_DIR=D:/program/Qt/6.12.0/msvc2022_64
$env:PS_QT_DIR = "D:/program/Qt/6.12.0/msvc2022_64"      # 或者先设环境变量
```

不指定时按顺序查找，命中即止：

1. `-DPS_QT_DIR` / `CMakeCache.txt` 里的旧值
2. 环境变量 `PS_QT_DIR`、`QTDIR`、`CMAKE_PREFIX_PATH`
3. 受管前缀 `$PS_DEPS_ROOT\toolchain\qt`
4. 常见目录：`C:\Qt`、`D:\Qt`、`E:\Qt`、`%ProgramFiles%\Qt`、`%USERPROFILE%\Qt`、`%LOCALAPPDATA%\Programs\Qt`，以及各盘符下一层的 `<任意目录>\Qt`（例如 `D:\program\Qt`）
5. vcpkg 的 `installed\<triplet>`

判据是目录里含 `lib\cmake\Qt6\Qt6Config.cmake`；版本按 ≥ 6.8.3 判，装的是 6.12 也算通过。找到后路径记进 `CMakeCache.txt`，下次 configure 不再查找；要换就重新 `-DPS_QT_DIR=...`。

想看清每个组件的落点和版本：

```powershell
python tools/ps-setup.py doctor        # 逐项：实测 / 要求 / 来源 / 路径 / 修法
python tools/ps-setup.py resolve qt    # 只看某一项
```

### 缺组件怎么补

configure 会自己补：`prepare` 默认缺什么装什么，装不上才 FATAL。想先看清缺哪一项，跑 `check` / `doctor`。手动作业按组件来：

```powershell
python tools\ps-setup.py check                 # 只体检，不改机器
python tools\ps-setup.py install msvc           # 缺编译器：winget 装 VS 2022 Build Tools（VC Tools + Windows SDK）
python tools\ps-setup.py install python         # 缺 Python / venv：pbs Python + venv（aqt、ruff 都落这里）
python tools\ps-setup.py install qt             # 缺 Qt：aqt 装 6.8.3 的 win64_msvc2022_64
```

**缺 Python**：一般不用自己装。`tools\ps-setup.bat` 先找受管 venv（`$PS_DEPS_ROOT\toolchain\python\venv`），其次找已下载的 pbs 解释器（`…\python\pbs\python\python.exe`），都没有才按版本 + sha256 下载 python-build-standalone 到 `$PS_DEPS_ROOT\toolchain\python`；全程不碰、也不依赖系统 Python。想显式重建 venv：`tools\ps-setup.bat install python`。

**缺 Qt**，按机器现状三选一：

1. 已有 MSVC 版 Qt（≥ 6.8.3）——不用装，指过去就行：`-DPS_QT_DIR=D:/program/Qt/6.12.0/msvc2022_64`。不在上面那些常见落点时尤其要显式指。
2. 完全没有 Qt——`python tools\ps-setup.py install qt`。前置是 MSVC 和 venv：没有 MSVC 时 `abi_name()` 直接报"先装 msvc"，不会替你装一份 ABI 不配套的 Qt。装完落在 `%PS_DEPS_ROOT%\toolchain\qt\6.8.3\win64_msvc2022_64`，下次 configure 自动命中。
3. 只有 MinGW 版 Qt——不认。MSVC 配 MinGW 的 Qt 能过 configure，链接期才炸 LNK2038；必须换 MSVC 版（`install qt`，或用官方安装器装 MSVC 2022 64-bit 组件）。

内网或完全离线：`install qt` 走 aqt 在线源（官方 + 阿里云镜像），断网装不了；改用 Qt 官方 Online Installer 装 6.8.3+ 的 **MSVC 2022 64-bit** 组件，再 `-DPS_QT_DIR` 指过去。

Linux 同构：`tools/ps-setup.sh install qt` 装 `linux_gcc_64`；gcc / ninja / ccache 走 apt，脚本只在有免密 sudo 时代装，否则打印命令。

### 构建与运行

```powershell
cmake --build build --config Release -j
ctest --test-dir build -C Release --output-on-failure
```

Qt 的 DLL 在 `<Qt>\bin`，构建期已经处理：`PS_DEPLOY_QT=ON`（默认）时用 `windeployqt` 把 DLL 与插件拷到可执行文件旁边，双击即可运行；VS 里按 F5 也带好了 `PATH`。不想拷贝 DLL 就 `-DPS_DEPLOY_QT=OFF`，此时运行前需自己把 `<Qt>\bin` 加进 `PATH`。

## 格式化

统一入口 `tools/format.sh`，覆盖 C/C++ 变更行、Markdown、Python：

```bash
tools/format.sh --staged              # 修复暂存区（pre-commit 调的就是它）
tools/format.sh --check origin/main   # 只检查改动（CI 调它）
tools/format.sh --all                 # 全量 Markdown + Python
tools/format.sh --check HEAD~1 --only py   # 只查一类
```

C/C++ 只格式化变更行（整树重排会毁 git blame，默认不做；确需整树才加 `--cpp`）。

提交前自动格式化靠 Git hook：`python3 tools/ps-setup.py mount` 注册 `core.hooksPath=.githooks`（configure 也会自动补上）。临时绕过：`git commit --no-verify`；彻底关闭：`git config --unset core.hooksPath`。hook 只看已暂存内容，`git add` 之后又改文件请重新 add。

工具版本固定在 `tools/requirements.txt`（`clang-format`、`ruff`、`mypy`、`shellcheck-py`），装法 `python3 tools/ps-setup.py install python`；prettier / markdownlint 由 `tools/format.sh` 用 `npx` 现取固定版本。本地与 CI 必须同版本，否则会出现"本地已格式化、CI 仍报 diff"。

## 构建加速开关

| 开关                            | 默认                       | 作用                                             |
| ------------------------------- | -------------------------- | ------------------------------------------------ |
| `PS_USE_CCACHE`                 | `ON`                       | 接入 ccache，命中即跳过编译                      |
| `PS_REQUIRE_CCACHE`             | Linux `ON` / Windows `OFF` | 探不到 ccache 直接失败，验收机用                 |
| `PS_BUILD_TESTS`                | `ON`                       | 是否构建测试目标（`--preset fast` 已设 OFF）     |
| `PS_USE_PCH` + `PS_PCH_TARGETS` | `OFF` / 空                 | 预编译头，只建议 CI 冷构建；与 ccache 同开会被拦 |

CI 冷构建示例：

```bash
cmake --preset default -DPS_USE_PCH=ON -DPS_PCH_TARGETS="SARibbonBar;fastplot" -DPS_USE_CCACHE=OFF
```

预热本机 ccache：`cmake --preset default && cmake --build --preset default`，Debug 用 `--preset debug`。要不要预热不影响正确性，只影响第一次构建耗时。

## 配置项

改法三选一：`-D<VAR>=<VALUE>` 一次性、改 `CMakePresets.json` 长期、`cmake --preset <name> --fresh` 推倒重配。

| 变量                | 默认                          | 说明                                                                   |
| ------------------- | ----------------------------- | ---------------------------------------------------------------------- |
| `CMAKE_BUILD_TYPE`  | `Release`                     | `Release` / `Debug` / `RelWithDebInfo`；Debug 才暴露断言与未初始化问题 |
| `PS_BUILD_TESTS`    | `ON`                          | 关掉只编主程序                                                         |
| `PS_USE_CCACHE`     | `ON`                          | 编译缓存                                                               |
| `PS_REQUIRE_CCACHE` | Linux `ON`，Windows `OFF`     | 缓存不可用即失败                                                       |
| `PS_USE_PCH`        | `OFF`                         | 预编译头                                                               |
| `PS_DEPS_FETCH`     | `ON`                          | 缺 zlib / sqlite3 / nanoarrow 时构建期拉源码编静态库                   |
| `PS_DEPS_ROOT`      | `~/.ps-deps`                  | 环境根（工具链 / bin / caches）；`-D` 优先，其次环境变量               |
| `PS_LIBS_PREFIX`    | 空                            | 预编译 zlib / sqlite3 / zstd / lz4 前缀；只认 `-D`                     |
| `CCACHE_DIR`        | `$PS_DEPS_ROOT/caches/ccache` | ccache 缓存目录；`-D` 优先，其次环境变量                               |
| `PS_QT_DIR`         | 自动发现                      | Qt 前缀；指定后写入 `CMakeCache.txt`                                   |
| `PS_DEPLOY_QT`      | `ON`                          | Windows：构建后 `windeployqt` 拷 DLL                                   |

取值优先级：命令行 `-D` > `CMakePresets.json` 的 `cacheVariables` > 代码里的默认值。preset 只是"一组默认 -D"，`cmake --preset fast -DPS_BUILD_TESTS=ON` 仍然编测试；要连 preset 一起丢掉用 `--fresh`。

环境变量这一层按变量分别对待：`PS_DEPS_ROOT`、`PS_QT_DIR`、`CCACHE_DIR` 都既是环境变量、也是 CMake 缓存变量，两者同时存在时 `-D` 优先（`PS_DEPS_ROOT` 没有"环境变量压过 `-D`"的例外）。`PS_LIBS_PREFIX` 只有缓存变量一种入口，不读同名环境变量。`CMakePresets.json` 的 `environment` 只改 configure 进程的环境（本仓库只注入 `CCACHE_BASEDIR`），不写 `cacheVariables`，因此不会覆盖你用 `-D` 给的值；`PS_QT_DIR` 只有在本次缓存里没有值时才由 `prepare` 把发现结果 FORCE 写进缓存，显式 `-D` 过的值原样保留。`PATH`、`CMAKE_PREFIX_PATH`、`LD_LIBRARY_PATH` 由 `prepare` 按 `PS_DEPS_ROOT` 与 Qt 前缀算好后导出，命令行不要重复设置。

## 产物与运行

| 产物            | 路径                                           |
| --------------- | ---------------------------------------------- |
| CLI（headless） | `build/src/application/app/plotscope`          |
| GUI             | `build/src/application/assembly/plotscope-gui` |
| 测试            | `build/tests/ps_test_*`                        |

```bash
./build/src/application/assembly/plotscope-gui
./build/src/application/app/plotscope --info tests/fixture/demo_flight.csv
```

无显示器环境跑 GUI 冒烟：

```bash
xvfb-run -a -s '-screen 0 1600x1000x24' \
  env QT_QPA_PLATFORM=xcb ./build/src/application/assembly/plotscope-gui
```

测试用 `assert` 驱动，Release 会定义 `NDEBUG`；`tests/CMakeLists.txt` 对 MSVC 统一加 `/UNDEBUG`，保证 Release 也是真断言。副作用不要写进 `assert`——Release 会把整段编译掉。

## 出发布包

推 `v*` tag 或手动触发 `.github/workflows/build-packages.yml`，三平台各自出包；矩阵与体积说明见 [packaging.md](packaging.md)。本地出包需先构建 `plotscope-gui plotscope`：

```bash
tools/pack-appimage.sh build dist                    # Linux AppImage
tools/pack-deb.sh      build dist                    # Linux deb
tools/pack-linux.sh    build dist                    # Linux 便携 tar.gz
powershell -File tools\pack-windows.ps1 -BuildDir build -OutDir dist
tools/pack-macos.sh    build dist                    # macOS
```

## 设置存储

`App` 构造时接收 `settingsPath`：为空则设置只存内存，进程退出即失；传入 JSON 路径则读写该文件。GUI 内「导出设置」可导出 `plotscope-settings.json`；CLI 的 `--record` / `--run` 场景用 `data.workspace.save` 保存工作区配方。
