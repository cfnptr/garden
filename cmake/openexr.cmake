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

if (NOT GARDEN_USE_OPENEXR)
	return()
endif()

set(OPENEXR_FORCE_INTERNAL_IMATH ON CACHE BOOL "" FORCE)
set(OPENEXR_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(OPENEXR_INSTALL_TOOLS OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

message(STATUS "Fetching OpenEXR, please wait...")
FetchContent_Declare(OpenEXR GIT_REPOSITORY https://github.com/AcademySoftwareFoundation/openexr
	GIT_TAG 69b2604fc76e370615438bdc8d2cd95b9349c12e GIT_SHALLOW TRUE)
set(OPENEXR_VERSION "3.5.2")

FetchContent_MakeAvailable(OpenEXR)
FetchContent_GetProperties(OpenEXR)

list(APPEND GARDEN_LINK_LIBS OpenEXR::OpenEXR)