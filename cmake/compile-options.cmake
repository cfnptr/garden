#---------------------------------------------------------------------------------------------------
# Voxfield - An open source voxel based multiplayer sandbox game.
# Copyright (C) 2022-2023  Nikita Fediuchin
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#---------------------------------------------------------------------------------------------------

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Based on Steam survey instruction set support.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
	add_compile_options(/MP /nologo /arch:AVX2)
	if(CMAKE_BUILD_TYPE STREQUAL "Release")
		add_compile_options(/GL /LTCG)
	endif()
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")
	add_compile_options(-march=haswell)
	add_compile_options(-Wno-nullability-completeness -Wno-unused-function)
	if(CMAKE_BUILD_TYPE STREQUAL "Release")
		add_compile_options(-flto -fno-fat-lto-objects)
	endif()
endif()