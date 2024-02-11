#!/bin/bash
cd "$(dirname "$BASH_SOURCE")"

cmake --version > /dev/null
status=$?

if [ $status -ne 0 ]; then
    echo "Failed to get CMake version, please check if it's installed."
    exit $status
fi

echo "Configuring project..."

cmake -G Xcode -DCMAKE_BUILD_TYPE=Debug -S ../ -B ../build-xcode/
status=$?

if [ $status -ne 0 ]; then
    echo "Failed to configure CMake project."
    exit $status
fi

exit 0