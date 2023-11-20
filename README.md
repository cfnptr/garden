# GARDEN ![CI](https://github.com/cfnptr/garden/actions/workflows/cmake.yml/badge.svg)

Garden is an open-source, cross-platform game engine designed for efficiency <br/>
and flexibility. Written in C++ and utilizing the Vulkan API for rendering, <br/>
it is aimed at providing developers with a robust toolset for creating <br/>
high-performance, visually stunning games.

## Features

* Cross-Platform Compatibility: Runs seamlessly on multiple platforms.
* Vulkan API Rendering Backend: High-efficiency graphics and compute operations.
* Modern C++ Design: Written in modern C++ for clarity, performance, and reliability.
* Extensible Architecture: Designed to be easily extendable and modifiable.
* TODO: add more features

## Supported operating systems

* Ubuntu
* MacOS
* Windows

## Minimum PC system requirements

* OS: Ubuntu 22.04+ / Windows 10+
* GPU: Nvidia GTX 750 / AMD RX 470
* Graphics API: Vulkan 1.3


## Build requirements

* [Clang](https://clang.llvm.org/)
* [Git 2.30+](https://git-scm.com/)
* [CMake 3.22+](https://cmake.org/)
* [Vulkan SDK 1.3+](https://vulkan.lunarg.com/)
* [X11](https://www.x.org/) (Linux only)
* [OpenSSL 3.0+](https://openssl.org/)
* Execute scripts/build-physx.sh

TODO: move generate_projects execution to the cmake.

### X11 installation

* Ubuntu: sudo apt install xorg-dev

### OpenSSL installation

* Ubuntu: sudo apt install libssl-dev
* MacOS: [brew](https://brew.sh/) install openssl
* Windows: [choco](https://chocolatey.org/) install openssl

### Zlib installation (Windows only)

* Download Zlib source code https://www.zlib.net
* cmake --build . --config Release
* cmake --install . --config Release

TODO: maybe remove it and use built-in Zlib?

### CMake options

| Name                  | Description                              | Default value |
|-----------------------|------------------------------------------|---------------|
| GARDEN_RELEASE_EDITOR | Build Garden editor in the release build | `OFF`         |

## Third-party

* TODO