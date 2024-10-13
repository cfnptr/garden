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

# Based on Steam survey instruction set support.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
	if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
		set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
	endif()

	add_compile_options(/MP /nologo /utf-8 /arch:AVX2)
	if(CMAKE_BUILD_TYPE STREQUAL "Release")
		add_compile_options(/GL)
	endif()
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")
	add_compile_options(-march=haswell)
	add_compile_options(-Wno-nullability-completeness -Wno-unused-function)
	if(CMAKE_BUILD_TYPE STREQUAL "Release")
		add_compile_options(-flto)
	endif()
	if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
		add_compile_options(-fno-fat-lto-objects)
	endif()
endif()