# lua —— Lua 5.5.1（MIT）

- 来源：<https://www.lua.org/ftp/lua-5.5.1.tar.gz>（官方发行包，不是 GitHub tag：`lua/lua` 的 tag 与发行包不一致，v5.4.4 tag 就缺 `luac.c`）。
- `src/` 是上游 `src/` 原样拷贝，**63 个文件逐字节一致、零本地修改**（含上游的 `lua.hpp`、`Makefile`，二者不参与构建，只为保持 1:1）。上游版权与许可见 `README-upstream.md`。
- 构建：`CMakeLists.txt` 把库本体（除 `lua.c` / `luac.c` 两个命令行入口）编成静态库目标 `lua::lua`，应用侧只链接这个目标。

升级步骤：换掉 `src/` 整目录、同步 `README-upstream.md` 与版本号，不需要改 CMake；自检用 `diff -rq src/3rdparty/lua/src <新发行包>/src`，应零输出。
