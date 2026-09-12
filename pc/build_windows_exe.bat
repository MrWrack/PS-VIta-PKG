@echo off
setlocal
cd /d "%~dp0"

echo Installerar/uppdaterar PyInstaller...
py -m pip install --upgrade pyinstaller
if errorlevel 1 (
  echo.
  echo FEL: Kunde inte installera PyInstaller.
  pause
  exit /b 1
)

echo.
echo Bygger VPK Manager PC.exe...
py -m PyInstaller --noconfirm --clean --onefile --windowed --name "VPK Manager PC" vpk_manager_pc.py
if errorlevel 1 (
  echo.
  echo FEL: EXE-bygget misslyckades.
  pause
  exit /b 1
)

echo.
echo Klar: dist\VPK Manager PC.exe
pause
