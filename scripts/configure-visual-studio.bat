@ECHO OFF
CD /D "%~dp0"

cmake --version > nul

IF NOT %ERRORLEVEL% == 0 (
    ECHO Failed to get CMake version, please check if it's installed.
    EXIT /B %ERRORLEVEL%
)

ECHO Configuring project...

cmake -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Debug -S ../ -B ../build-visual-studio/

IF NOT %ERRORLEVEL% == 0 (
    ECHO Failed to configure CMake project.
    EXIT /B %ERRORLEVEL%
)

EXIT /B 0