#!/bin/bash
cd "$(dirname "$BASH_SOURCE")"

glslangValidator --version > /dev/null
status=$?

if [ $status -ne 0 ]; then
    echo "Failed to get glslangValidator version, please check if it's installed."
    exit $status
fi

glslangValidator -V -x -o shader.frag.u32 ../resources/_imgui.frag
status=$?

if [ $status -ne 0 ]; then
    echo "Failed to compile ImGui shader."
    exit $status
fi

exit 0
