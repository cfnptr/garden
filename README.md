# GARDEN ![CI](https://github.com/cfnptr/garden/actions/workflows/cmake.yml/badge.svg)

Garden is an open-source, cross-platform game engine designed for efficiency and flexibility.
Written in C++ and utilizing the Vulkan API for rendering, it is aimed at providing
developers with a robust toolset for creating high-performance, visually stunning games.

## Features

* Cross-Platform Compatibility: Runs seamlessly on multiple platforms.
* Vulkan API Rendering Backend: High-efficiency graphics and compute operations.
* Modern C++ Design: Written in modern C++ for clarity, performance, and reliability.
* Extensible Architecture: Designed to be easily extendable and modifiable.
* TODO: add more features

## Supported operating systems

* Ubuntu (Linux)
* MacOS
* Windows

## Build requirements

* [Clang 12.0+](https://clang.llvm.org/)
* [Git 2.30+](https://git-scm.com/)
* [CMake 3.22+](https://cmake.org/)
* [Vulkan SDK 1.3+](https://vulkan.lunarg.com/)
* [X11](https://www.x.org/) (Linux only)
* [OpenSSL 3.0+](https://openssl.org/)
* Execute scripts/build-physx.sh

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