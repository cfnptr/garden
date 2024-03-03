//--------------------------------------------------------------------------------------------------
// Copyright 2022-2024 Nikita Fediuchin. All rights reserved.
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
//--------------------------------------------------------------------------------------------------

#include "garden/graphics/pipeline/graphics.hpp"
#include "garden/graphics/vulkan.hpp"

using namespace std;
using namespace garden::graphics;

//--------------------------------------------------------------------------------------------------
ComputePipeline::ComputePipeline(ComputeCreateData& createData, bool useAsync) :
	Pipeline(createData, useAsync)
{
	this->localSize = createData.localSize;

	auto _code = vector<vector<uint8>>(1); _code[0] = std::move(createData.code);
	auto shaders = createShaders(_code, createData.shaderPath);

	vk::SpecializationInfo specializationInfo;
	fillSpecConsts(createData.shaderPath, ShaderStage::Compute, createData.variantCount,
		&specializationInfo, createData.specConsts, createData.specConstData);

	vk::PipelineShaderStageCreateInfo stageInfo({},
		toVkShaderStage(ShaderStage::Compute), (VkShaderModule)shaders[0], "main",
		specializationInfo.mapEntryCount > 0 ? &specializationInfo : nullptr);
	vk::ComputePipelineCreateInfo pipelineInfo({}, stageInfo,
		(VkPipelineLayout)pipelineLayout, VK_NULL_HANDLE, -1);

	if (createData.variantCount > 1)
	{
		auto variants = malloc<vk::Pipeline>(createData.variantCount);
		this->instance = variants;

		for (uint32 variantIndex = 0; variantIndex < createData.variantCount; variantIndex++)
		{
			auto result = Vulkan::device.createComputePipeline(Vulkan::pipelineCache, pipelineInfo);
			resultCheck(result.result, "vk::Device::createComputePipeline");
			variants[variantIndex] = result.value;
		}
	}
	else
	{
		auto result = Vulkan::device.createComputePipeline(Vulkan::pipelineCache, pipelineInfo);
		resultCheck(result.result, "vk::Device::createComputePipeline");
		this->instance = result.value;
	}

	destroyShaders(shaders);
}

//--------------------------------------------------------------------------------------------------
void ComputePipeline::dispatch(const int3& count, bool isGlobalCount)
{
	GARDEN_ASSERT(count > 0);
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(!Framebuffer::getCurrent());
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);

	#if GARDEN_DEBUG
	if (!Vulkan::secondaryCommandBuffers.empty())
		throw runtime_error("Current render pass is asynchronous.");
	#endif

	DispatchCommand command;
	command.groupCount = isGlobalCount ?
		(int3)ceil((float3)count / (float3)localSize) : count;
	GraphicsAPI::currentCommandBuffer->addCommand(command);
}