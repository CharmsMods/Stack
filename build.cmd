@echo off
setlocal

set "ROOT=%~dp0."
set "BUILD_DIR=%~dp0build_codex"
set "POWERSHELL_EXE=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
set "BUILD_SCRIPT=%~dp0tools\build_release.ps1"

echo Closing any running instances of Stack.exe...
"%SystemRoot%\System32\taskkill.exe" /F /IM Stack.exe >nul 2>&1

echo.
echo Configuring and building Stack...
"%POWERSHELL_EXE%" -NoProfile -ExecutionPolicy Bypass -File "%BUILD_SCRIPT%" -Root "%ROOT%" -BuildDir "%BUILD_DIR%"
set "BUILD_EXIT=%ERRORLEVEL%"

echo.
if not "%BUILD_EXIT%"=="0" (
echo Build failed with exit code %BUILD_EXIT%.
exit /b %BUILD_EXIT%
)

echo Build complete!
echo Portable bundle: %BUILD_DIR%\portable\Stack.exe
