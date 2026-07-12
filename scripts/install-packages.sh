#!/bin/bash
cd "$(dirname "$BASH_SOURCE")"

echo "Installing packages required for the Garden engine compilation..."

if command -v apt-get &> /dev/null; then
    sudo apt-get update
    sudo apt-get install -y git cmake build-essential ninja-build clang lld lldb zlib1g-dev libssl-dev libcurl4-openssl-dev libwayland-dev libxkbcommon-dev xorg-dev libassimp-dev libopenexr-dev
elif command -v dnf &> /dev/null; then
    sudo dnf check-update
    sudo dnf install -y git cmake @c-development ninja-build clang clang-tools-extra lld lldb oxipng zlib-devel openssl-devel libcurl-devel wayland-devel libxkbcommon-devel libXcursor-devel libXi-devel libXinerama-devel libXrandr-devel assimp-devel openexr-devel
elif command -v pacman &> /dev/null; then
    sudo pacman -Syu --noconfirm git cmake base-devel ninja clang lld lldb oxipng openssl curl wayland libxkbcommon libxcursor libxi libxinerama libxrandr assimp openexr
elif command -v zypper &> /dev/null; then
    sudo zypper install -y -t pattern devel_basis
    sudo zypper install -y git cmake ninja clang lld lldb oxipng libopenssl-devel libcurl-devel wayland-devel libxkbcommon-devel libXcursor-devel libXi-devel libXinerama-devel libXrandr-devel assimp-devel openexr-devel
elif command -v apk &> /dev/null; then
    apk add --no-cache git cmake build-base ninja clang lld zlib-static openssl-dev curl-dev wayland-dev libxkbcommon-dev libxcursor-dev libxi-dev libxinerama-dev libxrandr-dev assimp-dev openexr-dev
elif command -v brew &> /dev/null; then
    brew update
    brew install git cmake ninja zlib openssl curl oxipng assimp openexr
else
    echo "Error: No supported package manager found!"
    exit 1
fi

echo
echo "Installing Vulkan SDK..."
rm -rf ~/vulkan-sdk

if ! command -v brew &> /dev/null; then
    curl -O https://sdk.lunarg.com/sdk/download/latest/linux/vulkan_sdk.tar.xz
    mkdir ~/vulkan-sdk && tar -xf vulkan_sdk.tar.xz --strip-components=1 -C ~/vulkan-sdk
    rm -f vulkan_sdk.tar.xz

    CONF_FILES=("$HOME/.bashrc" "$HOME/.zshrc")
    SOURCE_SETUP_ENV="source ~/vulkan-sdk/setup-env.sh > /dev/null"

    for CONF_FILE in "${CONF_FILES[@]}"; do
        if ! grep -Fq "$SOURCE_SETUP_ENV" "$CONF_FILE"; then
            echo "" >> "$CONF_FILE"
            echo "# Lines configured by Garden install-packages" >> "$CONF_FILE"
            echo "$SOURCE_SETUP_ENV" >> "$CONF_FILE"
            echo "Added setup-env.sh source to the '${CONF_FILE}'"
        fi
    done
    ~/vulkan-sdk/setup-env.sh
fi
