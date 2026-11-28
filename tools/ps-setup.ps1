# ps-setup.ps1 — Windows 薄壳，由 gen-shell 生成，勿手改。
# 找到 Python 就把参数原样交给 ps-setup.py。
# ExecutionPolicy 拦 .ps1 时：powershell -ExecutionPolicy Bypass -File tools\ps-setup.ps1
# 或直接用 tools\ps-setup.bat。
# 改 Python 版本后重跑：python3 tools/ps-setup.py gen-shell
$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$DepsRoot = if ($env:PS_DEPS_ROOT) { $env:PS_DEPS_ROOT } else { "$env:USERPROFILE\.ps-deps" }
$ToolPy = Join-Path $RepoRoot "tools\ps-setup.py"
$File = "cpython-3.12.12+20251014-x86_64-pc-windows-msvc-install_only.tar.gz"
$Sha = "2d670beb3b930d30e3a13cc909923a001dbdfcb5537692d5da40b6b41643ce1c"
$Tag = "20251014"

function Write-Log($msg) { Write-Host "[ps-setup] $msg" }

# venv 已就绪，直接进内核
$VenvPy = Join-Path $DepsRoot "toolchain\python\venv\Scripts\python.exe"
if (Test-Path $VenvPy) {
    & $VenvPy $ToolPy @args
    exit $LASTEXITCODE
}

# venv 没了但解释器还在，先用它进内核
$PbsDst = Join-Path $DepsRoot "toolchain\python\pbs"
$PbsPy = Join-Path $PbsDst "python\python.exe"
if (Test-Path $PbsPy) {
    & $PbsPy $ToolPy @args
    exit $LASTEXITCODE
}

# 下载 pbs 解释器并解压；Win10 1803+ 自带 tar.exe，不用装第三方工具
$Rel = "$Tag/$File"
$Tmp = Join-Path $env:TEMP $File
$Got = $false
foreach ($M in ("https://registry.npmmirror.com/-/binary/python-build-standalone", "https://python-standalone.org/mirror/astral-sh/python-build-standalone", "https://gh-proxy.com/https://github.com/astral-sh/python-build-standalone/releases/download", "https://ghfast.top/https://github.com/astral-sh/python-build-standalone/releases/download", "https://github.com/astral-sh/python-build-standalone/releases/download")) {
    $Url = $M.TrimEnd("/") + "/" + $Rel
    Write-Log "尝试 Python 源：$([uri]$Url | Select-Object -ExpandProperty Host)"
    try {
        Invoke-WebRequest -Uri $Url -OutFile $Tmp -UseBasicParsing -TimeoutSec 600
        $hash = (Get-FileHash $Tmp -Algorithm SHA256).Hash.ToLower()
        if ($hash -ne $Sha) { Write-Log "  sha256 不符，换下一源"; continue }
        $Got = $true
        break
    } catch { Write-Log "  失败：$($_.Exception.Message)" }
}
if (-not $Got) {
    Write-Log "受管 Python 下载失败（$File，期望 sha256=$Sha）"
    Write-Log "可手动下载 $Rel 解压到 $PbsDst"
    exit 1
}
New-Item -ItemType Directory -Force -Path $PbsDst | Out-Null
tar -xf $Tmp -C $PbsDst        # pbs 顶层自带 python/ 目录；Win10 1803+ 自带 tar.exe
Remove-Item $Tmp -Force
if (-not (Test-Path $PbsPy)) { Write-Log "Python 解压后不可执行，检查 $PbsDst"; exit 1 }
Write-Log "受管 Python 已就位：$PbsPy（sha256 校验通过）"
& $PbsPy $ToolPy @args
exit $LASTEXITCODE
