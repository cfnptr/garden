# Copyright 2022-2025 Nikita Fediuchin. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#***********************************************************************************************************************
cmake_minimum_required(VERSION 3.24)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
	message(FATAL_ERROR "Prevented in-tree build")
endif()

if(NOT CMAKE_BUILD_TYPE MATCHES "Debug|Release|RelWithDebInfo|MinSizeRel")
	# Check if VS Code option "Set Build Type On Multi Config" is enabled.
	message(FATAL_ERROR "CMAKE_BUILD_TYPE is not declared!")
else()
	message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
endif()

if(TARGET garden)
	return()
endif()

include(cmake/package-managers.cmake)

project(Garden VERSION 0.0.1 LANGUAGES CXX
	DESCRIPTION "An open source cross platform game engine \
		written in C++ with Vulkan API rendering backend."
	HOMEPAGE_URL "https://github.com/cfnptr/garden")

option(GARDEN_BUILD_GSLC "Build GSL shader compiler." ON)
option(GARDEN_BUILD_MODELC "Build Garden model converter." ON)
option(GARDEN_BUILD_JSON2BSON "Build JSON to binary JSON converter." ON)
option(GARDEN_BUILD_EQUI2CUBE "Build equirectangular to cubemap converter." ON)
option(GARDEN_RELEASE_EDITOR "Build Garden editor in the release build." OFF)
option(GARDEN_RELEASE_DEBUGGING "Build Garden debugging code in the release build." OFF)
option(GARDEN_DEBUG_PACK_RESOURCES "Pack and load resources in the debug build." OFF)
option(GARDEN_USE_GAPI_VALIDATIONS "Use graphics API validation layers." ON)
option(GARDEN_USE_TRACY_PROFILER "Use Tracy frame profiler." OFF)
option(GARDEN_USE_ASAN "Use Clang address sanitizer." OFF)

# By enabling this option you agree to the NVIDIA RTX SDKs license.
option(GARDEN_USE_NVIDIA_DLSS "Use Nvidia DLSS SDK." OFF)
# By enabling this option you agree to the Valve STEAMWORKS SDKs license.
option(GARDEN_USE_STEAMWORKS_SDK "Use Steamworks SDK." OFF)

string(TIMESTAMP GARDEN_CURRENT_YEAR "%Y")
if(NOT DEFINED GARDEN_APP_NAME)
	set(GARDEN_APP_NAME "Garden App")
endif()
if(NOT DEFINED GARDEN_APP_NAME_LOWERCASE)
	if(NOT DEFINED GARDEN_APP_NAME)
		set(GARDEN_APP_NAME_LOWERCASE "garden-app")
	else()
		string(TOLOWER ${GARDEN_APP_NAME} GARDEN_APP_NAME_LOWERCASE)
	endif()
endif()
if(NOT DEFINED GARDEN_APP_NAMESPACE)
	set(GARDEN_APP_NAMESPACE "garden::app")
endif()
if(NOT DEFINED GARDEN_APP_FILE_NAME)
	set(GARDEN_APP_FILE_NAME ${GARDEN_APP_NAME})
endif()
if(NOT DEFINED GARDEN_APP_DESCRIPTION)
	set(GARDEN_APP_DESCRIPTION "Made using Garden engine.")
endif()
if(NOT DEFINED GARDEN_APP_CREATOR)
	set(GARDEN_APP_CREATOR "Anonymous")
endif()
if(NOT DEFINED GARDEN_APP_COPYRIGHT)
	set(GARDEN_APP_COPYRIGHT "${GARDEN_CURRENT_YEAR} ${GARDEN_APP_CREATOR}. All rights reserved.")
endif()
if(NOT DEFINED GARDEN_APP_VERSION_MAJOR)
	set(GARDEN_APP_VERSION_MAJOR 1)
endif()
if(NOT DEFINED GARDEN_APP_VERSION_MINOR)
	set(GARDEN_APP_VERSION_MINOR 0)
endif()
if(NOT DEFINED GARDEN_APP_VERSION_PATCH)
	set(GARDEN_APP_VERSION_PATCH 0)
endif()
if(NOT DEFINED GARDEN_APP_RESOURCES_DIR)
	set(GARDEN_APP_RESOURCES_DIR ${PROJECT_SOURCE_DIR}/resources/default)
endif()
if(NOT DEFINED GARDEN_APP_CACHE_DIR)
	set(GARDEN_APP_CACHE_DIR ${PROJECT_SOURCE_DIR}/.cache)
endif()

if(NOT DEFINED GARDEN_DS_POOL_COMBINED_SAMPLER_COUNT)
	set(GARDEN_DS_POOL_COMBINED_SAMPLER_COUNT 1024)
endif()
if(NOT DEFINED GARDEN_DS_POOL_UNIFORM_BUFFER_COUNT)
	set(GARDEN_DS_POOL_UNIFORM_BUFFER_COUNT 512)
endif()
if(NOT DEFINED GARDEN_DS_POOL_STORAGE_IMAGE_COUNT)
	set(GARDEN_DS_POOL_STORAGE_IMAGE_COUNT 256)
endif()
if(NOT DEFINED GARDEN_DS_POOL_STORAGE_BUFFER_COUNT)
	set(GARDEN_DS_POOL_STORAGE_BUFFER_COUNT 512)
endif()
if(NOT DEFINED GARDEN_DS_POOL_INPUT_ATTACHMENT_COUNT)
	set(GARDEN_DS_POOL_INPUT_ATTACHMENT_COUNT 128)
endif()
if(NOT DEFINED GARDEN_DS_POOL_ACCEL_STRUCTURE_COUNT)
	set(GARDEN_DS_POOL_ACCEL_STRUCTURE_COUNT 64)
endif()

#***********************************************************************************************************************
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	enable_language(OBJC)
	enable_language(OBJCXX)
endif()

if(GARDEN_USE_CLANG)
	message(STATUS "Using Clang compiler.")
endif()

include(TestBigEndian)
TEST_BIG_ENDIAN(IS_BIG_ENDIAN)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(GARDEN_DEBUG 1)
	set(GARDEN_EDITOR 1)
	if(GARDEN_DEBUG_PACK_RESOURCES)
		set(GARDEN_PACK_RESOURCES 1)
	else()
		set(GARDEN_PACK_RESOURCES 0)
	endif()
else()
	if(GARDEN_RELEASE_DEBUGGING)
		set(GARDEN_DEBUG 1)
		set(GARDEN_PACK_RESOURCES 0)
	else()
		set(GARDEN_DEBUG 0)
		set(GARDEN_PACK_RESOURCES 1)
	endif()
	if(GARDEN_RELEASE_EDITOR)
		set(GARDEN_EDITOR 1)
	else()
		set(GARDEN_EDITOR 0)
	endif()
endif()

if(CMAKE_BUILD_TYPE MATCHES "Release|RelWithDebInfo" AND NOT GARDEN_RELEASE_DEBUGGING)
	if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		set(GARDEN_RELEASE_LINUX 1)
	else()
		set(GARDEN_RELEASE_LINUX 0)
	endif()
	if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
		set(GARDEN_RELEASE_MACOS 1)
	else()
		set(GARDEN_RELEASE_MACOS 0)
	endif()
	if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
		set(GARDEN_RELEASE_WINDOWS 1)
	else()
		set(GARDEN_RELEASE_WINDOWS 0)
	endif()
else()
	set(GARDEN_RELEASE_LINUX 0)
	set(GARDEN_RELEASE_MACOS 0)
	set(GARDEN_RELEASE_WINDOWS 0)
endif()

if(IS_BIG_ENDIAN)
	set(GARDEN_LITTLE_ENDIAN 0)
else()
	set(GARDEN_LITTLE_ENDIAN 1)
endif()
if(GARDEN_USE_GAPI_VALIDATIONS)
	set(GARDEN_GAPI_VALIDATIONS 1)
else()
	set(GARDEN_GAPI_VALIDATIONS 0)
endif()
if(GARDEN_USE_TRACY_PROFILER)
	set(GARDEN_TRACY_PROFILER 1)
else()
	set(GARDEN_TRACY_PROFILER 0)
endif()

math(EXPR GARDEN_APP_VERSION "((${GARDEN_APP_VERSION_MAJOR} << 24) | (${GARDEN_APP_VERSION_MINOR} << 16) | (${GARDEN_APP_VERSION_PATCH} << 8))")
set(GARDEN_APP_VERSION_STRING "${GARDEN_APP_VERSION_MAJOR}.${GARDEN_APP_VERSION_MINOR}.${GARDEN_APP_VERSION_PATCH}")

if(DEFINED GARDEN_APP_ICON_PATH AND CMAKE_SYSTEM_NAME STREQUAL "Windows")
	set(GARDEN_APP_WINDOWS_ICON "IDI_ICON1 ICON \"${GARDEN_APP_ICON_PATH}\"")
endif()

if(NOT PROJECT_IS_TOP_LEVEL)
	set(GARDEN_DEBUG ${GARDEN_DEBUG} PARENT_SCOPE)
	set(GARDEN_EDITOR ${GARDEN_EDITOR} PARENT_SCOPE)
	set(GARDEN_PACK_RESOURCES ${GARDEN_PACK_RESOURCES} PARENT_SCOPE)
	set(GARDEN_LITTLE_ENDIAN ${GARDEN_LITTLE_ENDIAN} PARENT_SCOPE)
	set(GARDEN_APP_VERSION ${GARDEN_APP_VERSION} PARENT_SCOPE)
	set(GARDEN_APP_VERSION_STRING ${GARDEN_APP_VERSION_STRING} PARENT_SCOPE)
	if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
		set(GARDEN_APP_WINDOWS_ICON ${GARDEN_APP_WINDOWS_ICON} PARENT_SCOPE)
	endif()
endif()

include(cmake/publish-functions.cmake)
include(cmake/compile-options.cmake)
include(cmake/gather-git-info.cmake)

#***********************************************************************************************************************
set(GARDEN_INCLUDE_DIRS ${PROJECT_BINARY_DIR}/include ${PROJECT_SOURCE_DIR}/include resources/shaders 
	libraries/ecsm/include libraries/voxy/include libraries/vma/include libraries/stb 
	libraries/tinyexr libraries/tinygltf libraries/png ${PROJECT_BINARY_DIR}/libraries/png)
set(GARDEN_LINK_LIBS Vulkan::Vulkan volk nlohmann_json::nlohmann_json xxHash::xxhash 
	assimp::assimp ecsm-static math-static conf-static pack-static logy-static nets-static 
	voxy glfw webp png_static tinyexr tinygltf freetype FastNoise Jolt)
set(GARDEN_LINK_DIRS)

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	list(APPEND GARDEN_LINK_LIBS "-framework CoreServices")
endif()
if(NOT GARDEN_PACK_RESOURCES)
	list(APPEND GARDEN_LINK_LIBS assimp::assimp)
endif()

include(FetchContent)
find_package(Vulkan REQUIRED)
find_package(assimp REQUIRED)

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_MACOS_MVK)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_XLIB_KHR)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
	set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_WIN32_KHR)
endif()
add_subdirectory(libraries/volk)

set(ECSM_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ECSM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(libraries/ecsm)

set(MATH_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MATH_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(libraries/math)

set(PACK_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(PACK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(libraries/pack)

set(CONF_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(CONF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(libraries/conf)

set(LOGY_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(LOGY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(libraries/logy)

set(NETS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(libraries/nets)

set(VOXY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(libraries/voxy)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
add_subdirectory(libraries/json)

set(XXHASH_BUILD_XXHSUM OFF CACHE BOOL "" FORCE)
add_subdirectory(libraries/xxhash/cmake_unofficial)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(libraries/glfw)

set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_LIBWEBPMUX OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(WEBP_USE_THREAD OFF CACHE BOOL "" FORCE)
add_subdirectory(libraries/webp)

set(PNG_SHARED OFF CACHE BOOL "" FORCE)
set(PNG_FRAMEWORK OFF CACHE BOOL "" FORCE)
set(PNG_TESTS OFF CACHE BOOL "" FORCE)
set(PNG_TOOLS OFF CACHE BOOL "" FORCE)
add_subdirectory(libraries/png)

set(TINYEXR_BUILD_SAMPLE OFF CACHE BOOL "" FORCE)
add_subdirectory(libraries/tinyexr)

set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE BOOL "" FORCE)
set(TINYGLTF_INSTALL_VENDOR OFF CACHE BOOL "" FORCE)
add_subdirectory(libraries/tinygltf)

set(FASTNOISE2_NOISETOOL OFF CACHE BOOL "" FORCE)
add_subdirectory(libraries/fastnoise2)

set(FT_ENABLE_ERROR_STRINGS ON CACHE BOOL "" FORCE)
add_subdirectory(libraries/freetype)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(TRACK_BROADPHASE_STATS ON CACHE BOOL "" FORCE)
	set(TRACK_NARROWPHASE_STATS ON CACHE BOOL "" FORCE)
endif()
if(CMAKE_BUILD_TYPE MATCHES "Release|RelWithDebInfo" AND GARDEN_RELEASE_EDITOR)
	set(DEBUG_RENDERER_IN_DISTRIBUTION ON CACHE BOOL "" FORCE)
endif()
set(CPP_EXCEPTIONS_ENABLED ON CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
add_subdirectory(libraries/jolt/Build)

if(GARDEN_USE_TRACY_PROFILER)
	set(TRACY_STATIC ON CACHE BOOL "" FORCE)
	set(TRACY_FIBERS ON CACHE BOOL "" FORCE)
	set(TRACY_NO_CALLSTACK ON CACHE BOOL "" FORCE)
	set(TRACY_NO_SAMPLING ON CACHE BOOL "" FORCE)
	set(TRACY_NO_CODE_TRANSFER ON CACHE BOOL "" FORCE)
	set(TRACY_NO_CONTEXT_SWITCH ON CACHE BOOL "" FORCE)
	set(TRACY_NO_FRAME_IMAGE ON CACHE BOOL "" FORCE)
	add_subdirectory(libraries/tracy)
	list(APPEND GARDEN_LINK_LIBS Tracy::TracyClient)
endif()

include(cmake/nvidia-dlss.cmake)
include(cmake/steamworks-sdk.cmake)

configure_file(cmake/defines.hpp.in include/garden/defines.hpp)

#***********************************************************************************************************************
file(GLOB_RECURSE GARDEN_SOURCES source/*.cpp)

file(GLOB IMGUI_SOURCES libraries/imgui/*.cpp)
file(GLOB IMGUI_MISC_SOURCES libraries/imgui/misc/cpp/*.cpp)
list(APPEND GARDEN_INCLUDE_DIRS libraries/imgui)
list(APPEND GARDEN_SOURCES ${IMGUI_SOURCES} ${IMGUI_MISC_SOURCES})

add_library(garden STATIC ${GARDEN_SOURCES})
target_include_directories(garden PUBLIC ${GARDEN_INCLUDE_DIRS})
target_link_libraries(garden PUBLIC ${GARDEN_LINK_LIBS})
target_link_directories(garden PUBLIC ${GARDEN_LINK_DIRS})

if(GARDEN_BUILD_GSLC)
	add_executable(gslc source/graphics/gslc.cpp)
	target_compile_definitions(gslc PUBLIC GSL_COMPILER)
	target_link_libraries(gslc PUBLIC garden)

	if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT GARDEN_RELEASE_DEBUGGING)
		stripExecutable(gslc)
	endif()
endif()
if(GARDEN_BUILD_MODELC)
	add_executable(modelc source/graphics/modelc.cpp)
	target_compile_definitions(modelc PUBLIC GARDEN_MODEL_CONVERTER)
	target_link_libraries(modelc PUBLIC garden)

	if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT GARDEN_RELEASE_DEBUGGING)
		stripExecutable(modelc)
	endif()
endif()
if(GARDEN_BUILD_JSON2BSON)
	add_executable(json2bson source/json2bson.cpp)
	target_compile_definitions(json2bson PUBLIC JSON2BSON)
	target_link_libraries(json2bson PUBLIC garden)

	if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT GARDEN_RELEASE_DEBUGGING)
		stripExecutable(json2bson)
	endif()
endif()
if(GARDEN_BUILD_EQUI2CUBE)
	add_executable(equi2cube source/graphics/equi2cube.cpp)
	target_compile_definitions(equi2cube PUBLIC EQUI2CUBE)
	target_link_libraries(equi2cube PUBLIC garden)

	if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT GARDEN_RELEASE_DEBUGGING)
		stripExecutable(equi2cube)
	endif()
endif()