// Copyright 2022-2025 Nikita Fediuchin. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*******************************************************************************************************************
 * @file
 * @brief Common shader compiler functions.
 */

#pragma once
#include "garden/graphics/pipeline/compute.hpp"
#include "garden/graphics/pipeline/graphics.hpp"
#include "garden/graphics/pipeline/ray-tracing.hpp"

#if GARDEN_PACK_RESOURCES && !defined(GSL_COMPILER)
#include "pack/reader.hpp"
#endif

#if GARDEN_OS_MACOS
#define GARDEN_VULKAN_SHADER_VERSION_STRING "vulkan1.3"
#else
#define GARDEN_VULKAN_SHADER_VERSION_STRING "vulkan1.4"
#endif

namespace garden::graphics
{

/**
 * @brief Garden Shading Language compiler. (Wrapper around GLSL compiler).
 * 
 * @details
 * Shader compiler is a specialized software tool that converts shader code written in a high-level shading 
 * language (such as GLSL for OpenGL and Vulkan) into a lower-level or machine-specific format that can be 
 * executed directly by the GPU. Shaders are programs that run on the GPU to perform various tasks related to 
 * rendering, such as calculating vertex positions, generating textures, or determining pixel colors. 
 * They are an integral part of modern graphics and compute pipelines, enabling detailed control over the 
 * visual appearance of 3D scenes and the execution of parallel computations.
 */
class GslCompiler final
{
public:
	/**
	 * @brief Compiled shader file magic number size in bytes.
	 */
	static constexpr psize gslMagicSize = 4;

	/**
	 * @brief Graphics pipeline shader data.
	 */
	struct GraphicsData : public GraphicsPipeline::GraphicsCreateData
	{
		#if !GARDEN_PACK_RESOURCES || defined(GSL_COMPILER)
		fs::path cachePath;
		fs::path resourcesPath;
		#endif
		#if GARDEN_PACK_RESOURCES && !defined(GSL_COMPILER)
		pack::Reader* packReader = nullptr;
		int32 threadIndex = 0;
		#endif
	};
	/**
	 * @brief Compute pipeline shader data.
	 */
	struct ComputeData : public ComputePipeline::ComputeCreateData
	{
		#if !GARDEN_PACK_RESOURCES || defined(GSL_COMPILER)
		fs::path cachePath;
		fs::path resourcesPath;
		#endif
		#if GARDEN_PACK_RESOURCES && !defined(GSL_COMPILER)
		pack::Reader* packReader = nullptr;
		int32 threadIndex = 0;
		#endif
	};
	/**
	 * @brief Ray tracing pipeline shader data.
	 */
	struct RayTracingData : public RayTracingPipeline::RayTracingCreateData
	{
		#if !GARDEN_PACK_RESOURCES || defined(GSL_COMPILER)
		fs::path cachePath;
		fs::path resourcesPath;
		#endif
		#if GARDEN_PACK_RESOURCES && !defined(GSL_COMPILER)
		pack::Reader* packReader = nullptr;
		int32 threadIndex = 0;
		#endif
	};

	/**
	 * @brief Graphics pipeline file magic number
	 */
	static constexpr string_view graphicsGslMagic = "GSLG";
	/**
	 * @brief Graphics pipeline file magic number
	 */
	static constexpr string_view computeGslMagic = "GSLC";
	/**
	 * @brief Ray tracing pipeline file magic number
	 */
	static constexpr string_view rayTracingGslMagic = "GSLR";
	
	/**
	 * @brief Loads graphics pipeline shader data.
	 * @param[in,out] data target shader data container
	 */
	static void loadGraphicsShaders(GraphicsData& data);
	/**
	 * @brief Loads compute pipeline shader data.
	 * @param[in,out] data target shader data container
	 */
	static void loadComputeShader(ComputeData& data);
	/**
	 * @brief Loads ray tracing pipeline shader data.
	 * @param[in,out] data target shader data container
	 */
	static void loadRayTracingShaders(RayTracingData& data);

	#if !GARDEN_PACK_RESOURCES || defined(GSL_COMPILER)
	/**
	 * @brief Compiles graphics shaders. (.vert, .frag, .mesh, .task)
	 * 
	 * @param inputPath target shaders input directory path
	 * @param outputPath compiled shaders output directory path
	 * @param includePaths include shaders directory paths
	 * @param[in,out] data input and compiled shaders data
	 * 
	 * @return True on success and writes processed data, otherwise false if shaders not found.
	 * @throw CompileError on shaders compilation or syntax error.
	 */
	static bool compileGraphicsShaders(const fs::path& inputPath, const fs::path& outputPath,
		const vector<fs::path>& includePaths, GraphicsData& data);

	/**
	 * @brief Compiles compute shader. (.comp)
	 * 
	 * @param inputPath target shader input directory path
	 * @param outputPath compiled shader output directory path
	 * @param includePaths include shaders directory paths
	 * @param[in,out] data input and compiled shader data
	 * 
	 * @return True on success and writes processed data, otherwise false if shader not found.
	 * @throw CompileError on shader compilation or syntax error.
	 */
	static bool compileComputeShader(const fs::path& inputPath, const fs::path& outputPath,
		const vector<fs::path>& includePaths, ComputeData& data);

	/**
	 * @brief Compiles ray tracing shaders. (.rgen, rint, .rahit, rchit, .rmiss, .rcall)
	 * 
	 * @param inputPath target shader input directory path
	 * @param outputPath compiled shader output directory path
	 * @param includePaths include shaders directory paths
	 * @param[in,out] data input and compiled shader data
	 * 
	 * @return True on success and writes processed data, otherwise false if shader not found.
	 * @throw CompileError on shader compilation or syntax error.
	 */
	static bool compileRayTracingShaders(const fs::path& inputPath, const fs::path& outputPath,
		const vector<fs::path>& includePaths, RayTracingData& data);
	#endif
};

} // namespace garden::graphics