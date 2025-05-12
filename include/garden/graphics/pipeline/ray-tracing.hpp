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

/***********************************************************************************************************************
 * @file
 * @brief Ray tracing pipeline functions.
 */

#pragma once
#include "garden/graphics/pipeline.hpp"
#include "garden/graphics/acceleration-structure/blas.hpp"
#include "garden/graphics/acceleration-structure/tlas.hpp" // TODO: maybe move to the API?

namespace garden::graphics
{

class RayTracingPipelineExt;

/**
 * @brief Ray tracing stage container.
 */
class RayTracingPipeline final : public Pipeline
{
public:
	/**
	 * @brief Ray tracing pipeline shader code overrides.
	 * @details It allows to override pipeline shader code.
	 */
	struct ShaderOverrides final
	{
		vector<uint8> rayGenerationCode;
		vector<uint8> intersectionCode;
		vector<uint8> anyHitCode;
		vector<uint8> closestHitCode;
		vector<uint8> missCode;
		vector<uint8> headerData;
	};
	/**
	 * @brief Ray tracing pipeline create data container.
	 * @warning In most cases you should use @ref GraphicsSystem functions.
	 */
	struct RayTracingCreateData : public CreateData
	{
		uint8 _alignment0 = 0;
		uint16 _alignment1 = 0;
		vector<uint8> rayGenerationCode;
		vector<uint8> intersectionCode;
		vector<uint8> anyHitCode;
		vector<uint8> closestHitCode;
		vector<uint8> missCode;
	};
private:
	uint16 _alignment = 0;

	RayTracingPipeline(const fs::path& path, uint32 maxBindlessCount, bool useAsyncRecording, uint64 pipelineVersion) :
		Pipeline(PipelineType::RayTracing, path, maxBindlessCount, useAsyncRecording, pipelineVersion) { }
	RayTracingPipeline(RayTracingCreateData& createData, bool useAsyncRecording);

	void createVkInstance(RayTracingCreateData& createData);

	friend class RayTracingPipelineExt;
	friend class LinearPool<RayTracingPipeline>;
public:
	/**
	 * @brief Creates a new empty ray tracing pipeline data container.
	 * @note Use @ref GraphicsSystem to create, destroy and access ray tracing pipelines.
	 */
	RayTracingPipeline() = default;



	//******************************************************************************************************************
	// Render commands
	//******************************************************************************************************************

	
};

/***********************************************************************************************************************
 * @brief Ray tracing pipeline resource extension mechanism.
 * @warning Use only if you know what you are doing!
 */
class RayTracingPipelineExt final
{
public:
	/**
	 * @brief Creates a new ray tracing pipeline data.
	 * @warning In most cases you should use @ref GraphicsSystem functions.
	 * 
	 * @param[in,out] createData target compute pipeline create data
	 * @param useAsyncRecording use multithreaded render commands recording
	 */
	static RayTracingPipeline create(RayTracingPipeline::RayTracingCreateData& createData, bool useAsyncRecording)
	{
		return RayTracingPipeline(createData, useAsyncRecording);
	}
	/**
	 * @brief Moves internal ray tracing pipeline objects.
	 * @warning In most cases you should use @ref GraphicsSystem functions.
	 * 
	 * @param[in,out] source source ray tracing pipeline instance
	 * @param[in,out] destination destination ray tracing pipeline instance
	 */
	static void moveInternalObjects(RayTracingPipeline& source, RayTracingPipeline& destination) noexcept
	{
		
		PipelineExt::moveInternalObjects(source, destination);
	}
};

} // namespace garden::graphics