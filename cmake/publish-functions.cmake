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

#***********************************************************************************************************************
function(stripExecutable STRIP_TARGET)
    if(LINUX)
		add_custom_command(TARGET ${STRIP_TARGET} POST_BUILD VERBATIM
			COMMAND ${CMAKE_COMMAND} -E echo "Stripping $<TARGET_FILE_NAME:${STRIP_TARGET}> executable..."
			COMMAND objcopy --only-keep-debug "$<TARGET_FILE:${STRIP_TARGET}>" 
				"$<TARGET_FILE_NAME:${STRIP_TARGET}>.debug"
			COMMAND strip ARGS --strip-all "$<TARGET_FILE:${STRIP_TARGET}>")
			# TODO: objcopy --add-gnu-debuglink=myprogram.debug myprogram
	elseif(APPLE)
		add_custom_command(TARGET ${STRIP_TARGET} POST_BUILD VERBATIM
			COMMAND ${CMAKE_COMMAND} -E echo "Stripping $<TARGET_FILE_NAME:${STRIP_TARGET}> executable..."
			COMMAND strip ARGS "$<TARGET_FILE:${STRIP_TARGET}>")
			# TODO: objcopy alternative on arm
	endif()
endfunction()

#***********************************************************************************************************************
macro(collectPackShaders PACK_CACHE_DIR PACK_RESOURCES_DIR
	INCLUDE_EDITOR INCLUDE_DEBUG PACK_SHADERS PACK_RESOURCES)

	set(PACK_SHADERS_DIR ${${PACK_RESOURCES_DIR}}/shaders)
	file(GLOB_RECURSE PACK_SHADER_PATHS ${PACK_SHADERS_DIR}/*.vert ${PACK_SHADERS_DIR}/*.frag 
		${PACK_SHADERS_DIR}/*.comp ${PACK_SHADERS_DIR}/*.rgen ${PACK_SHADERS_DIR}/*.rint ${PACK_SHADERS_DIR}/*.rahit 
		${PACK_SHADERS_DIR}/*.rchit ${PACK_SHADERS_DIR}/*.rmiss  ${PACK_SHADERS_DIR}/*.rcall 
		${PACK_SHADERS_DIR}/*.mesh ${PACK_SHADERS_DIR}/*.task)
	
	foreach(SHADER ${PACK_SHADER_PATHS})
		if((NOT ${INCLUDE_EDITOR} AND SHADER MATCHES "editor") OR
			(NOT ${INCLUDE_DEBUG} AND SHADER MATCHES "debug"))
			continue()
		endif()

		string(REPLACE ${${PACK_RESOURCES_DIR}}/ "" SHADER_PART ${SHADER})
		string(REGEX REPLACE "\\.[^.]*$" "" SHADER_PATH ${SHADER_PART})
		list(APPEND ${PACK_RESOURCES} "${${PACK_CACHE_DIR}}/${SHADER_PART}.spv")
		list(APPEND ${PACK_RESOURCES} "${SHADER_PART}.spv")

		# Note: Preventing double .vert and .frag shader name and ray tracing shader variants addition.
		if((NOT ${SHADER_PATH} IN_LIST ${PACK_SHADERS}) AND (NOT "${SHADER_PATH}" MATCHES "\\."))
			list(APPEND ${PACK_SHADERS} "${SHADER_PATH}")
			list(APPEND ${PACK_RESOURCES} "${${PACK_CACHE_DIR}}/${SHADER_PATH}.gslh")
			list(APPEND ${PACK_RESOURCES} "${SHADER_PATH}.gslh")
		endif()
	endforeach()
endmacro()

#***********************************************************************************************************************
macro(collectPackImages PACK_CACHE_DIR PACK_RESOURCES_DIR INCLUDE_EDITOR 
	INCLUDE_DEBUG PACK_IMAGES PACK_CUBEMAPS PACK_RESOURCES)

	file(GLOB_RECURSE PACK_IMAGE_PATHS
		${${PACK_RESOURCES_DIR}}/images/*.png ${${PACK_RESOURCES_DIR}}/models/*.png 
		${${PACK_RESOURCES_DIR}}/images/*.webp ${${PACK_RESOURCES_DIR}}/models/*.webp 
		${${PACK_RESOURCES_DIR}}/images/*.jpg ${${PACK_RESOURCES_DIR}}/models/*.jpg 
		${${PACK_RESOURCES_DIR}}/images/*.jpeg ${${PACK_RESOURCES_DIR}}/models/*.jpeg 
		${${PACK_RESOURCES_DIR}}/images/*.exr ${${PACK_RESOURCES_DIR}}/models/*.exr 
		${${PACK_RESOURCES_DIR}}/images/*.hdr ${${PACK_RESOURCES_DIR}}/models/*.hdr)

	foreach(IMAGE ${PACK_IMAGE_PATHS})
		if((NOT ${INCLUDE_EDITOR} AND IMAGE MATCHES "editor") OR
			(NOT ${INCLUDE_DEBUG} AND IMAGE MATCHES "debug"))
			continue()
		endif()

		string(REPLACE ${${PACK_RESOURCES_DIR}}/ "" IMAGE_PART ${IMAGE})
		string(REGEX REPLACE "\\.[^.]*$" "" IMAGE_PATH ${IMAGE_PART})

		if(IMAGE MATCHES "cubemap")
			list(APPEND ${PACK_CUBEMAPS} "${IMAGE_PART}")
			list(APPEND ${PACK_RESOURCES} "${${PACK_CACHE_DIR}}/${IMAGE_PATH}-nx.gic")
			list(APPEND ${PACK_RESOURCES} "${IMAGE_PATH}-nx.gic")
			list(APPEND ${PACK_RESOURCES} "${${PACK_CACHE_DIR}}/${IMAGE_PATH}-px.gic")
			list(APPEND ${PACK_RESOURCES} "${IMAGE_PATH}-px.gic")
			list(APPEND ${PACK_RESOURCES} "${${PACK_CACHE_DIR}}/${IMAGE_PATH}-ny.gic")
			list(APPEND ${PACK_RESOURCES} "${IMAGE_PATH}-ny.gic")
			list(APPEND ${PACK_RESOURCES} "${${PACK_CACHE_DIR}}/${IMAGE_PATH}-py.gic")
			list(APPEND ${PACK_RESOURCES} "${IMAGE_PATH}-py.gic")
			list(APPEND ${PACK_RESOURCES} "${${PACK_CACHE_DIR}}/${IMAGE_PATH}-nz.gic")
			list(APPEND ${PACK_RESOURCES} "${IMAGE_PATH}-nz.gic")
			list(APPEND ${PACK_RESOURCES} "${${PACK_CACHE_DIR}}/${IMAGE_PATH}-pz.gic")
			list(APPEND ${PACK_RESOURCES} "${IMAGE_PATH}-pz.gic")
		else()
			list(APPEND ${PACK_IMAGES} "${IMAGE_PART}")
			list(APPEND ${PACK_RESOURCES} "${${PACK_CACHE_DIR}}/${IMAGE_PATH}.gic")
			list(APPEND ${PACK_RESOURCES} "${IMAGE_PATH}.gic")
		endif()
	endforeach()
endmacro()

#***********************************************************************************************************************
macro(collectPackModels PACK_CACHE_DIR PACK_RESOURCES_DIR
	INCLUDE_EDITOR INCLUDE_DEBUG PACK_IMAGES PACK_RESOURCES)
	message(FATAL_ERROR "Not implemented yet")
endmacro()

#***********************************************************************************************************************
macro(collectPackJson2bsons PACK_CACHE_DIR PACK_RESOURCES_DIR
	INCLUDE_EDITOR INCLUDE_DEBUG PACK_JSONS PACK_RESOURCES)

	file(GLOB_RECURSE PACK_JSON_PATHS ${${PACK_RESOURCES_DIR}}/scenes/*.scene 
		${${PACK_RESOURCES_DIR}}/animations/*.anim ${${PACK_RESOURCES_DIR}}/configs/*.json)
	
	foreach(JSON ${PACK_JSON_PATHS})
		if((NOT ${INCLUDE_EDITOR} AND JSON MATCHES "editor") OR
			(NOT ${INCLUDE_DEBUG} AND JSON MATCHES "debug"))
			continue()
		endif()

		string(REPLACE ${${PACK_RESOURCES_DIR}}/ "" JSON_PATH ${JSON})
		list(APPEND ${PACK_JSONS} "${JSON_PATH}")
		list(APPEND ${PACK_RESOURCES} "${${PACK_CACHE_DIR}}/${JSON_PATH}")
		list(APPEND ${PACK_RESOURCES} "${JSON_PATH}")
	endforeach()
endmacro()

#***********************************************************************************************************************
macro(collectPackResources PACK_RESOURCES_DIR INCLUDE_EDITOR INCLUDE_DEBUG PACK_RESOURCES APP_RESOURCES)

	file(GLOB_RECURSE PACK_ANY_RESOURCE_PATHS 
		${${PACK_RESOURCES_DIR}}/fonts/*.ttf ${${PACK_RESOURCES_DIR}}/locales/*.txt)
	list(APPEND PACK_ANY_RESOURCE_PATHS ${${APP_RESOURCES}})

	foreach(ANY_RESOURCE ${PACK_ANY_RESOURCE_PATHS})
		if((NOT ${INCLUDE_EDITOR} AND ANY_RESOURCE MATCHES "editor") OR
			(NOT ${INCLUDE_DEBUG} AND ANY_RESOURCE MATCHES "debug"))
			continue()
		endif()

		if((NOT WIN32 AND ANY_RESOURCE MATCHES "windows") OR
			(NOT APPLE AND ANY_RESOURCE MATCHES "apple") OR
			(NOT LINUX AND ANY_RESOURCE MATCHES "linux"))
			continue()
		endif()

		string(REPLACE ${${PACK_RESOURCES_DIR}}/ "" ANY_RESOURCE_PART ${ANY_RESOURCE})
		list(APPEND ${PACK_RESOURCES} "${ANY_RESOURCE}")
		list(APPEND ${PACK_RESOURCES} "${ANY_RESOURCE_PART}")
	endforeach()
endmacro()

#***********************************************************************************************************************
function(packResources PACK_EXE_NAME PACK_CACHE_DIR PACK_APP_RES_DIR PACK_GARDEN_RES_DIR 
	PACK_APP_VERSION INCLUDE_EDITOR INCLUDE_DEBUG APP_RESOURCES)

	set(PACK_APP_RES_PATHS)
	set(PACK_GARDEN_SHADERS)
	set(PACK_APP_SHADERS)

	collectPackShaders(PACK_CACHE_DIR PROJECT_SOURCE_DIR INCLUDE_EDITOR
		INCLUDE_DEBUG PACK_GARDEN_SHADERS PACK_APP_RES_PATHS)
	collectPackShaders(PACK_CACHE_DIR PACK_APP_RES_DIR INCLUDE_EDITOR
		INCLUDE_DEBUG PACK_APP_SHADERS PACK_APP_RES_PATHS)

	set(PACK_GARDEN_IMAGES)
	set(PACK_APP_IMAGES)
	set(PACK_GARDEN_CUBEMAPS)
	set(PACK_APP_CUBEMAPS)

	collectPackImages(PACK_CACHE_DIR PACK_GARDEN_RES_DIR INCLUDE_EDITOR INCLUDE_DEBUG 
		PACK_GARDEN_IMAGES PACK_GARDEN_CUBEMAPS PACK_APP_RES_PATHS)
	collectPackImages(PACK_CACHE_DIR PACK_APP_RES_DIR INCLUDE_EDITOR INCLUDE_DEBUG 
		PACK_APP_IMAGES PACK_APP_CUBEMAPS PACK_APP_RES_PATHS)

	set(PACK_GARDEN_JSON2BSONS)
	set(PACK_APP_JSON2BSONS)

	collectPackJson2bsons(PACK_CACHE_DIR PACK_GARDEN_RES_DIR INCLUDE_EDITOR
		INCLUDE_DEBUG PACK_GARDEN_JSON2BSONS PACK_APP_RES_PATHS)
	collectPackJson2bsons(PACK_CACHE_DIR PACK_APP_RES_DIR INCLUDE_EDITOR
		INCLUDE_DEBUG PACK_APP_JSON2BSONS PACK_APP_RES_PATHS)

	collectPackResources(PACK_GARDEN_RES_DIR INCLUDE_EDITOR INCLUDE_DEBUG PACK_APP_RES_PATHS "")
	collectPackResources(PACK_APP_RES_DIR INCLUDE_EDITOR INCLUDE_DEBUG PACK_APP_RES_PATHS APP_RESOURCES)

	add_custom_command(TARGET ${PACK_EXE_NAME} POST_BUILD VERBATIM
		COMMAND ${CMAKE_COMMAND} -E echo "Cleaning up editor cache..."
		COMMAND ${CMAKE_COMMAND} -E remove_directory ${PACK_CACHE_DIR}

		COMMAND ${CMAKE_COMMAND} -E echo "Compiling Garden shaders..."
		COMMAND $<TARGET_FILE:gslc> -i ${PROJECT_SOURCE_DIR} -o ${PACK_CACHE_DIR} 
			-I ${PROJECT_SOURCE_DIR}/shaders ${PACK_GARDEN_SHADERS}

		COMMAND ${CMAKE_COMMAND} -E echo "Compiling ${PACK_EXE_NAME} shaders..."
		COMMAND $<TARGET_FILE:gslc> -i ${PACK_APP_RES_DIR} -o ${PACK_CACHE_DIR} 
			-I ${PROJECT_SOURCE_DIR}/shaders -I ${PACK_APP_RES_DIR}/shaders ${PACK_APP_SHADERS}

		COMMAND ${CMAKE_COMMAND} -E echo "Converting Garden images..."
		COMMAND $<TARGET_FILE:imagec> -i ${PACK_GARDEN_RES_DIR} 
			-o ${PACK_CACHE_DIR} ${PACK_GARDEN_IMAGES}

		COMMAND ${CMAKE_COMMAND} -E echo "Converting ${PACK_EXE_NAME} images..."
		COMMAND $<TARGET_FILE:imagec> -i ${PACK_APP_RES_DIR} 
			-o ${PACK_CACHE_DIR} ${PACK_APP_IMAGES}

		COMMAND ${CMAKE_COMMAND} -E echo "Converting Garden JSON files..."
		COMMAND $<TARGET_FILE:json2bson> -i ${PACK_GARDEN_RES_DIR} 
			-o ${PACK_CACHE_DIR} ${PACK_GARDEN_JSON2BSONS}

		COMMAND ${CMAKE_COMMAND} -E echo "Converting ${PACK_EXE_NAME} JSON files..."
		COMMAND $<TARGET_FILE:json2bson> -i ${PACK_APP_RES_DIR} 
			-o ${PACK_CACHE_DIR} ${PACK_APP_JSON2BSONS}

		COMMAND ${CMAKE_COMMAND} -E echo "Packing ${PACK_EXE_NAME} resources..."
		COMMAND $<TARGET_FILE:packer> -v ${PACK_APP_VERSION} 
			$<TARGET_FILE_DIR:${PACK_EXE_NAME}>/resources.pack ${PACK_APP_RES_PATHS})
endfunction()