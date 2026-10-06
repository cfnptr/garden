# How to use Garden with Flatpak VSCodium?

* Restrict full host access and remove `org.freedesktop.Flatpak`. (optional)
* Get the VSCodium current runtime version: ```flatpak info com.vscodium.codium | grep Runtime```
* Install latest LLVM extension for target runtime: ```flatpak install flathub org.freedesktop.Sdk.Extension.llvm22```
* Add it to the VSCodium: ```flatpak override --user --env=FLATPAK_ENABLE_SDK_EXT=llvm22 com.vscodium.codium```
* Restart the VSCodium to apply changes.