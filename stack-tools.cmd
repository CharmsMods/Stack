@echo off
setlocal

set "POWERSHELL_EXE=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
set "MENU_SCRIPT=%~dp0tools\stack_menu.ps1"

if not exist "%POWERSHELL_EXE%" (
echo PowerShell was not found at "%POWERSHELL_EXE%".
exit /b 1
)

if not exist "%MENU_SCRIPT%" (
echo Stack menu script was not found at "%MENU_SCRIPT%".
exit /b 1
)

"%POWERSHELL_EXE%" -NoProfile -ExecutionPolicy Bypass -File "%MENU_SCRIPT%" -Root "%~dp0."
exit /b %ERRORLEVEL%
