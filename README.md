# GARDEN ![CI](https://github.com/cfnptr/garden/actions/workflows/cmake.yml/badge.svg)

![Engine screenshot](docs/engine-screenshot.png)

Garden is an open source, cross platform game engine designed for efficiency and flexibility.
Written in modern C++ and utilizing the Vulkan API for rendering, it is aimed at providing
developers with a robust toolset for creating high-performance, visually stunning games.
The main features of the engine include extensible architecture, built-in editor,
convenient creation of rendering pipelines using a custom shader language (GSL), and
automatic placement of memory barriers, which are necessary in recent graphics APIs.

## Features

* Cross-Platform Compatibility
* Entity Component System (ECS)
* Built-in Editor (ImGui)
* Deferred Rendering (G-Buffer)
* Physically Based Rendering (PBR)
* Image-Based Lighting (IBL)
* Gamma Correction (HDR)
* Tone Mapping (ACES, Uchimura)
* Automatic Exposure (AE)
* Light Bloom (Glow)
* Screen Space Ambient Occlusion (SSAO)
* Fast Approximate Anti-Aliasing (FXAA)
* Cascade Shadow Mapping (CSM)
* Skybox Rendering (Cubemap)
* Perspective / Orthographic Camera
* Infinite far plane (Reverse Z-Buffer)
* Frustum Culling (AABB)
* Bounding Volume Hierarchy (BVH)
* Opaque / Translucent / Cutoff Shaders
* Vulkan API 1.3 backend
* Multithreaded Rendering
* Automatic Memory Barriers
* Bindless Descriptors
* Custom Shader Language (GSL)
* Easy To Use Rendering Pipelines
* Linear Resource Pool
* Built-in Thread Pool (Tasks)
* Settings / Logging System
* Scene Serialization / Deserialization
* First Person View (FPV)
* .web / .png / .jpg / .exr / .hdr Support
* glTF Model And Scene Loader
* Config File Reader / Writer
* Resource Pack Reader / Writer
* Equi To Cube Map Converter

### Planned Features

- [ ] Procedural Atmosphere (Sky)
- [ ] Screen Space Reflections (SSR)
- [ ] Motion Blur
- [ ] Forward Rendering
- [ ] Vignette Post-Process
- [ ] Depth Of Field (DOF)
- [ ] Physical Based Camera
- [ ] Screen Space Shadows (SSS)
- [ ] Froxel Culled Lights
- [ ] GPU Occlusion Culling
- [ ] Chromatic Abberation
- [ ] Lens Flare / Glare
- [ ] Color Grading (ACES)
- [ ] Lens Distortion
- [ ] God Rays
- [ ] Volumetric Smoke / Clouds
- [ ] Soft / Point Shadows
- [ ] Translucent Shadow Maps
- [ ] Vulkan Memory Aliasing
- [ ] Particles / Trails
- [ ] 2D Tile Maps (9-sliced)
- [ ] Nvidia DLSS Support
- [ ] Nvidia Reflex Support
- [ ] AMD FSR Support
- [ ] Physics Support (Jolt)
- [ ] Hardware Ray-Tracing
- [ ] Ray-Traced Shadows
- [ ] HDR Monitor Support
- [ ] Virtual Reality Support
- [ ] Consoles Support
- [ ] Mobiles Support

## Supported operating systems

* Windows (10/11)
* Ubuntu (22.04 LTS)
* macOS (Ventura 13)

This list includes only those systems on which functionality testing is conducted.
However, you can also compile it under any other Linux distribution or operating system.

## Supported hardware

### Graphics processing unit (GPU)

* Nvidia [Kepler](https://en.wikipedia.org/wiki/Kepler_(microarchitecture)) microarchitecture and newer
* AMD [GCN](https://en.wikipedia.org/wiki/Graphics_Core_Next) microarchitecture and newer
* Intel [Gen9](https://en.wikichip.org/wiki/intel/microarchitectures/gen9) microarchitecture and newer
* Apple [M1](https://en.wikipedia.org/wiki/Apple_M1) processor and newer (partial support)

### Central processing unit (CPU)

* Intel [Haswell](https://en.wikipedia.org/wiki/Haswell_(microarchitecture)) microarchitecture and newer
* AMD [Excavator](https://en.wikipedia.org/wiki/Excavator_(microarchitecture)) microarchitecture and newer

These requirements are based on the support of the **AVX2** instruction set.
If you want, you can freely disable it and compile for any other architecture, including **ARM**.

## Build requirements

* C++17 compiler
* [Git 2.30+](https://git-scm.com)
* [CMake 3.22+](https://cmake.org)
* [Vulkan SDK 1.3+](https://vulkan.lunarg.com)
* [OpenSSL 3.0+](https://openssl.org)
* [X11](https://www.x.org) (Linux only)

Use building [instructions](BUILDING.md) to install all required tools and libraries.

## Build examples

1. Run ```git clone --progress --recursive https://github.com/cfnptr/garden``` using **Terminal**
2. Open garden directory using **IDE** or use [build.sh](build.sh) script

### CMake options

| Name                     | Description                                      | Default value |
|--------------------------|--------------------------------------------------|---------------|
| GARDEN_BUILD_GSLC        | Build GSL shader compiler                        | `ON`          |
| GARDEN_BUILD_EQUI2CUBE   | Build equirectangular to cubemap converter       | `ON`          |
| GARDEN_BUILD_EXAMPLES    | Build Garden library examples                    | `ON`          |
| GARDEN_RELEASE_EDITOR    | Build Garden editor in the release build         | `OFF`         |
| GARDEN_RELEASE_DEBUGGING | Build Garden debugging code in the release build | `OFF`         |

### CMake variables

| Name                      | Description                               | Default value                      |
|---------------------------|-------------------------------------------|------------------------------------|
| GARDEN_APP_NAME           | Garden application name                   | `Garden App`                       |
| GARDEN_APP_NAME_LOWERCASE | Garden application lowercase name         | `garden-app`                       |
| GARDEN_APP_FILE_NAME      | Garden application file name (executable) | `Garden App`                       |
| GARDEN_APP_DESCRIPTION    | Garden application description            | `Made using Garden engine.`        |
| GARDEN_APP_CREATOR        | Garden application creator (company)      | `Garden`                           |
| GARDEN_APP_COPYRIGHT      | Garden application copyright (license)    | `20XX {name} All rights reserved.` |
| GARDEN_APP_VERSION_MAJOR  | Garden application major version          | `1`                                |
| GARDEN_APP_VERSION_MINOR  | Garden application minor version          | `0`                                |
| GARDEN_APP_VERSION_PATCH  | Garden application patch version          | `0`                                |
| GARDEN_APP_RESOURCES_DIR  | Garden application resources directory    | `garden/resources/default`         |
| GARDEN_APP_CACHES_DIR     | Garden application caches directory       | `garden/.caches`                   |

## Garden Shading Language (GSL)

Documentation with all GLSL changes is located [here](docs/gsl.md).
You can install **Visual Studio Code** [extension](https://marketplace.visualstudio.com/items?itemName=cfnptr.gsl-linter) to highlight GSL code.

## Third-party

* [Conf](https://github.com/cfnptr/conf) (Apache-2.0 license)
* [ECSM](https://github.com/cfnptr/ecsm) (Apache-2.0 license)
* [FastNoise2](https://github.com/Auburn/FastNoise2) (MIT license)
* [FreeType](https://github.com/freetype/freetype) (FreeType license)
* [GLFW](https://github.com/glfw/glfw) (Zlib license)
* [Imath](https://github.com/AcademySoftwareFoundation/Imath) (BSD-3-Clause license)
* [ImGui](https://github.com/ocornut/imgui) (MIT license)
* [JSON](https://github.com/nlohmann/json) (MIT license)
* [Logy](https://github.com/cfnptr/logy) (Apache-2.0 license)
* [Math](https://github.com/cfnptr/math) (Apache-2.0 license)
* [Nets](https://github.com/cfnptr/nets) (Apache-2.0 license)
* [OpenEXR](https://github.com/AcademySoftwareFoundation/openexr) (BSD-3-Clause license)
* [Pack](https://github.com/cfnptr/pack) (Apache-2.0 license)
* [stb](https://github.com/nothings/stb) (MIT license)
* [VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) (MIT license)
* [Voxy](https://github.com/cfnptr/voxy) (Apache-2.0 license)
* [WebP](https://github.com/webmproject/libwebp) (BSD-3-Clause license)
* [xxHash](https://github.com/Cyan4973/xxHash) (BSD-2-Clause license)
* [Vulkan](https://github.com/KhronosGroup) (Apache-2.0 license)