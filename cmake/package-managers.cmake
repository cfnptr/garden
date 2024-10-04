# Copyright 2022-2024 Nikita Fediuchin. All rights reserved.
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

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL TRUE CACHE BOOL "" FORCE)
set(ZLIB_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
	if(NOT DEFINED VCPKG_ROOT)
		cmake_path(SET VCPKG_ROOT $ENV{VCPKG_ROOT})
		message(STATUS "VCPKG_ROOT: " ${VCPKG_ROOT})
	endif()
	if (NOT DEFINED CMAKE_TOOLCHAIN_FILE)
		set(CMAKE_TOOLCHAIN_FILE ${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)
		if (NOT EXISTS ${CMAKE_TOOLCHAIN_FILE})
			message(FATAL_ERROR "vcpkg is not installed or added to the System Environment Variables.")
		endif()
	endif()

	if(NOT DEFINED VCPKG_ARCH)
		if(CMAKE_CXX_COMPILER MATCHES "x64/cl.exe")
			set(VCPKG_ARCH "x64")
		elseif(CMAKE_CXX_COMPILER MATCHES "x86/cl.exe")
			set(VCPKG_ARCH "x86")
		else()
			message(FATAL_ERROR "Failed to determine target architecture")
		endif()
	endif()

	if(NOT DEFINED VCPKG_TARGET_TRIPLET)
		set(VCPKG_TARGET_TRIPLET "${VCPKG_ARCH}-windows-static")
		message(STATUS "VCPKG_TARGET_TRIPLET: " ${VCPKG_TARGET_TRIPLET})
	endif()

	if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
		set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
	endif()
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
	if(NOT DEFINED HOMEBREW_PREFIX)
		set(HOMEBREW_PREFIX $ENV{HOMEBREW_PREFIX})
		if (NOT EXISTS ${HOMEBREW_PREFIX})
			message(FATAL_ERROR "Homebrew is not installed or added to the System Environment Variables.")
		endif()
	endif()
	if(NOT DEFINED CMAKE_PREFIX_PATH)
		set(CMAKE_PREFIX_PATH ${HOMEBREW_PREFIX} ${HOMEBREW_PREFIX}/opt/zlib)
	endif()
endif()