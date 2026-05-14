@echo off
setlocal

set "POWERSHELL_EXE=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
set "HELPER=%~dp0tools\use_fixed_env.ps1"

if not exist "%POWERSHELL_EXE%" (
echo PowerShell was not found at "%POWERSHELL_EXE%".
exit /b 1
)

if not exist "%HELPER%" (
echo Helper script was not found at "%HELPER%".
exit /b 1
)

"%POWERSHELL_EXE%" -NoExit -ExecutionPolicy Bypass -Command "& { & '%HELPER%'; Set-Location '%~dp0' }"
exit /b %ERRORLEVEL%
