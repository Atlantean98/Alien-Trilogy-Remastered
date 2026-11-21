@echo off
rem Alien Trilogy level editor (3D): starts a small local server and opens the editor in your web browser.
rem Close this window to quit the editor.
cd /d "%~dp0"
python editor_server.py
if errorlevel 1 (
  echo.
  echo The editor needs Python 3 with Pillow: https://www.python.org then "py -m pip install pillow"
  pause
)
