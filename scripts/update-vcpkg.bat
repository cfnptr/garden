@ECHO OFF
CD /D "%~dp0"

PowerShell -NoProfile -ExecutionPolicy Bypass -Command "& './update-vcpkg.ps1'"

IF NOT %ERRORLEVEL% == 0 (
    ECHO Failed to run update-vcpkg PowerShell script.
    EXIT /B %ERRORLEVEL%
)

EXIT /B 0