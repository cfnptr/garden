#!/bin/bash
cd "$(dirname "$BASH_SOURCE")"

cmake --version > /dev/null
status=$?

if [ $status -ne 0 ]; then
    echo "Failed to get CMake version, please check if it's installed."
    exit $status
fi

echo "Configuring project..."

cmake -DCMAKE_BUILD_TYPE=Debug -S ../ -B ../build-debug/
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