# Build instructions

## Operating Systems

* [Window (10/11)](BUILDING.md#windows-1011)
* [Ubuntu (22.04 LTS)](BUILDING.md#ubuntu-2204-lts)
* [macOS (Ventura 13)](BUILDING.md#macos-ventura-13)
* [Build Project](BUILDING.md#build-project)

# Windows (10/11)

## 1. Install Visual Studio Community (Or any other IDE)

1. Download latest version from [visualstudio.microsoft.com](https://visualstudio.microsoft.com/downloads)
2. Launch downloaded Visual Studio installer
3. Select "Desktop development with C++" before installation
4. Add "C++ Clang tools for Windows" inside **Installation details**
5. Then click **Install** button to begin installation

## 2. Install Git

* Download and install latest version from [git-cms.com](https://git-scm.com/downloads)

You may use default Git install options or chose any other default Git editor instead of **Vim**.

## 3. Install CMake

1. Download latest release version of installer from [cmake.org](https://cmake.org/download)
2. Select "Add CMake to the system PATH for the current user" during installation

## 4. Install Vulkan SDK

* Download and install latest version from [vulkan.lunarg.com](https://vulkan.lunarg.com) for **Windows**

To build the engine you will only need **The Vulkan SDK Core** components.

## 5. Install OpenSSL

1. Install **Chocolatey** package manager from [chocolatey.org](https://chocolatey.org/install)
2. Run ```choco install openssl``` using **Terminal** or **CMD**


# Ubuntu (22.04 LTS)

## 1. Install Visual Studio Code (Or any other IDE)

1. Download and install latest version from [code.visualstudio.com](https://code.visualstudio.com/download)
2. Install "C/C++" And "CMake Tools" extensions inside **Visual Studio Code**

## 2. Install required packages

1. Run ```sudo apt-get update``` using **Terminal** app
2. And ```sudo apt-get install git gcc g++ cmake clang xorg-dev libssl-dev```

## 3. Install Vulkan SDK

* Install latest version from [vulkan.lunarg.com](https://vulkan.lunarg.com) for **Linux**

Use "Ubuntu Packages" tab and "Latest Supported Release" instructions for that.


# macOS (Ventura 13)

## 1. Install Xcode (Or any other IDE)

1. Download and install latest version from **App Store** app
2. Run ```xcode-select --install``` using **Terminal** app to install Xcode tools

## 2. Install required packages

1. Install **Homebrew** package manager from [brew.sh](https://brew.sh)
2. Run ```brew update``` using **Terminal** app
3. And ```brew install git cmake openssl```

## 3. Install Vulkan SDK

1. Download latest version from [vulkan.lunarg.com](https://vulkan.lunarg.com) for **macOS**
2. During **Select Components** screen "System Global Installation" should be checked

To build the engine you will only need The Vulkan SDK Core components.


# Build Project

To build the project run one of the [scripts](scripts/) using **Terminal**, **Git Bash** or build it using **IDE**.

## Visual Studio (2022)

1. Open **Visual Studio 2022** IDE
2. Click "Open a local folder" and open the repository folder
3. Click **Build -> Build All** to build the project

## Visual Studio Code (VS Code)

1. Open **Visual Studio Code** IDE
2. Install "C/C++" And "CMake Tools" extensions
3. Click **File -> Open Folder...** and open the repository folder
4. Click **Yes** in "Would you like to configure project..."
5. Select one of the compiler **Kits** in the opened window
6. Click **Build** button at the bottom bar to build the project