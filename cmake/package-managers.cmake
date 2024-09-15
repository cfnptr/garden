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

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
	cmake_path(SET VCPKG_ROOT $ENV{VCPKG_ROOT})
	set(CMAKE_TOOLCHAIN_FILE ${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)
	if (NOT EXISTS ${CMAKE_TOOLCHAIN_FILE})
		message(FATAL_ERROR "vcpkg is not installed or added to the System Environment Variables.")
	endif()
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
	set(HOMEBREW_PREFIX $ENV{HOMEBREW_PREFIX})
	if (NOT EXISTS ${HOMEBREW_PREFIX})
		message(FATAL_ERROR "Homebrew is not installed or added to the System Environment Variables.")
	endif()
	set(CMAKE_PREFIX_PATH ${HOMEBREW_PREFIX} ${HOMEBREW_PREFIX}/opt/zlib)
endif()