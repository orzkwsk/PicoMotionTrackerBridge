@echo off
setlocal
cd /d "%~dp0"

echo Starting PICO Backend...
call "%~dp0Start_PICO_Backend.cmd"
if errorlevel 1 exit /b %ERRORLEVEL%

echo Starting SteamVR Control GUI...
call "%~dp0Start_GUI.cmd"
exit /b %ERRORLEVEL%
