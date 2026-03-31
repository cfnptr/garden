# Build instructions

## Operating Systems

* [Window](BUILDING.md#windows)
* [macOS](BUILDING.md#macos)
* [Ubuntu](BUILDING.md#ubuntu)
* [Fedora](BUILDING.md#fedora)
* [Build Project](BUILDING.md#build-project)


# Windows

### 1. Install Visual Studio Community (Or any other IDE)

1. Download latest version from [visualstudio.microsoft.com](https://visualstudio.microsoft.com/downloads)
2. Launch downloaded Visual Studio installer
3. Select "Desktop development with C++" before installation
4. Add "C++ Clang tools for Windows" inside **Installation details** (optional)
5. Unselect "vcpkg package manager" inside **Installation details** we will use our own! <---
6. Finally click **Install** button to begin installation

### 2. Install Git

* Download and install latest version from [git-cms.com](https://git-scm.com/install/windows)

You may use default Git install options or chose any other default Git editor instead of **Vim**.

### 3. Install CMake

1. Download latest release version of installer from [cmake.org](https://cmake.org/download)
2. Select "Add CMake to the system PATH for the current user" during installation

### 4. Install Vulkan SDK

* Download and install latest version from [vulkan.lunarg.com](https://vulkan.lunarg.com) for **Windows**

To build the project you will only need **The Vulkan SDK Core** components.

### 5. Clone Repository

1. Open **Terminal** or **CMD** app to execute following commands
2. Change current working directory using ```cd``` command where to clone the project. (Google it)
3. Run ```git clone --recursive https://github.com/cfnptr/garden``` command to download the project
4. Run ```cd garden/``` to enter the project directory

Note! Use appropriate github link if Garden engine is used as a third-party library.

### 6. Install vcpkg package manager

1. Reopen **CMD** or **Terminal** app as Administrator. (Right click the app)
2. Run ```scripts/update-vcpkg.bat``` command from the project *scripts/* directory
3. Reopen **CMD** or **Terminal** app to get updated system environment variables.

Or

1. Follow installation steps from [learn.microsoft.com](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started) (Better to choose C:\vcpkg path)
2. Run ```vcpkg integrate install``` command using **Terminal** or **CMD** app to make packages available user-wide
3. Add **vcpkg** to the System Environment Variables. (Google it)

### 7. Install required packages

1. Run ```vcpkg install zlib:x64-windows-static openssl:x64-windows-static curl:x64-windows-static assimp:x64-windows-static``` using **Terminal** or **CMD** app

Alternatively run ```install-packages.bat``` from the project **scripts/** directory


# macOS

### 1. Install Xcode (Or any other IDE)

1. Download and install latest version from the built-in **App Store**
2. Run ```xcode-select --install``` command using **Terminal** app to install Xcode tools

### 2. Install required packages

1. Install **Homebrew** package manager from [brew.sh](https://brew.sh)
2. Run ```brew update``` command using **Terminal** app to update package list
3. Run ```brew install git cmake zlib openssl curl assimp``` to install packages

### 3. Install Vulkan SDK

1. Download latest version from [vulkan.lunarg.com](https://vulkan.lunarg.com) for **macOS**
2. During **Select Components** screen "System Global Installation" should be checked

To build the project you will only need The Vulkan SDK Core components.


# Ubuntu

### 1. Install Visual Studio Code

1. Install [Visual Studio Code](https://code.visualstudio.com/) from the built-in [App Center](snap://code)
2. Install "[C/C++](vscode:extension/ms-vscode.cpptools)" and "[CMake Tools](vscode:extension/ms-vscode.cmake-tools)" extensions inside the **VS Code**

Alternatively you can install and use [CLion](https://www.jetbrains.com/clion/), [VSCodium](https://vscodium.com/) or any other IDE.

### 2. Install required packages

1. Run ```sudo apt update``` command using **Terminal** app to update package list
2. Run ```sudo apt install build-essential gdb ninja-build git cmake clang lld lldb glslc libvulkan-dev vulkan-validationlayers zlib1g-dev libssl-dev libcurl4-openssl-dev libwayland-dev libxkbcommon-dev xorg-dev libassimp-dev``` to install packages


# Fedora

1. Download and install [VSCodium](https://vscodium.com/) IDE (Non-flatpack version!)
2. Install "[clangd](vscodium:extension/llvm-vs-code-extensions.vscode-clangd)", "[CodeLLDB](vscodium:extension/vadimcn.vscode-lldb)" an "[CMake Tools](
vscodium:extension/ms-vscode.cmake-tools)" extensions inside the **VSCodium**

Alternatively you can install and use [CLion](https://www.jetbrains.com/clion/) or [Visual Studio Code](https://code.visualstudio.com/) IDE.

### 2. Install required packages

1. Run ```sudo dnf check-update``` command using **Terminal** or **Konsole** app to update package list
2. Run ```sudo dnf install @c-development ninja-build git cmake clang clang-tools-extra lld lldb glslc vulkan-loader-devel vulkan-headers vulkan-validation-layers-devel zlib-devel openssl-devel libcurl-devel wayland-devel libxkbcommon-devel libXcursor-devel libXi-devel libXinerama-devel libXrandr-devel assimp-devel``` to install packages


# Build Project (Compile)

Before building the project you should clone it: ```git clone --recursive -j8 <project-url>```<br>
To build the project run one of the [scripts](scripts/) using **Terminal**, **Git Bash** or build it using **IDE**.

### Visual Studio

1. Open **Visual Studio** IDE application
2. Click "Open a project or solution" and open the project **CMakeLists.txt**
3. Wait for project CMake generation to finish
4. Click **Build -> Build All** to build the project

### Visual Studio Code (VS Code and VSCodium)

1. Open **Visual Studio Code** IDE application
3. Click **File -> Open Folder...** and open the project folder
4. Select one of the compiler **Kits** in the opened window (Clang or GCC)
5. Wait for project CMake generation to finish (may take some time)
6. Click **Build** button at the bottom bar to build the project
