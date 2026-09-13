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
py -m PyInstaller --noconfirm --clean --onefile --windowed --noupx --log-level WARN --name "VPK Manager PC" --icon vpk_manager_pc.ico vpk_manager_pc.py
if errorlevel 1 (
  echo.
  echo FEL: EXE-bygget misslyckades.
  pause
  exit /b 1
)

echo.
echo Klar: dist\VPK Manager PC.exe
pause
