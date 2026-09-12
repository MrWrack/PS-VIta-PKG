@echo off
cd /d "%~dp0"
py -3 vpk_manager_pc.py
if errorlevel 1 python vpk_manager_pc.py
pause
