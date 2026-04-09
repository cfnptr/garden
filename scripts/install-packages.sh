#!/bin/bash
cd "$(dirname "$BASH_SOURCE")"

if command -v apt-get &> /dev/null; then
    sudo apt-get update && sudo apt-get install -y git cmake build-essential ninja-build gdb clang lld lldb glslc libvulkan-dev vulkan-validationlayers zlib1g-dev libssl-dev libcurl4-openssl-dev libwayland-dev libxkbcommon-dev xorg-dev libassimp-dev
elif command -v dnf &> /dev/null; then
    sudo dnf check-update && sudo dnf install -y git cmake @c-development ninja-build clang clang-tools-extra lld lldb glslc vulkan-loader-devel vulkan-headers vulkan-validation-layers-devel zlib-devel openssl-devel libcurl-devel wayland-devel libxkbcommon-devel libXcursor-devel libXi-devel libXinerama-devel libXrandr-devel assimp-devel
elif command -v pacman &> /dev/null; then
    sudo pacman -Syu --noconfirm git cmake base-devel ninja clang lld lldb shaderc vulkan-headers vulkan-validation-layers zlib openssl curl wayland libxkbcommon libxcb assimp
elif command -v zypper &> /dev/null; then
    sudo zypper install -y git cmake ninja clang lld lldb shaderc vulkan-devel vulkan-validation-layers zlib-devel libopenssl-devel libcurl-devel wayland-devel libxkbcommon-devel libxcb-devel assimp-devel  -t pattern devel_basis
elif command -v apk &> /dev/null; then
    apk add --no-cache git cmake build-base ninja clang lld shaderc vulkan-headers vulkan-validation-layers zlib-dev openssl-dev curl-dev wayland-dev libxkbcommon-dev libxcb-dev assimp-dev
elif command -v brew &> /dev/null; then
    brew update && brew install git cmake ninja zlib openssl curl assimp
else
    echo "No supported package manager found."
    exit 1
fi
