@echo off
rem The older top-down level editor (same project files as the 3D editor).
cd /d "%~dp0"
python level_editor_2d.py
if errorlevel 1 (
  echo.
  echo The editor needs Python 3 with Pillow: https://www.python.org then "py -m pip install pillow"
  pause
)
