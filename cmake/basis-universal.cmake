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

if (NOT GARDEN_USE_BASIS_UNIVERSAL)
	return()
endif()

set(BASISU_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BASISU_SSE ON CACHE BOOL "" FORCE)

if(GARDEN_USE_OPENCL AND GARDEN_EDITOR)
	set(BASISU_OPENCL ON CACHE BOOL "" FORCE)
endif()

message(STATUS "Fetching Binomial Basis Universal, please wait...")
FetchContent_Declare(basis-universal GIT_REPOSITORY https://github.com/binomialLLC/basis_universal 
	GIT_TAG 9bebe16726b3a61c8c213eeee3b7cffb462ef34e GIT_SHALLOW TRUE)
set(BASIS_UNIVERSAL_VERSION "2.5")

FetchContent_MakeAvailable(basis-universal)
FetchContent_GetProperties(basis-universal)

list(APPEND GARDEN_INCLUDE_DIRS ${basis-universal_SOURCE_DIR}/encoder 
	${basis-universal_SOURCE_DIR}/transcoder)
list(APPEND GARDEN_LINK_LIBS basisu_encoder)

if(GARDEN_DEBUG)
	add_compile_definitions(BASISU_FORCE_DEVEL_MESSAGES=1)
endif()
