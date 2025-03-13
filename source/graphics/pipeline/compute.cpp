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

#include "garden/graphics/pipeline/graphics.hpp"
#include "garden/graphics/vulkan/api.hpp"

using namespace garden::graphics;

//**********************************************************************************************************************
void ComputePipeline::createVkInstance(ComputeCreateData& createData)
{
	auto vulkanAPI = VulkanAPI::get();
	auto _code = vector<vector<uint8>>(1); _code[0] = std::move(createData.code);
	auto shaders = createShaders(_code, createData.shaderPath);

	vk::SpecializationInfo specializationInfo;
	fillVkSpecConsts(createData.shaderPath, &specializationInfo, createData.specConsts,
		createData.specConstValues, ShaderStage::Compute, createData.variantCount);

	vk::PipelineShaderStageCreateInfo stageInfo({},
		toVkShaderStage(ShaderStage::Compute), (VkShaderModule)shaders[0], "main",
		specializationInfo.mapEntryCount > 0 ? &specializationInfo : nullptr);
	vk::ComputePipelineCreateInfo pipelineInfo({},
		stageInfo, (VkPipelineLayout)pipelineLayout, {}, -1);

	for (uint32 variantIndex = 0; variantIndex < createData.variantCount; variantIndex++)
	{
		if (variantCount > 1)
			setVkVariantIndex(&specializationInfo, variantIndex);

		auto result = vulkanAPI->device.createComputePipeline(vulkanAPI->pipelineCache, pipelineInfo);
		vk::detail::resultCheck(result.result, "vk::Device::createComputePipeline");

		if (createData.variantCount > 1)
			((void**)this->instance)[variantIndex] = result.value;
		else
			this->instance = result.value;
	}

	destroyShaders(shaders);
}

//**********************************************************************************************************************
ComputePipeline::ComputePipeline(ComputeCreateData& createData, bool asyncRecording) :
	Pipeline(createData, asyncRecording), localSize(createData.localSize)
{
	if (createData.variantCount > 1)
		this->instance = malloc<vk::Pipeline>(createData.variantCount);

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		createVkInstance(createData);
	else abort();
}

//**********************************************************************************************************************
void ComputePipeline::dispatch(u32x4 count, bool isGlobalCount)
{
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(areAllTrue(count > u32x4::zero));
	GARDEN_ASSERT(!GraphicsAPI::get()->currentFramebuffer);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);
	GARDEN_ASSERT(!GraphicsAPI::get()->isCurrentRenderPassAsync);

	DispatchCommand command;
	command.groupCount = (uint3)(isGlobalCount ? (u32x4)ceil((f32x4)count / (f32x4)localSize) : count);
	GraphicsAPI::get()->currentCommandBuffer->addCommand(command);
}