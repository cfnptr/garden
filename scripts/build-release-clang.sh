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
    cmake -DCMAKE_BUILD_TYPE=Release -T ClangCL -A x64 -S ../ -B ../build-release-clang/
elif [[ "$OSTYPE" == "darwin"* ]]; then
    cmake -DCMAKE_BUILD_TYPE=Release -S ../ -B ../build-release-clang/
else
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/clang-cpp -S ../ -B ../build-release-clang/
fi

status=$?

if [ $status -ne 0 ]; then
    echo "Failed to configure CMake project."
    exit $status
fi

echo ""
echo "Building project..."

cmake --build ../build-release-clang/ --config Release --parallel
status=$?

if [ $status -ne 0 ]; then
    echo "Failed to build CMake project."
    exit $status
fi

if [[ "$OSTYPE" == "darwin"* ]]; then
    echo ""
    echo "Fixing up macOS bundle..."

    cmake --install ../build-release-clang/ --component FixupBundle
    status=$?

    if [ $status -ne 0 ]; then
        echo "Failed to fix up macOS bundle."
        exit $status
    fi
fi

exit 0