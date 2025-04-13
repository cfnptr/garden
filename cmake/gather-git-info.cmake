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

# Gathers current GIT information
find_package(Git)

execute_process(COMMAND 
	"${GIT_EXECUTABLE}" log -1 --format=%s 
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" 
	OUTPUT_VARIABLE GARDEN_GIT_COMMIT 
	ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND 
	"${GIT_EXECUTABLE}" describe --match=NeVeRmAtCh --always --abbrev=40 --dirty 
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" 
	OUTPUT_VARIABLE GARDEN_GIT_SHA1 
	ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND 
	"${GIT_EXECUTABLE}" log -1 --format=%ad --date=local 
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" 
	OUTPUT_VARIABLE GARDEN_GIT_DATE 
	ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)