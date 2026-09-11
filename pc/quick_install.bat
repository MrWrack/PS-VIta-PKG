@echo off
setlocal
if "%~2"=="" (
  echo Usage: quick_install.bat VITA_IP file.vpk
  echo Example: quick_install.bat 192.168.1.50 MyApp.vpk
  pause
  exit /b 1
)
py "%~dp0send_vpk.py" "%~1" "%~2"
pause
