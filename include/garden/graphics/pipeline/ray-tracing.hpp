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
#include "garden/graphics/buffer.hpp"
#include "garden/graphics/pipeline.hpp"

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
	 * @brief Ray tracing pipeline shader hit group region information container.
	 */
	struct HitGroupRegion final
	{
		uint64 deviceAddress = 0;
		uint32 stride = 0;
		uint32 size = 0;
	};
	/**
	 * @brief Ray tracing pipeline variant SBT group regions container.
	 */
	struct SbtGroupRegions final
	{
		HitGroupRegion rayGenRegion;
		HitGroupRegion missRegion;
		HitGroupRegion hitRegion;
		HitGroupRegion callRegion;
	};
	/**
	 * @brief Ray tracing pipeline shader binding table container;
	 */
	struct SBT final
	{
		vector<SbtGroupRegions> groupRegions;
		ID<Buffer> buffer = {};
	};

	/**
	 * @brief Ray tracing pipeline shader hit group data container.
	 */
	struct HitGroupData final
	{
		vector<uint8> intersectionCode;
		vector<uint8> anyHitCode;
		vector<uint8> closestHitCode;
		bool hasIntersectShader = false;
		bool hasAnyHitShader = false;
		bool hasClosHitShader = false;
	};
	/**
	 * @brief Ray tracing pipeline shader code overrides.
	 * @details It allows to override pipeline shader code.
	 */
	struct ShaderOverrides final
	{
		vector<uint8> headerData;
		vector<vector<uint8>> rayGenGroups;
		vector<vector<uint8>> missGroups;
		vector<HitGroupData> hitGroups;
		vector<vector<uint8>> callGroups;
	};
	/**
	 * @brief Ray tracing pipeline create data container.
	 * @warning In most cases you should use @ref GraphicsSystem functions.
	 */
	struct RayTracingCreateData : public CreateData
	{	
		uint8 _alignment0 = 0;
		uint16 _alignment1 = 0;
		vector<vector<uint8>> rayGenGroups;
		vector<vector<uint8>> missGroups;
		vector<HitGroupData> hitGroups;
		vector<vector<uint8>> callGroups;
		uint32 rayRecursionDepth = 1;
	};
private:
	uint8 rayGenGroupCount = 0;
	uint8 missGroupCount = 0;
	uint8 hitGroupCount = 0;
	uint8 callGroupCount = 0;

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

	/**
	 * @brief Creates and transfers ray tracing pipeline shader binding table. (SBT)
	 * @param flags additional SBT buffer flags
	 */
	SBT createSBT(Buffer::Usage flags = {});

	/**
	 * @brief Executes ray tracing shader with specified SBT and 3D generation group size.
	 * 
	 * @param[in] sbt target RT pipeline shader binding table
	 * @param count ray tracing generation group 3D size
	 */
	void traceRays(const SBT& sbt, uint3 count);
	/**
	 * @brief Executes ray tracing shader with specified SBT and 2D generation group size.
	 * @details See the @ref traceRays().
	 * 
	 * @param[in] sbt target RT pipeline shader binding table
	 * @param count ray tracing generation group 2D size
	 */
	void traceRays(const SBT& sbt, uint2 count)
	{
		traceRays(sbt, uint3(count.x, count.y, 1));
	}
	/**
	 * @brief Executes ray tracing shader with specified SBT and 1D generation group size.
	 * @details See the @ref traceRays().
	 * 
	 * @param[in] sbt target RT pipeline shader binding table
	 * @param count ray tracing generation group 1D size
	 */
	void traceRays(const SBT& sbt, uint32 count)
	{
		traceRays(sbt, uint3(count, 1, 1));
	}
};

/***********************************************************************************************************************
 * @brief Ray tracing pipeline resource extension mechanism.
 * @warning Use only if you know what you are doing!
 */
class RayTracingPipelineExt final
{
public:
	/**
	 * @brief Returns ray tracing pipeline ray generation shader group count.
	 * @warning In most cases you should use @ref GraphicsPipeline functions.
	 * @param[in] pipeline target ray tracing pipeline instance
	 */
	static uint8& getRayGenGroupCount(RayTracingPipeline& pipeline) { return pipeline.rayGenGroupCount; }
	/**
	 * @brief Returns ray tracing pipeline ray miss shader group count.
	 * @warning In most cases you should use @ref GraphicsPipeline functions.
	 * @param[in] pipeline target ray tracing pipeline instance
	 */
	static uint8& getMissGroupCount(RayTracingPipeline& pipeline) { return pipeline.missGroupCount; }
	/**
	 * @brief Returns ray tracing pipeline ray hit shader group count.
	 * @warning In most cases you should use @ref GraphicsPipeline functions.
	 * @param[in] pipeline target ray tracing pipeline instance
	 */
	static uint8& getHitGroupCount(RayTracingPipeline& pipeline) { return pipeline.hitGroupCount; }
	/**
	 * @brief Returns ray tracing pipeline callable shader group count.
	 * @warning In most cases you should use @ref GraphicsPipeline functions.
	 * @param[in] pipeline target ray tracing pipeline instance
	 */
	static uint8& getCallGroupCount(RayTracingPipeline& pipeline) { return pipeline.callGroupCount; }

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
		RayTracingPipelineExt::getRayGenGroupCount(destination) = 
			std::move(RayTracingPipelineExt::getRayGenGroupCount(source));
		RayTracingPipelineExt::getMissGroupCount(destination) = 
			std::move(RayTracingPipelineExt::getMissGroupCount(source));
		RayTracingPipelineExt::getHitGroupCount(destination) = 
			std::move(RayTracingPipelineExt::getHitGroupCount(source));
		RayTracingPipelineExt::getCallGroupCount(destination) = 
			std::move(RayTracingPipelineExt::getCallGroupCount(source));
		PipelineExt::moveInternalObjects(source, destination);
	}
};

} // namespace garden::graphics