#!/bin/bash
cd "$(dirname "$BASH_SOURCE")"

curl --version > /dev/null
status=$?

if [ $status -ne 0 ]; then
    echo "Failed to get cURL version, please check if it's installed."
    exit $status
fi

if [[ "$OSTYPE" == "darwin"* ]]; then
    vulkanVersion=$(curl -s https://vulkan.lunarg.com/sdk/latest/mac.txt)
    echo "Latest macOS Vulkan SDK version: $vulkanVersion"

    curl -O https://sdk.lunarg.com/sdk/download/latest/mac/vulkan_sdk.zip
    status=$?

    if [ $status -ne 0 ]; then
        echo "cURL failed to download Vulkan SDK archive."
        exit $status
    fi

    unzip vulkan_sdk.zip
    status=$?

    if [ $status -ne 0 ]; then
        echo "Failed to extract Vulkan SDK archive."
        exit $status
    fi

    sudo ./vulkansdk-macOS-$vulkanVersion.app/Contents/MacOS/vulkansdk-macOS-$vulkanVersion --al --da -c install com.lunarg.vulkan.core com.lunarg.vulkan.usr
    status=$?

    if [ $status -ne 0 ]; then
        echo "Failed to install Vulkan SDK."
        exit $status
    fi
else
    vulkanVersion=$(curl -s https://vulkan.lunarg.com/sdk/latest/linux.txt)
    echo "Latest Linux Vulkan SDK version: $vulkanVersion"

    export VULKAN_SDK=~/vulkan-sdk/$vulkanVersion/x86_64
    cd ~ && mkdir vulkan-sdk
    curl -O https://sdk.lunarg.com/sdk/download/latest/linux/vulkan_sdk.tar.xz
    status=$?

    if [ $status -ne 0 ]; then
        echo "cURL failed to download Vulkan SDK archive."
        exit $status
    fi

    tar xf vulkan_sdk.tar.xz -C vulkan-sdk
    status=$?

    if [ $status -ne 0 ]; then
        echo "Failed to extract Vulkan SDK archive."
        exit $status
    fi

    sudo cp -r $VULKAN_SDK/include/vulkan/ /usr/local/include/
    sudo cp -P $VULKAN_SDK/lib/libvulkan.so* /usr/local/lib/
fi

exit 0
