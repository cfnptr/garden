#!/bin/bash
cd "$(dirname "$BASH_SOURCE")"

cmake --version > /dev/null
status=$?

if [ $status -ne 0 ]; then
    echo "Failed to get CMake version, please check if it's installed."
    exit $status
fi

echo "Configuring project..."

if [[ "$OSTYPE" == "msys" ]]; then
    cmake -DCMAKE_BUILD_TYPE=Debug -T ClangCL -A x64 -S ../ -B ../build-debug-clang/
elif [[ "$OSTYPE" == "darwin"* ]]; then
    cmake -DCMAKE_BUILD_TYPE=Debug -S ../ -B ../build-debug-clang/
else
    cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -S ../ -B ../build-debug-clang/
fi

status=$?

if [ $status -ne 0 ]; then
    echo "Failed to configure CMake project."
    exit $status
fi

echo ""
echo "Building project..."

cmake --build ../build-debug-clang/ --config Debug --parallel
status=$?

if [ $status -ne 0 ]; then
    echo "Failed to build CMake project."
    exit $status
fi

if [[ "$OSTYPE" == "darwin"* ]]; then
    echo ""
    echo "Fixing up macOS bundle..."

    cmake --install ../build-debug-clang/ --component FixupBundle
    status=$?

    if [ $status -ne 0 ]; then
        echo "Failed to fix up macOS bundle."
        exit $status
    fi
fi

exit 0