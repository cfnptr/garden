# Build instructions

* [Window](BUILDING.md#windows)
* [macOS](BUILDING.md#macos)
* [Linux](BUILDING.md#linux)


# Windows

### 1. Install Visual Studio Community

1. Download latest version from [visualstudio.microsoft.com](https://visualstudio.microsoft.com/downloads)
2. Launch downloaded Visual Studio installer
3. Select "Desktop development with C++" before installation
4. Add "C++ Clang tools for Windows" inside **Installation details**
5. Unselect "vcpkg package manager" inside **Installation details**, we will use our own! <---
6. Finally click **Install** button to begin installation

Alternatively you can install and use [CLion](https://www.jetbrains.com/clion/), [VSCode](https://code.visualstudio.com/) or any other IDE.

### 2. Install Git

* Download and install latest version from [git-cms.com](https://git-scm.com/install/windows)

You may use default Git install options or chose any other default Git editor instead of **Vim**.

### 3. Install CMake

1. Download latest release version of installer from [cmake.org](https://cmake.org/download)
2. Select "Add CMake to the system PATH for the current user" during installation

### 4. Install Vulkan SDK

* Download and install latest version from [vulkan.lunarg.com](https://vulkan.lunarg.com) for **Windows**

To build the project you will only need **The Vulkan SDK Core** components.

### 5. Clone repository

1. Open **Terminal** or **CMD** app to execute following commands
2. Change current working directory using ```cd``` command where to clone the project. (Google it)
3. Run ```git clone --recursive -j8 https://github.com/cfnptr/garden``` command to download the project
4. Run ```cd garden/``` to enter the project directory

Note! Use appropriate github link if **Garden** engine is used as a third-party library.

### 6. Install vcpkg package manager

1. Reopen **CMD** or **Terminal** app as Administrator. (Right click the app)
2. Run ```scripts/update-vcpkg.bat``` command from the project *scripts/* directory
3. Reopen **CMD** or **Terminal** app to get updated system environment variables.

Or

1. Follow installation steps from [learn.microsoft.com](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started) (Better to choose C:\vcpkg path)
2. Run ```vcpkg integrate install``` command using **Terminal** or **CMD** app to make packages available user-wide
3. Add **vcpkg** to the System Environment Variables. (Google it)

### 7. Install required packages

1. Run ```vcpkg install zlib openssl curl assimp openexr --triplet x64-windows-static``` using **Terminal** or **CMD** app

Alternatively run ```install-packages.bat``` from the project **scripts/** directory


# macOS

### 1. Install Xcode

1. Download and install latest version from the built-in [App Store](https://apps.apple.com/app/xcode/id497799835)
2. Run ```xcode-select --install``` command using **Terminal** app to install Xcode tools

### 2. Install required packages

1. Install **Homebrew** package manager from [brew.sh](https://brew.sh)
2. Run ```brew update``` command using **Terminal** app to update package list
3. And run ```brew install git cmake zlib openssl curl assimp openexr``` command to install packages

### 3. Install Vulkan SDK

1. Download latest version from [vulkan.lunarg.com](https://vulkan.lunarg.com) for **macOS**
2. During **Select Components** screen "System Global Installation" should be checked

To build the project you will only need The **Vulkan SDK Core** components.


# Linux

### 1. Install Visual Studio Code IDE

1. Download and install latest version from [code.visualstudio.com](https://code.visualstudio.com/download) or from built-in store
2. Install "[C/C++](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools)" and "[CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools)" extensions inside the **VS Code**

Or 

1. Download and install from [vscodium.com](https://vscodium.com/) or from built-in store (Non-flatpack version!)
2. Install "[clangd](https://open-vsx.org/extension/llvm-vs-code-extensions/vscode-clangd)", "[CodeLLDB](https://open-vsx.org/extension/vadimcn/vscode-lldb)" an "[CMake Tools](https://open-vsx.org/extension/ms-vscode/cmake-tools)" extensions inside the **VSCodium**


Alternatively you can install and use [CLion](https://www.jetbrains.com/clion/) or any other IDE.

### 2. Install required packages

* Execute [scripts/install-packages.sh](scripts/install-packages.sh) script

Or

### For Ubuntu/Debian

1. Run ```sudo apt update``` command using **Terminal** app to update package list
2. And ```sudo apt install build-essential ninja-build git cmake clang lld lldb glslc libvulkan-dev vulkan-validationlayers zlib1g-dev libssl-dev libcurl4-openssl-dev libwayland-dev libxkbcommon-dev xorg-dev libassimp-dev libopenexr-dev```


# Build project (Compile)

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
