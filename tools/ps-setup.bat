@echo off
rem ps-setup.bat — cmd 兜底入口（bat 不受 PowerShell ExecutionPolicy 限制，双击可用）。
rem 实际逻辑在 tools\ps-setup.ps1，本文件只负责绕过策略调起它。
powershell -ExecutionPolicy Bypass -NoProfile -File "%~dp0ps-setup.ps1" %*
exit /b %ERRORLEVEL%
