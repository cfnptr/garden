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

# Based on Steam survey CPU instruction set support.
# https://store.steampowered.com/hwsurvey

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
	add_compile_options(/MP /nologo /utf-8)
	if(NOT GARDEN_DO_NOT_USE_AVX2)
		add_compile_options(/arch:AVX2)
	endif()
	if(CMAKE_BUILD_TYPE STREQUAL "Release")
		add_compile_options(/GL)
	endif()
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")
	if(NOT GARDEN_DO_NOT_USE_AVX2)
		add_compile_options(-march=haswell)
	endif()
	if(CMAKE_BUILD_TYPE STREQUAL "Release")
		add_compile_options(-flto)
	endif()

	add_compile_options(-Wno-unused-function -Wno-unused-private-field 
		-Wno-reorder-ctor -Wno-switch-default -Wno-nan-infinity-disabled 
		-Wno-misleading-indentation -Wno-unused-command-line-argument)

	if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
		if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
			add_link_options(-fuse-ld=lld)
		else()
			add_compile_options(-fno-fat-lto-objects)
		endif()
	endif()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang") # Note: Do not remove MATCHES!
	add_compile_options(-ffp-model=precise) # Disabling fast-math, it breaks math.
	if(CMAKE_BUILD_TYPE STREQUAL "Debug")
		add_compile_options(-fstandalone-debug)
	endif()
	if(GARDEN_USE_ASAN)
		add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
		add_link_options(-fsanitize=address)
	endif()
	if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
		# This warning is emmited due to Brew iclude directories.
		add_compile_options(-Wno-poison-system-directories)
		# Strip symbols for a release build, it speed ups linking.
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -s")
	endif()
endif()