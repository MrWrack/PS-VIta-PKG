@echo off
py -m pip install pillow pyinstaller
pyinstaller --onefile --windowed --name VPK_Manager_Theme_Creator theme_creator.py
pause
