@ECHO OFF
CD /D "%~dp0"

glslangValidator --version > nul

IF NOT %ERRORLEVEL% == 0 (
    ECHO Failed to get glslangValidator version, please check if it's installed.
    EXIT /B %ERRORLEVEL%
)

glslangValidator -V -x -o shader.frag.u32 ../resources/_imgui.frag

IF NOT %ERRORLEVEL% == 0 (
    ECHO Failed to compile ImGui shader.
    EXIT /B %ERRORLEVEL%
)

EXIT /B 0