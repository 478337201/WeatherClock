@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0inject_wifi_defines.ps1"
if errorlevel 1 exit /b 1
exit /b 0
