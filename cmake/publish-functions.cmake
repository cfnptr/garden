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

function(stripExecutable STRIP_EXE_NAME)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
		add_custom_command(TARGET ${STRIP_EXE_NAME} POST_BUILD VERBATIM
			COMMAND ${CMAKE_COMMAND} -E echo "Stripping ${STRIP_EXE_NAME} executable..."
			COMMAND objcopy --only-keep-debug "$<TARGET_FILE:${STRIP_EXE_NAME}>" "${STRIP_EXE_NAME}.debug"
			COMMAND strip ARGS --strip-all "$<TARGET_FILE:${STRIP_EXE_NAME}>")
			# TODO: objcopy --add-gnu-debuglink=myprogram.debug myprogram
	endif()
endfunction()

#---------------------------------------------------------------------------------------------------
macro(collectPackShaders PACK_CACHES_DIR PACK_RESOURCES_DIR 
	IS_RELEASE_EDITOR PACK_SHADERS PACK_RESOURCES)

	file(GLOB_RECURSE PACK_SHADER_PATHS ${${PACK_RESOURCES_DIR}}/shaders/*.vert
		${${PACK_RESOURCES_DIR}}/shaders/*.frag ${${PACK_RESOURCES_DIR}}/shaders/*.comp)
	
	foreach(SHADER ${PACK_SHADER_PATHS})
		if(SHADER MATCHES "debug" OR (NOT ${IS_RELEASE_EDITOR} AND SHADER MATCHES "editor"))
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

#---------------------------------------------------------------------------------------------------
macro(collectPackEqui2cubes PACK_CACHES_DIR PACK_RESOURCES_DIR 
	IS_RELEASE_EDITOR PACK_EQUI2CUBES PACK_RESOURCES)

	file(GLOB_RECURSE PACK_EQUI2CUBE_PATHS
		${${PACK_RESOURCES_DIR}}/images/*.exr ${${PACK_RESOURCES_DIR}}/images/*.hdr
		${${PACK_RESOURCES_DIR}}/models/*.exr ${${PACK_RESOURCES_DIR}}/models/*.hdr)

	foreach(EQUI2CUBE ${PACK_EQUI2CUBE_PATHS})
		if(NOT EQUI2CUBE MATCHES "cubemap" OR EQUI2CUBE MATCHES "debug" OR
			(NOT ${IS_RELEASE_EDITOR} AND EQUI2CUBE MATCHES "editor"))
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

#---------------------------------------------------------------------------------------------------
macro(collectPackResources PACK_RESOURCES_DIR IS_RELEASE_EDITOR PACK_RESOURCES)
	file(GLOB_RECURSE PACK_ANY_RESOURCE_PATHS
		${${PACK_RESOURCES_DIR}}/images/*.webp ${${PACK_RESOURCES_DIR}}/images/*.png
		${${PACK_RESOURCES_DIR}}/images/*.jpg ${${PACK_RESOURCES_DIR}}/fonts/*.ttf)

	foreach(ANY_RESOURCE ${PACK_ANY_RESOURCE_PATHS})
		if(ANY_RESOURCE MATCHES "cubemap" OR ANY_RESOURCE MATCHES "debug" OR
			(${IS_RELEASE_EDITOR} AND ANY_RESOURCE MATCHES "editor"))
			continue()
		endif()

		string(REPLACE ${${PACK_RESOURCES_DIR}}/ "" ANY_RESOURCE_PART ${ANY_RESOURCE})
		list(APPEND ${PACK_RESOURCES} "${ANY_RESOURCE}")
		list(APPEND ${PACK_RESOURCES} "${ANY_RESOURCE_PART}")
	endforeach()
endmacro()

#---------------------------------------------------------------------------------------------------
function(packResources PACK_EXE_NAME PACK_CACHES_DIR
	PACK_APP_RES_DIR PACK_GARDEN_RES_DIR IS_RELEASE_EDITOR)

	set(PACK_APP_RES_PATHS)

	set(PACK_GARDEN_SHADERS)
	set(PACK_APP_SHADERS)
	collectPackShaders(PACK_CACHES_DIR PACK_GARDEN_RES_DIR
		IS_RELEASE_EDITOR PACK_GARDEN_SHADERS PACK_APP_RES_PATHS)
	collectPackShaders(PACK_CACHES_DIR PACK_APP_RES_DIR
		IS_RELEASE_EDITOR PACK_APP_SHADERS PACK_APP_RES_PATHS)

	set(PACK_GARDEN_EQUI2CUBES)
	set(PACK_APP_EQUI2CUBES)
	collectPackEqui2cubes(PACK_CACHES_DIR PACK_GARDEN_RES_DIR
		IS_RELEASE_EDITOR PACK_GARDEN_EQUI2CUBES PACK_APP_RES_PATHS)
	collectPackEqui2cubes(PACK_CACHES_DIR PACK_APP_RES_DIR
		IS_RELEASE_EDITOR PACK_APP_EQUI2CUBES PACK_APP_RES_PATHS)

	collectPackResources(PACK_GARDEN_RES_DIR IS_RELEASE_EDITOR PACK_APP_RES_PATHS)
	collectPackResources(PACK_APP_RES_DIR IS_RELEASE_EDITOR PACK_APP_RES_PATHS)
	
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