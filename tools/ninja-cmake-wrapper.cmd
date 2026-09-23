@echo off
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "F:\CODEX\ArxFatalis VR\tools\ninja-cmake-wrapper.ps1" %*
exit /b %errorlevel%
