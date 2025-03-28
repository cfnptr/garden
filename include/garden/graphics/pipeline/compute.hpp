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
 * @brief Compute pipeline functions.
 */

#pragma once
#include "garden/graphics/pipeline.hpp"

namespace garden::graphics
{

class ComputePipelineExt;

/**
 * @brief Compute only stage container.
 * 
 * @details
 * Compute pipeline is much simpler than the graphics pipeline and is designed for general-purpose computing tasks 
 * that don't involve the fixed-function stages of the graphics pipeline. It consists of a single stage:
 * 
 * Compute Shader: Executes a compute operation, which can perform a wide range of tasks, including physics simulations, 
 * post-processing effects and any computation that doesn't require the graphics pipeline's specific stages.
 */
class ComputePipeline final : public Pipeline
{
public:
	/**
	 * @brief Compute pipeline shader code overrides.
	 * @details It allows to override pipeline shader code.
	 */
	struct ShaderOverrides final
	{
		vector<uint8> code;
		vector<uint8> headerData;
	};
	/**
	 * @brief Compute pipeline create data container.
	 * @warning In most cases you should use @ref GraphicsSystem functions.
	 */
	struct ComputeCreateData : public CreateData
	{
		uint8 _alignment0 = 0;
		uint16 _alignment1 = 0;
		uint3 localSize = uint3::zero;
		vector<uint8> code;
	};
private:
	uint16 _alignment = 0;
	uint3 localSize = uint3::zero;

	ComputePipeline(const fs::path& path, uint32 maxBindlessCount, bool useAsyncRecording, uint64 pipelineVersion) :
		Pipeline(PipelineType::Compute, path, maxBindlessCount, useAsyncRecording, pipelineVersion) { }
	ComputePipeline(ComputeCreateData& createData, bool useAsyncRecording);

	void createVkInstance(ComputeCreateData& createData);

	friend class ComputePipelineExt;
	friend class LinearPool<ComputePipeline>;
public:
	/**
	 * @brief Creates a new empty compute pipeline data container.
	 * @note Use @ref GraphicsSystem to create, destroy and access compute pipelines.
	 */
	ComputePipeline() = default;

	/**
	 * @brief Returns shader local work group size.
	 * @details It is also available in the shader: gl.workGroupSize
	 */
	u32x4 getLocalSize() const noexcept { return (u32x4)localSize; }

	//******************************************************************************************************************
	// Render commands
	//******************************************************************************************************************

	/**
	 * @brief Executes compute shader with specified 3D work group size.
	 * 
	 * @param count 3D work group size
	 * @param isGlobalCount is work group size in global space
	 * 
	 * @details
	 * Work group size determines the size and organization of work items within work groups 
	 * that execute on the GPU. This concept is essential for optimizing the performance and 
	 * efficiency of compute tasks on graphics processing units (GPUs).
	 * 
	 * gl.localInvocationIndex = gl.localInvocationID.z * gl.workGroupSize.x * gl.workGroupSize.y +
     *     gl.localInvocationID.y * gl.workGroupSize.x + gl.localInvocationID.x;
	 * gl.globalInvocationID = gl.workGroupID * gl.workGroupSize + gl.localInvocationID;
	 */
	void dispatch(u32x4 count, bool isGlobalCount = true);
	/**
	 * @brief Executes compute shader with specified 2D work group size.
	 * @details See the @ref dispatch().
	 * 
	 * @param count 2D work group size
	 * @param isGlobalCount is work group size in global space
	 */
	void dispatch(uint2 count, bool isGlobalCount = true) { dispatch(u32x4(count.x, count.y, 1), isGlobalCount); }
	/**
	 * @brief Executes compute shader with specified 1D work group size.
	 * @details See the @ref dispatch().
	 * 
	 * @param count 1D work group size
	 * @param isGlobalCount is work group size in global space
	 */
	void dispatch(uint32 count, bool isGlobalCount = true) { dispatch(u32x4(count, 1, 1), isGlobalCount); }
};

/***********************************************************************************************************************
 * @brief Compute pipeline resource extension mechanism.
 * @warning Use only if you know what you are doing!
 */
class ComputePipelineExt final
{
public:
	/**
	 * @brief Returns shader local work group size.
	 * @warning In most cases you should use @ref ComputePipeline functions.
	 * @param[in] pipeline target compute pipeline instance
	 */
	static uint3& getLocalSize(ComputePipeline& pipeline) noexcept { return pipeline.localSize; }

	/**
	 * @brief Creates a new compute pipeline data.
	 * @warning In most cases you should use @ref GraphicsSystem functions.
	 * 
	 * @param[in,out] createData target compute pipeline create data
	 * @param useAsyncRecording use multithreaded render commands recording
	 */
	static ComputePipeline create(ComputePipeline::ComputeCreateData& createData, bool useAsyncRecording)
	{
		return ComputePipeline(createData, useAsyncRecording);
	}
	/**
	 * @brief Moves internal compute pipeline objects.
	 * @warning In most cases you should use @ref GraphicsSystem functions.
	 * 
	 * @param[in,out] source source compute pipeline instance
	 * @param[in,out] destination destination compute pipeline instance
	 */
	static void moveInternalObjects(ComputePipeline& source, ComputePipeline& destination) noexcept
	{
		ComputePipelineExt::getLocalSize(destination) = ComputePipelineExt::getLocalSize(source);
		PipelineExt::moveInternalObjects(source, destination);
	}
};

} // namespace garden::graphics