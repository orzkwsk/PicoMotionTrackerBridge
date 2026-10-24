@echo off
python "%~dp0scripts\launch_artifact_set.py" pico %*
exit /b %ERRORLEVEL%
