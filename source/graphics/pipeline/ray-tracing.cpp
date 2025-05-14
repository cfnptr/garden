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

#include "garden/graphics/pipeline/ray-tracing.hpp"
#include "garden/graphics/vulkan/api.hpp"

using namespace garden::graphics;

//**********************************************************************************************************************
void RayTracingPipeline::createVkInstance(RayTracingCreateData& createData)
{
	auto vulkanAPI = VulkanAPI::get();
	auto shaderCode = std::move(createData.rayGenerationCode);
	auto shaders = createShaders(&shaderCode, 1, createData.shaderPath); //TODO: with maintenance5 we can omit shader modules creation.

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
RayTracingPipeline::RayTracingPipeline(RayTracingCreateData& createData, 
	bool asyncRecording) : Pipeline(createData, asyncRecording)
{
	if (createData.variantCount > 1)
		this->instance = malloc<vk::Pipeline>(createData.variantCount);

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		createVkInstance(createData);
	else abort();
}

//**********************************************************************************************************************
