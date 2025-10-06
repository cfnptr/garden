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

if (NOT DEFINED GARDEN_NVIDIA_DLSS_PROJECT_ID AND NOT DEFINED GARDEN_NVIDIA_DLSS_APP_ID)
	message(FATAL_ERROR "Not defined Nvidia DLSS project or application ID!")
	# Note that DLSS project ID should be a GUID-like string!
endif()

message(STATUS "Fetching Nvidia DLSS SDK, please wait...")
FetchContent_Declare(nvidia-dlss GIT_REPOSITORY https://github.com/NVIDIA/DLSS
	GIT_TAG a8ed84e2d1cc1efa7bef63fb9394be6cb06c0d74)
set(NVIDIA_DLSS_VERSION "310.3.0")

FetchContent_MakeAvailable(nvidia-dlss)
FetchContent_GetProperties(nvidia-dlss)

set(NVIDIA_DLSS_INCLUDE_DIR ${nvidia-dlss_SOURCE_DIR}/include)
set(NVIDIA_DLSS_BIN_DIR ${nvidia-dlss_SOURCE_DIR}/lib)

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")
	if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		set(NVIDIA_DLSS_LIBS_DIR ${NVIDIA_DLSS_BIN_DIR}/Linux_x86_64)
		set(NVIDIA_DLSS_SDK_LIB ${NVIDIA_DLSS_LIBS_DIR}/libnvsdk_ngx.a)
	elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
		set(NVIDIA_DLSS_LIBS_DIR ${NVIDIA_DLSS_BIN_DIR}/Windows_x86_64)
		set(NVIDIA_DLSS_SDK_LIBS_DIR ${NVIDIA_DLSS_LIBS_DIR}/khr/x64)
		# TODO: use non khr path for DirectX, if support will be needed.
		if(CMAKE_BUILD_TYPE STREQUAL "Debug")
			set(NVIDIA_DLSS_SDK_LIB ${NVIDIA_DLSS_SDK_LIBS_DIR}/nvsdk_ngx_khr_s_dbg.lib)
		else()
			set(NVIDIA_DLSS_SDK_LIB ${NVIDIA_DLSS_SDK_LIBS_DIR}/nvsdk_ngx_khr_s.lib)
		endif()
	else()
		message(FATAL_ERROR "Nvidia DLSS is not supported on target OS!")
	endif()
else()
	message(FATAL_ERROR "Nvidia DLSS is not supported on target architecture!")
endif()

if(NVIDIA_DLSS_DEBUG_LIB)
	set(NVIDIA_DLSS_LIB_DIR ${NVIDIA_DLSS_LIBS_DIR}/dev)
else()
	set(NVIDIA_DLSS_LIB_DIR ${NVIDIA_DLSS_LIBS_DIR}/rel)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	set(NVIDIA_DLSS_LIB ${NVIDIA_DLSS_LIB_DIR}/libnvidia-ngx-dlss.so.${NVIDIA_DLSS_VERSION})
else()
	set(NVIDIA_DLSS_LIB ${NVIDIA_DLSS_LIB_DIR}/nvngx_dlss.dll)
endif()

list(APPEND GARDEN_INCLUDE_DIRS ${NVIDIA_DLSS_INCLUDE_DIR})
list(APPEND GARDEN_LINK_LIBS ${NVIDIA_DLSS_SDK_LIB})
configure_file(${NVIDIA_DLSS_LIB} ${CMAKE_BINARY_DIR} COPYONLY)

if (DEFINED GARDEN_NVIDIA_DLSS_PROJECT_ID)
	set(GARDEN_NVIDIA_DLSS_USE_PROJECT_ID 1)
else()
	set(GARDEN_NVIDIA_DLSS_USE_PROJECT_ID 0)
endif()