#!/bin/bash
cd "$(dirname "$BASH_SOURCE")"

if command -v brew &> /dev/null; then
    echo "Skipping Vulkan SDK installation, use macOS installer instead."
    exit 0
fi

NEW_SDK_VERSION=$(curl -s "https://vulkan.lunarg.com/sdk/latest/linux.txt" | tr -d '\r\n')

if [[ -f "$HOME/VulkanSDK/version.txt" ]]; then
    OLD_SDK_VERSION=$(tr -d '\r\n' < "$HOME/VulkanSDK/version.txt")
    if [[ "$NEW_SDK_VERSION" == "$OLD_SDK_VERSION" ]]; then
        echo "Skipping Vulkan SDK installation, already up to date."
        exit 0
    fi
fi

echo
echo "Installing Vulkan SDK $NEW_SDK_VERSION version..."

curl -O "https://sdk.lunarg.com/sdk/download/latest/linux/vulkan_sdk.tar.xz"
rm -rf "$HOME/VulkanSDK" && mkdir "$HOME/VulkanSDK"
tar -xf "vulkan_sdk.tar.xz" --strip-components=1 -C "$HOME/VulkanSDK"
echo "$NEW_SDK_VERSION" > "$HOME/VulkanSDK/version.txt"
rm -f "vulkan_sdk.tar.xz"

CONF_FILES=("$HOME/.bashrc" "$HOME/.zshrc")
SOURCE_SETUP_ENV='source "$HOME/VulkanSDK/setup-env.sh" > /dev/null'

for CONF_FILE in "${CONF_FILES[@]}"; do
    if ! grep -Fq "$SOURCE_SETUP_ENV" "$CONF_FILE"; then
        echo "" >> "$CONF_FILE"
        echo "# Lines configured by Garden install-packages" >> "$CONF_FILE"
        echo "$SOURCE_SETUP_ENV" >> "$CONF_FILE"
        echo "Added setup-env.sh source to the '${CONF_FILE}'"
    fi
done

source "$HOME/VulkanSDK/setup-env.sh"
