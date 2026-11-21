@echo off
if not exist "%~dp0decompilation\cpp\build\recovered_world_explorer.exe" (
    echo The exploration executable has not been built yet.
    echo Build the recovered C++ project first, then run this launcher again.
    pause
    exit /b 1
)
start "" "%~dp0decompilation\cpp\build\recovered_world_explorer.exe"
