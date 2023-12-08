#---------------------------------------------------------------------------------------------------
# Copyright 2022-2023 Nikita Fediuchin. All rights reserved.
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
#---------------------------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
	message(FATAL_ERROR "Prevented in-tree build")
endif()

if(TARGET garden)
	return()
endif()

project(Garden VERSION 0.1.0 LANGUAGES CXX
	DESCRIPTION "An open source cross platform game engine \
		written in C++ with Vulkan API rendering backend"
	HOMEPAGE_URL "https://github.com/cfnptr/garden")

option(GARDEN_RELEASE_EDITOR "Build Garden editor in the release build." OFF)

if(NOT DEFINED GARDEN_APP_NAME)
	set(GARDEN_APP_NAME "GardenApp")
endif()
if(NOT DEFINED GARDEN_APP_NAME_LOWERCASE)
	string(TOLOWER ${GARDEN_APP_NAME} GARDEN_APP_NAME_LOWERCASE)
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
	set(GARDEN_APP_RESOURCES_DIR ${PROJECT_SOURCE_DIR}/resources)
endif()
if(NOT DEFINED GARDEN_APP_CACHES_DIR)
	set(GARDEN_APP_RESOURCES_DIR ${PROJECT_SOURCE_DIR}/.caches)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(BUILD_SHARED_LIBS OFF)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
	set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	enable_language(OBJC)
	enable_language(OBJCXX)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(CMAKE_BUILD_TYPE STREQUAL Debug)
	set(GARDEN_DEBUG 1)
	set(GARDEN_EDITOR 1)
else()
	set(GARDEN_DEBUG 0)
	if (GARDEN_RELEASE_EDITOR)
		set(GARDEN_EDITOR 1)
	else()
		set(GARDEN_EDITOR 0)
	endif()
endif()

include(TestBigEndian)
TEST_BIG_ENDIAN(IS_BIG_ENDIAN)

if(IS_BIG_ENDIAN)
	set(GARDEN_LITTLE_ENDIAN 0)
else()
	set(GARDEN_LITTLE_ENDIAN 1)
endif()

set(GARDEN_APP_VERSION "${GARDEN_APP_VERSION_MAJOR}.${GARDEN_APP_VERSION_MINOR}.${GARDEN_APP_VERSION_PATCH}")
configure_file(cmake/defines.hpp.in include/garden/defines.hpp)

#---------------------------------------------------------------------------------------------------
set(GARDEN_INCLUDE_DIRS ${PROJECT_BINARY_DIR}/include ${PROJECT_SOURCE_DIR}/include
	libraries/ecsm/include libraries/vma/include libraries/stb)
set(GARDEN_LINK_LIBS Vulkan::Vulkan xxHash::xxhash ecsm math-static
	conf-static pack-static logy-static voxy glfw webp FastNoise OpenEXR)
set(GARDEN_LINK_DIRS)

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	list(APPEND GARDEN_LINK_LIBS "-framework CoreServices")
endif()

find_package(Vulkan REQUIRED)

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

set(VOXY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(libraries/voxy)

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

set(FASTNOISE2_NOISETOOL OFF CACHE BOOL "" FORCE)
add_subdirectory(libraries/fastnoise2)

set(OPENEXR_INSTALL OFF CACHE BOOL "" FORCE)
set(OPENEXR_INSTALL_TOOLS OFF CACHE BOOL "" FORCE)
set(OPENEXR_INSTALL_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(libraries/imath)
add_subdirectory(libraries/openexr)

#---------------------------------------------------------------------------------------------------
file(GLOB_RECURSE GARDEN_SOURCES source/*.cpp)

if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR GARDEN_RELEASE_EDITOR)
	file(GLOB IMGUI_SOURCES libraries/imgui/*.cpp)
	file(GLOB IMGUI_MISC_SOURCES libraries/imgui/misc/cpp/*.cpp)
	list(APPEND GARDEN_INCLUDE_DIRS
		libraries/imgui libraries/imgui/backends)
	list(APPEND GARDEN_SOURCES ${IMGUI_SOURCES} ${IMGUI_MISC_SOURCES}
		libraries/imgui/backends/imgui_impl_glfw.cpp
		libraries/imgui/backends/imgui_impl_vulkan.cpp)
	
	# TODO: use my own mesh format on release. (look at MeshOptimizer)
	list(APPEND GARDEN_INCLUDE_DIRS libraries/cgltf)
endif()

#---------------------------------------------------------------------------------------------------
add_library(garden STATIC ${GARDEN_SOURCES})
target_include_directories(garden PUBLIC ${GARDEN_INCLUDE_DIRS})
target_link_libraries(garden PUBLIC ${GARDEN_LINK_LIBS})
target_link_directories(garden PUBLIC ${GARDEN_LINK_DIRS})

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang") # TODO: Fix VMA warnings
	target_compile_options(garden PRIVATE -Wno-nullability-completeness)
endif()

add_executable(gslc source/graphics/compiler.cpp)
target_compile_definitions(gslc PUBLIC GSL_COMPILER)
target_link_libraries(gslc PUBLIC garden)

add_executable(equi2cube source/graphics/equi2cube.cpp)
target_compile_definitions(equi2cube PUBLIC EQUI2CUBE)
target_link_libraries(equi2cube PUBLIC garden)