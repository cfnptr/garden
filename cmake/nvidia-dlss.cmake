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

if (NOT DEFINED GARDEN_NVIDIA_DLSS_PROJECT_ID AND NOT DEFINED GARDEN_NVIDIA_DLSS_APPLICATION_ID)
	message(FATAL_ERROR "Not defined Nvidia DLSS project or application ID!")
	# Note that DLSS project ID should be a GUID-like string!
endif()

message(STATUS "Fetching Nvidia DLSS SDK, please wait...")
FetchContent_Declare(nvidia-dlss GIT_REPOSITORY https://github.com/NVIDIA/DLSS
	GIT_TAG a8ed84e2d1cc1efa7bef63fb9394be6cb06c0d74)
set(GARDEN_NVIDIA_DLSS_VERSION "310.3.0")

FetchContent_MakeAvailable(nvidia-dlss)
FetchContent_GetProperties(nvidia-dlss)

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")
	if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		set(GARDEN_NVIDIA_DLSS_DIR ${nvidia-dlss_SOURCE_DIR}/lib/Linux_x86_64)
		set(GARDEN_NVIDIA_DLSS_SDK_LIB ${GARDEN_NVIDIA_DLSS_DIR}/libnvsdk_ngx.a)
	elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
		set(GARDEN_NVIDIA_DLSS_DIR ${nvidia-dlss_SOURCE_DIR}/lib/Windows_x86_64)
		if(CMAKE_BUILD_TYPE STREQUAL "Debug")
			set(GARDEN_NVIDIA_DLSS_SDK_LIB ${GARDEN_NVIDIA_DLSS_DIR}/khr/x64/nvsdk_ngx_khr_s_dbg.lib)
		else()
			set(GARDEN_NVIDIA_DLSS_SDK_LIB ${GARDEN_NVIDIA_DLSS_DIR}/khr/x64/nvsdk_ngx_khr_s.lib)
		endif()
		# TODO: use non khr path for DirectX, if support will be needed.
	else()
		message(FATAL_ERROR "Nvidia DLSS is not supported on target OS!")
	endif()
else()
	message(FATAL_ERROR "Nvidia DLSS is not supported on target architecture!")
endif()

if(GARDEN_NVIDIA_DLSS_DEBUG_LIB)
	set(GARDEN_NVIDIA_DLSS_LIB ${GARDEN_NVIDIA_DLSS_DIR}/dev)
else()
	set(GARDEN_NVIDIA_DLSS_LIB ${GARDEN_NVIDIA_DLSS_DIR}/rel)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	set(GARDEN_NVIDIA_DLSS_LIB ${GARDEN_NVIDIA_DLSS_LIB}/libnvidia-ngx-dlss.so.${GARDEN_NVIDIA_DLSS_VERSION})
else()
	set(GARDEN_NVIDIA_DLSS_LIB ${GARDEN_NVIDIA_DLSS_LIB}/nvngx_dlss.dll)
endif()

list(APPEND GARDEN_INCLUDE_DIRS ${nvidia-dlss_SOURCE_DIR}/include)
list(APPEND GARDEN_LINK_LIBS ${GARDEN_NVIDIA_DLSS_SDK_LIB})
configure_file(${GARDEN_NVIDIA_DLSS_LIB} ${CMAKE_BINARY_DIR} COPYONLY)

if (DEFINED GARDEN_NVIDIA_DLSS_PROJECT_ID)
	set(GARDEN_NVIDIA_DLSS_USE_PROJECT_ID 1)
else()
	set(GARDEN_NVIDIA_DLSS_USE_PROJECT_ID 0)
endif()