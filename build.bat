@echo off
setlocal

call "%~dp0build.cmd"
set "BUILD_EXIT=%ERRORLEVEL%"

if not "%BUILD_EXIT%"=="0" (
echo.
echo Build failed with exit code %BUILD_EXIT%.
exit /b %BUILD_EXIT%
)

echo.
pause
