#!/usr/bin/env node
// 供 CI 读取工具链版本：只读文件，不执行 ps-setup（CI 里没有仓库外的 deps 根）。
//
// 用法：node tools/ci/toolchain-ver.cjs <qt|cmake|ninja|ccache|python|rust>
//
// 取值来源：
//   - qt / cmake / ninja / ccache / python → 仓库根 toolchain.json 的 toolchains.<name>.version
//   - rust → src/kernel/rust-toolchain.toml 的 channel（cargo 直读的同一份文件）
//
// 输出裸版本（不带比较运算符），便于直接喂给 install-qt-action / dtolnay-rust-toolchain。
// 遇到 ">=x" 这类门禁表达式时脚本拒绝输出，避免把区间当成可安装的具体版本。

const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..');
const KEY = process.argv[2];

if (!KEY) {
    console.error('用法：node tools/ci/toolchain-ver.cjs <qt|cmake|ninja|ccache|python|rust>');
    process.exit(2);
}

function fail(message) {
    console.error(`[toolchain-ver] ${message}`);
    process.exit(1);
}

function readRustChannel() {
    const file = path.join(ROOT, 'src', 'kernel', 'rust-toolchain.toml');
    if (!fs.existsSync(file)) fail(`缺 ${file}`);
    const match = fs.readFileSync(file, 'utf8').match(/^\s*channel\s*=\s*"([^"]+)"/m);
    if (!match) fail('rust-toolchain.toml 缺 channel = "..."');
    return match[1];
}

function readToolchain(key) {
    const file = path.join(ROOT, 'toolchain.json');
    if (!fs.existsSync(file)) fail(`缺 ${file}`);
    const manifest = JSON.parse(fs.readFileSync(file, 'utf8'));
    const entry = manifest.toolchains && manifest.toolchains[key];
    if (!entry || !entry.require) fail(`toolchain.json 缺 toolchains.${key}.require`);
    return entry.require;
}

const version = KEY === 'rust' ? readRustChannel() : readToolchain(KEY);

// 只有纯版本号才适合安装；">=1.10" 这类门禁范围交回给人处理。
if (!/^\d+(\.\d+)*$/.test(version)) {
    fail(`${KEY} 的版本 "${version}" 不是可安装的具体版本（门禁范围不能直接用于 CI 安装）`);
}

process.stdout.write(version);
