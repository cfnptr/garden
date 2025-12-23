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

if (NOT GARDEN_USE_STEAMWORKS_SDK)
	set(GARDEN_STEAMWORKS_SDK 0)
	return()
endif()

if(NOT DEFINED GARDEN_STEAMWORKS_SDK_DIR)
	set(GARDEN_STEAMWORKS_SDK_DIR libraries/steamworks-sdk)
endif()

if(NOT EXISTS ${PROJECT_SOURCE_DIR}/${GARDEN_STEAMWORKS_SDK_DIR})
	message(WARNING "Steamworks SDK directory does not exist! Please download SDK.")
	set(GARDEN_STEAMWORKS_SDK 0)
	return()
endif()

if (NOT DEFINED GARDEN_STEAMWORKS_APP_ID)
	message(FATAL_ERROR "Not defined Valve Steam application ID!")
endif()

message(STATUS "Included Valve Steamworks SDK.")

set(STEAMWORKS_SDK_INCLUDE_DIR ${GARDEN_STEAMWORKS_SDK_DIR}/public)
set(STEAMWORKS_SDK_BIN_DIR ${GARDEN_STEAMWORKS_SDK_DIR}/redistributable_bin)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
	set(STEAMWORKS_SDK_LIBS_DIR ${STEAMWORKS_SDK_BIN_DIR}/win64)
	list(APPEND GARDEN_LINK_LIBS ${STEAMWORKS_SDK_LIBS_DIR}/steam_api64.lib)
	set(STEAMWORKS_SDK_LIB ${STEAMWORKS_SDK_LIBS_DIR}/steam_api64.dll)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
	set(STEAMWORKS_SDK_LIBS_DIR ${STEAMWORKS_SDK_BIN_DIR}/osx)
	set(STEAMWORKS_SDK_LIB ${STEAMWORKS_SDK_LIBS_DIR}/libsteam_api.dylib)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
	set(STEAMWORKS_SDK_LIBS_DIR ${STEAMWORKS_SDK_BIN_DIR}/linux64)
	set(STEAMWORKS_SDK_LIB ${STEAMWORKS_SDK_LIBS_DIR}/libsteam_api.so)
endif()

list(APPEND GARDEN_INCLUDE_DIRS ${STEAMWORKS_SDK_INCLUDE_DIR})
list(APPEND GARDEN_LINK_LIBS ${PROJECT_SOURCE_DIR}/${STEAMWORKS_SDK_LIB})
configure_file(${STEAMWORKS_SDK_LIB} ${CMAKE_BINARY_DIR} COPYONLY)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	file(WRITE ${CMAKE_BINARY_DIR}/steam_appid.txt ${GARDEN_STEAMWORKS_APP_ID})
endif()

set(GARDEN_STEAMWORKS_SDK 1)