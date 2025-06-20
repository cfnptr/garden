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

#include "garden/graphics/pipeline/compute.hpp"
#include "garden/graphics/vulkan/api.hpp"

using namespace garden::graphics;

//**********************************************************************************************************************
void ComputePipeline::createVkInstance(ComputeCreateData& createData)
{
	if (variantCount > 1)
		this->instance = malloc<vk::Pipeline>(createData.variantCount);

	auto vulkanAPI = VulkanAPI::get();
	auto shaderCode = std::move(createData.code);
	auto shaders = createShaders(&shaderCode, 1, createData.shaderPath);

	vk::SpecializationInfo specializationInfo;
	fillVkSpecConsts(createData.shaderPath, &specializationInfo, createData.specConsts,
		createData.specConstValues, ShaderStage::Compute, variantCount);

	vk::PipelineShaderStageCreateInfo stageInfo({}, toVkShaderStage(ShaderStage::Compute), 
		(VkShaderModule)shaders[0], "main", specializationInfo.mapEntryCount > 0 ? &specializationInfo : nullptr);
	vk::ComputePipelineCreateInfo pipelineInfo({}, stageInfo, (VkPipelineLayout)pipelineLayout, nullptr, -1);

	for (uint8 i = 0; i < variantCount; i++)
	{
		if (variantCount > 1)
			setVkVariantIndex(&specializationInfo, i);

		auto result = vulkanAPI->device.createComputePipeline(vulkanAPI->pipelineCache, pipelineInfo);
		vk::detail::resultCheck(result.result, "vk::Device::createComputePipeline");

		if (createData.variantCount > 1)
			((void**)this->instance)[i] = result.value;
		else
			this->instance = result.value;
	}

	destroyShaders(shaders);
}

//**********************************************************************************************************************
ComputePipeline::ComputePipeline(ComputeCreateData& createData, bool asyncRecording) :
	Pipeline(createData, asyncRecording), localSize(createData.localSize)
{
	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		createVkInstance(createData);
	else abort();
}

//**********************************************************************************************************************
void ComputePipeline::dispatch(uint3 count, bool isGlobalCount)
{
	GARDEN_ASSERT_MSG(areAllTrue(count > uint3::zero), "Assert " + debugName);
	GARDEN_ASSERT_MSG(!GraphicsAPI::get()->currentFramebuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(instance, "Compute pipeline [" + debugName + "] is not ready");

	DispatchCommand command;
	command.groupCount = isGlobalCount ?  (uint3)ceil((float3)count / localSize) : count;
	GraphicsAPI::get()->currentCommandBuffer->addCommand(command);
}