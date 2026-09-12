@echo off
cd /d "%~dp0"
py -3 -m pip install --upgrade pyinstaller
py -3 -m PyInstaller --noconfirm --onefile --windowed --name "VPK Manager PC" vpk_manager_pc.py
echo.
echo EXE: dist\VPK Manager PC.exe
pause
