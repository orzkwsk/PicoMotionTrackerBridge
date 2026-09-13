@echo off
setlocal
cd /d "%~dp0"

echo Checking PC bridge runtime...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\ensure_pc_runtime.ps1"
if errorlevel 1 exit /b %ERRORLEVEL%

echo Starting PC Bridge Service...
call "%~dp0Start_Bridge_Service.cmd"
if errorlevel 1 exit /b %ERRORLEVEL%

echo Starting PICO Backend...
call "%~dp0Start_PICO_Backend.cmd"
if errorlevel 1 exit /b %ERRORLEVEL%

echo Starting SteamVR Control GUI...
call "%~dp0Start_GUI.cmd"
exit /b %ERRORLEVEL%
