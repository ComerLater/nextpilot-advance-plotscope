@echo off
set CI=1
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
cd /d C:\Users\zhanfuyu\WorkBuddy\plotscope
cmake -S . -B build-proof -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=D:/program/Qt/6.12.0/msvc2022_64 -DPS_BUILD_TESTS=OFF
exit /b %errorlevel%
