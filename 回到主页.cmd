@echo off
chcp 65001 >nul
setlocal

rem 把设备切回主页启动器：清空 otadata，使引导器启动 ota_0。
rem 用法: 回到主页.cmd [串口号]    默认 COM5

set "PORT=%~1"
if "%PORT%"=="" set "PORT=COM5"

set "ESPTOOL="
where esptool >nul 2>nul
if not errorlevel 1 set "ESPTOOL=esptool"
if not defined ESPTOOL if exist "%USERPROFILE%\.platformio\penv\Scripts\esptool.exe" set "ESPTOOL=%USERPROFILE%\.platformio\penv\Scripts\esptool.exe"
if not defined ESPTOOL (
  echo 找不到 esptool。请先安装 PlatformIO，或把 esptool 加入 PATH。
  pause
  exit /b 1
)

echo 正在把设备切回主页启动器：清空 otadata，让引导器启动 ota_0 ...
"%ESPTOOL%" --chip esp32c6 --port %PORT% --baud 921600 --before default-reset --after hard-reset erase-region 0xF000 0x2000

if errorlevel 1 (
  echo.
  echo 失败：请确认串口号（默认 COM5，可传参，例如 回到主页.cmd COM5）。
) else (
  echo.
  echo 完成，设备已重启进入主页。
)
pause