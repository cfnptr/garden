@ECHO OFF
cd /D "%~dp0"

cmake --version > nul

IF NOT %ERRORLEVEL% == 0 (
    ECHO Failed to get CMake version, please check if it's installed.
    EXIT /B %ERRORLEVEL%
)

ECHO Configuring project...

cmake -DCMAKE_BUILD_TYPE=Release -S ../ -B ../build-release/

IF NOT %ERRORLEVEL% == 0 (
    ECHO Failed to configure CMake project.
    EXIT /B %ERRORLEVEL%
)

ECHO(
ECHO Building project...

cmake --build ../build-release/ --config Release --parallel

IF NOT %ERRORLEVEL% == 0 (
    ECHO Failed to build CMake project.
    EXIT /B %ERRORLEVEL%
)

EXIT /B 0