@echo off
setlocal
cd /d "%~dp0"
py -m pip install --upgrade pyinstaller pillow
py -m PyInstaller --noconfirm --clean --onefile --windowed --name "VPK Manager PC" vpk_manager_pc.py
if errorlevel 1 pause & exit /b 1
echo.
echo Klar: dist\VPK Manager PC.exe
pause
