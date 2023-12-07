# Windows (10/11)

## 1. Install Visual Studio Community (Or any other IDE)

1. Download latest version from [visualstudio.microsoft.com](https://visualstudio.microsoft.com/downloads)
2. Launch downloaded Visual Studio **installer**
3. Select "Desktop development with C++" before installation
4. Add "C++ Clang tools for Windows" inside **Installation details**
5. Then click **Install** button to begin installation

## 2. Install Git

* Download and install latest version from [git-cms.com](https://git-scm.com/downloads)

You may chose any other default Git editor instead of Vim during installation.

## 3. Install CMake

1. Download latest release version from [cmake.org](https://cmake.org/download)
2. Select "Add CMake to the system PATH for the current user" during installation

## 4. Install Vulkan SDK

* Download and install latest version from [vulkan.lunarg.com](https://vulkan.lunarg.com) for **Windows**


# Ubuntu (22.04 LTS)

## 1. Install Visual Studio Code (Or any other IDE)

1. Download and install latest version from [code.visualstudio.com](https://code.visualstudio.com/download)
2. Install "C/C++" And "CMake Tools" extensions inside **VS Code**

## 2. Install required packages

1. ```sudo apt-get update```
2. ```sudo apt-get install git cmake clang xorg-dev libssl-dev```

## 3. Install Vulkan SDK

* Install latest version from [vulkan.lunarg.com](https://vulkan.lunarg.com) for **Linux**

Use "Ubuntu Packages" tab and "Latest Supported Release" instructions for that.


# macOS (Monterey 12)

## 1. Install Xcode (Or any other IDE)

1. Download and install latest version from **App Store** app
2. Install tools using ```xcode-select --install``` in **Terminal**

## 2. Install required packages

1. Install **Homebrew** from [brew.sh](https://brew.sh)
2. ```brew update```
3. ```brew install git cmake openssl```

## 3. Install Vulkan SDK

* Download latest version from [vulkan.lunarg.com](https://vulkan.lunarg.com) for **macOS**