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

#***********************************************************************************************************************
function(stripExecutable STRIP_EXE_NAME)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		add_custom_command(TARGET ${STRIP_EXE_NAME} POST_BUILD VERBATIM
			COMMAND ${CMAKE_COMMAND} -E echo "Stripping ${STRIP_EXE_NAME} executable..."
			COMMAND objcopy --only-keep-debug "$<TARGET_FILE:${STRIP_EXE_NAME}>" "${STRIP_EXE_NAME}.debug"
			COMMAND strip ARGS --strip-all "$<TARGET_FILE:${STRIP_EXE_NAME}>")
			# TODO: objcopy --add-gnu-debuglink=myprogram.debug myprogram
	elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
		add_custom_command(TARGET ${STRIP_EXE_NAME} POST_BUILD VERBATIM
			COMMAND ${CMAKE_COMMAND} -E echo "Stripping ${STRIP_EXE_NAME} executable..."
			COMMAND strip ARGS "$<TARGET_FILE:${STRIP_EXE_NAME}>")
			# TODO: objcopy alternative on arm
	endif()
endfunction()

#***********************************************************************************************************************
macro(collectPackShaders PACK_CACHES_DIR PACK_RESOURCES_DIR
	IS_RELEASE_EDITOR IS_RELEASE_DEBUGGING PACK_SHADERS PACK_RESOURCES)
	file(GLOB_RECURSE PACK_SHADER_PATHS ${${PACK_RESOURCES_DIR}}/shaders/*.vert
		${${PACK_RESOURCES_DIR}}/shaders/*.frag ${${PACK_RESOURCES_DIR}}/shaders/*.comp)
	
	foreach(SHADER ${PACK_SHADER_PATHS})
		if((NOT ${IS_RELEASE_EDITOR} AND SHADER MATCHES "editor") OR
			(NOT ${IS_RELEASE_DEBUGGING} AND SHADER MATCHES "debug"))
			continue()
		endif()

		string(REPLACE ${${PACK_RESOURCES_DIR}}/ "" SHADER_PART ${SHADER})
		string(REGEX REPLACE "\\.[^.]*$" "" SHADER_PATH ${SHADER_PART})
		list(APPEND ${PACK_RESOURCES} "${${PACK_CACHES_DIR}}/${SHADER_PART}.spv")
		list(APPEND ${PACK_RESOURCES} "${SHADER_PART}.spv")

		if(NOT ${SHADER_PATH} IN_LIST ${PACK_SHADERS})
			list(APPEND ${PACK_SHADERS} "${SHADER_PATH}")
			list(APPEND ${PACK_RESOURCES} "${${PACK_CACHES_DIR}}/${SHADER_PATH}.gslh")
			list(APPEND ${PACK_RESOURCES} "${SHADER_PATH}.gslh")
		endif()
	endforeach()
endmacro()

#***********************************************************************************************************************
macro(collectPackEqui2cubes PACK_CACHES_DIR PACK_RESOURCES_DIR
	IS_RELEASE_EDITOR IS_RELEASE_DEBUGGING PACK_EQUI2CUBES PACK_RESOURCES)
	file(GLOB_RECURSE PACK_EQUI2CUBE_PATHS
		${${PACK_RESOURCES_DIR}}/images/*.exr ${${PACK_RESOURCES_DIR}}/images/*.hdr
		${${PACK_RESOURCES_DIR}}/models/*.exr ${${PACK_RESOURCES_DIR}}/models/*.hdr)

	foreach(EQUI2CUBE ${PACK_EQUI2CUBE_PATHS})
		if(NOT EQUI2CUBE MATCHES "cubemap" OR
			(NOT ${IS_RELEASE_EDITOR} AND EQUI2CUBE MATCHES "editor") OR
			(NOT ${IS_RELEASE_DEBUGGING} AND EQUI2CUBE MATCHES "debug"))
			continue()
		endif()

		string(REPLACE ${${PACK_RESOURCES_DIR}}/ "" EQUI2CUBE_PART ${EQUI2CUBE})
		string(REGEX REPLACE "\\.[^.]*$" "" EQUI2CUBE_PATH ${EQUI2CUBE_PART})
		list(APPEND ${PACK_EQUI2CUBES} "${EQUI2CUBE_PART}")
		list(APPEND ${PACK_RESOURCES} "${${PACK_CACHES_DIR}}/${EQUI2CUBE_PATH}-nx.exr")
		list(APPEND ${PACK_RESOURCES} "${EQUI2CUBE_PATH}-nx.exr")
		list(APPEND ${PACK_RESOURCES} "${${PACK_CACHES_DIR}}/${EQUI2CUBE_PATH}-px.exr")
		list(APPEND ${PACK_RESOURCES} "${EQUI2CUBE_PATH}-px.exr")
		list(APPEND ${PACK_RESOURCES} "${${PACK_CACHES_DIR}}/${EQUI2CUBE_PATH}-ny.exr")
		list(APPEND ${PACK_RESOURCES} "${EQUI2CUBE_PATH}-ny.exr")
		list(APPEND ${PACK_RESOURCES} "${${PACK_CACHES_DIR}}/${EQUI2CUBE_PATH}-py.exr")
		list(APPEND ${PACK_RESOURCES} "${EQUI2CUBE_PATH}-py.exr")
		list(APPEND ${PACK_RESOURCES} "${${PACK_CACHES_DIR}}/${EQUI2CUBE_PATH}-nz.exr")
		list(APPEND ${PACK_RESOURCES} "${EQUI2CUBE_PATH}-nz.exr")
		list(APPEND ${PACK_RESOURCES} "${${PACK_CACHES_DIR}}/${EQUI2CUBE_PATH}-pz.exr")
		list(APPEND ${PACK_RESOURCES} "${EQUI2CUBE_PATH}-pz.exr")
	endforeach()
endmacro()

#***********************************************************************************************************************
macro(collectPackResources PACK_RESOURCES_DIR IS_RELEASE_EDITOR IS_RELEASE_DEBUGGING PACK_RESOURCES)
	file(GLOB_RECURSE PACK_ANY_RESOURCE_PATHS
		${${PACK_RESOURCES_DIR}}/images/*.webp ${${PACK_RESOURCES_DIR}}/images/*.png
		${${PACK_RESOURCES_DIR}}/images/*.jpg ${${PACK_RESOURCES_DIR}}/fonts/*.ttf)

	foreach(ANY_RESOURCE ${PACK_ANY_RESOURCE_PATHS})
		if(ANY_RESOURCE MATCHES "cubemap" OR 
			(NOT ${IS_RELEASE_EDITOR} AND ANY_RESOURCE MATCHES "editor") OR
			(NOT ${IS_RELEASE_DEBUGGING} AND ANY_RESOURCE MATCHES "debug"))
			continue()
		endif()

		string(REPLACE ${${PACK_RESOURCES_DIR}}/ "" ANY_RESOURCE_PART ${ANY_RESOURCE})
		list(APPEND ${PACK_RESOURCES} "${ANY_RESOURCE}")
		list(APPEND ${PACK_RESOURCES} "${ANY_RESOURCE_PART}")
	endforeach()
endmacro()

#***********************************************************************************************************************
function(packResources PACK_EXE_NAME PACK_CACHES_DIR PACK_APP_RES_DIR
	PACK_GARDEN_RES_DIR IS_RELEASE_EDITOR IS_RELEASE_DEBUGGING)
	set(PACK_APP_RES_PATHS)

	set(PACK_GARDEN_SHADERS)
	set(PACK_APP_SHADERS)

	collectPackShaders(PACK_CACHES_DIR PACK_GARDEN_RES_DIR IS_RELEASE_EDITOR
		IS_RELEASE_DEBUGGING PACK_GARDEN_SHADERS PACK_APP_RES_PATHS)
	collectPackShaders(PACK_CACHES_DIR PACK_APP_RES_DIR IS_RELEASE_EDITOR
		IS_RELEASE_DEBUGGING PACK_APP_SHADERS PACK_APP_RES_PATHS)

	set(PACK_GARDEN_EQUI2CUBES)
	set(PACK_APP_EQUI2CUBES)

	collectPackEqui2cubes(PACK_CACHES_DIR PACK_GARDEN_RES_DIR IS_RELEASE_EDITOR
		IS_RELEASE_DEBUGGING PACK_GARDEN_EQUI2CUBES PACK_APP_RES_PATHS)
	collectPackEqui2cubes(PACK_CACHES_DIR PACK_APP_RES_DIR IS_RELEASE_EDITOR
		IS_RELEASE_DEBUGGINGR PACK_APP_EQUI2CUBES PACK_APP_RES_PATHS)

	collectPackResources(PACK_GARDEN_RES_DIR IS_RELEASE_EDITOR IS_RELEASE_DEBUGGINGR PACK_APP_RES_PATHS)
	collectPackResources(PACK_APP_RES_DIR IS_RELEASE_EDITOR IS_RELEASE_DEBUGGINGR PACK_APP_RES_PATHS)
	
	add_custom_command(TARGET ${PACK_EXE_NAME} POST_BUILD VERBATIM
		COMMAND ${CMAKE_COMMAND} -E echo "Compiling garden shaders..."
		COMMAND $<TARGET_FILE:gslc> -i ${PACK_GARDEN_RES_DIR} 
			-o ${PACK_CACHES_DIR} -I ${PACK_GARDEN_RES_DIR}/shaders ${PACK_GARDEN_SHADERS}

		COMMAND ${CMAKE_COMMAND} -E echo "Compiling ${PACK_EXE_NAME} shaders..."
		COMMAND $<TARGET_FILE:gslc> -i ${PACK_APP_RES_DIR} 
			-o ${PACK_CACHES_DIR} -I ${PACK_APP_RES_DIR}/shaders 
			-I ${PACK_GARDEN_RES_DIR}/shaders ${PACK_APP_SHADERS}

		COMMAND ${CMAKE_COMMAND} -E echo "Convering garden images..."
		COMMAND $<TARGET_FILE:equi2cube> -i ${PACK_GARDEN_RES_DIR} 
			-o ${PACK_CACHES_DIR} ${PACK_GARDEN_EQUI2CUBES}

		COMMAND ${CMAKE_COMMAND} -E echo "Convering ${PACK_EXE_NAME} images..."
		COMMAND $<TARGET_FILE:equi2cube> -i ${PACK_APP_RES_DIR} 
			-o ${PACK_CACHES_DIR} ${PACK_APP_EQUI2CUBES}

		COMMAND ${CMAKE_COMMAND} -E echo "Packing ${PACK_EXE_NAME} resources..."
		COMMAND $<TARGET_FILE:packer>
			$<TARGET_FILE_DIR:${PACK_EXE_NAME}>/resources.pack ${PACK_APP_RES_PATHS})
endfunction()