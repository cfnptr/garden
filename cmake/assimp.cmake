# Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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

if (NOT GARDEN_USE_ASSIMP)
	return()
endif()

set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INJECT_DEBUG_POSTFIX OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_VIEW OFF CACHE BOOL "" FORCE)

message(STATUS "Fetching Assimp, please wait...")
FetchContent_Declare(assimp GIT_REPOSITORY https://github.com/assimp/assimp
	GIT_TAG 392a658f9c271be965271f45e7521a1b80ea4392 GIT_SHALLOW TRUE)
set(ASSIMP_VERSION "6.0.5")

FetchContent_MakeAvailable(assimp)
FetchContent_GetProperties(assimp)

list(APPEND GARDEN_INCLUDE_DIRS ${assimp_SOURCE_DIR}/include)
list(APPEND GARDEN_LINK_LIBS assimp)