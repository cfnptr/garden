#!/bin/bash
cd "$(dirname "$BASH_SOURCE")"

if ! cmake --version &> /dev/null; then
    echo "Failed to get CMake version, please check if it's installed."
    exit 1
fi

echo "Configuring project..."

if [[ "$OSTYPE" == "msys" ]]; then
    cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -T ClangCL -A x64 -S ../ -B ../build-debug/
elif [[ "$OSTYPE" == "darwin"* ]]; then
    cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -S ../ -B ../build-debug/
else
    cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -D GARDEN_USE_CLANG=TRUE -S ../ -B ../build-debug/
fi

status=$?

if [ $status -ne 0 ]; then
    echo "Failed to configure CMake project."
    exit $status
fi

echo ""
echo "Building project..."

cmake --build ../build-debug/ --config Debug --parallel
status=$?

if [ $status -ne 0 ]; then
    echo "Failed to build CMake project."
    exit $status
fi

if [[ "$OSTYPE" == "darwin"* ]]; then
    echo ""
    echo "Fixing up macOS bundle..."

    cmake --install ../build-debug/ --component FixupBundle
    status=$?

    if [ $status -ne 0 ]; then
        echo "Failed to fix up macOS bundle."
        exit $status
    fi
fi

exit 0
