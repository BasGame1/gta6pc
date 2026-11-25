@echo off
set "HELPER=%~dp0helpers\helper.bat"

if not exist "%HELPER%" (
    echo Could not load helper, exiting
    exit /b 255
)

call "%HELPER%" banner
echo.

::Placeholder for init
