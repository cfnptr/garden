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

using namespace garden;
using namespace garden::graphics;

//**********************************************************************************************************************
void RayTracingPipeline::createVkInstance(RayTracingCreateData& createData)
{
	if (variantCount > 1)
		this->instance = malloc<vk::Pipeline>(variantCount);

	auto groupCount = rayGenGroupCount + missGroupCount + hitGroupCount + callGroupCount;
	vector<ShaderStage> shaderStages; vector<vector<uint8>> codeArray;

	for (const auto& rayGenCode : createData.rayGenGroups)
	{
		shaderStages.push_back(ShaderStage::RayGeneration);
		codeArray.push_back(std::move(rayGenCode));
	}
	for (const auto& missCode : createData.missGroups)
	{
		shaderStages.push_back(ShaderStage::Miss);
		codeArray.push_back(std::move(missCode));
	}
	for (const auto& hitGroup : createData.hitGroups)
	{
		if (!hitGroup.intersectionCode.empty())
		{
			shaderStages.push_back(ShaderStage::Intersection);
			codeArray.push_back(std::move(hitGroup.intersectionCode));
		}
		if (!hitGroup.anyHitCode.empty())
		{
			shaderStages.push_back(ShaderStage::AnyHit);
			codeArray.push_back(std::move(hitGroup.anyHitCode));
		}
		if (!hitGroup.closestHitCode.empty())
		{
			shaderStages.push_back(ShaderStage::ClosestHit);
			codeArray.push_back(std::move(hitGroup.closestHitCode));
		}
	}
	for (const auto& callCode : createData.callGroups)
	{
		shaderStages.push_back(ShaderStage::Callable);
		codeArray.push_back(std::move(callCode));
	}

	auto stageCount = (uint8)shaderStages.size();
	auto shaders = createShaders(codeArray.data(), stageCount, createData.shaderPath);
	vector<vk::PipelineShaderStageCreateInfo> stageInfos(stageCount);
	vector<vk::SpecializationInfo> specializationInfos(stageCount);
	
	for (uint8 i = 0; i < stageCount; i++)
	{
		auto shaderStage = shaderStages[i]; auto specializationInfo = &specializationInfos[i];
		fillVkSpecConsts(createData.shaderPath, specializationInfo, 
			createData.specConsts, createData.specConstValues, shaderStage, variantCount);
		vk::PipelineShaderStageCreateInfo stageInfo({}, toVkShaderStage(shaderStage), (VkShaderModule)shaders[i], 
			"main", specializationInfo->mapEntryCount > 0 ? specializationInfo : nullptr);
		stageInfos[i] = stageInfo;
	}

	vector<vk::RayTracingShaderGroupCreateInfoKHR> shaderGroupInfos(groupCount);
	uint32 groupIndex = 0, stageIndex = 0;

	for (uint8 i = 0; i < rayGenGroupCount; i++)
	{
		shaderGroupInfos[groupIndex++] = vk::RayTracingShaderGroupCreateInfoKHR(
			vk::RayTracingShaderGroupTypeKHR::eGeneral, stageIndex++);
	}
	for (uint8 i = 0; i < missGroupCount; i++)
	{
		shaderGroupInfos[groupIndex++] = vk::RayTracingShaderGroupCreateInfoKHR(
			vk::RayTracingShaderGroupTypeKHR::eGeneral, stageIndex++);
	}
	for (const auto& hitGroup : createData.hitGroups)
	{
		auto groupType = hitGroup.hasIntersectShader ?
			vk::RayTracingShaderGroupTypeKHR::eProceduralHitGroup :
			vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup;
		vk::RayTracingShaderGroupCreateInfoKHR groupInfo(groupType);

		if (hitGroup.hasIntersectShader)
			groupInfo.intersectionShader = stageIndex++;
		if (hitGroup.hasAnyHitShader)
			groupInfo.anyHitShader = stageIndex++;
		if (hitGroup.hasClosHitShader)
			groupInfo.closestHitShader = stageIndex++;
		shaderGroupInfos[groupIndex++] = groupInfo;
	}
	for (uint8 i = 0; i < callGroupCount; i++)
	{
		shaderGroupInfos[groupIndex++] = vk::RayTracingShaderGroupCreateInfoKHR(
			vk::RayTracingShaderGroupTypeKHR::eGeneral, stageIndex++);
	}

	auto vulkanAPI = VulkanAPI::get();
	vk::RayTracingPipelineCreateInfoKHR pipelineInfo({}, 
		stageCount, stageInfos.data(), groupCount, shaderGroupInfos.data(), createData.maxRecursionDepth, 
		nullptr, nullptr, nullptr, (VkPipelineLayout)pipelineLayout, nullptr, -1);

	for (uint8 i = 0; i < variantCount; i++)
	{
		if (variantCount > 1)
		{
			for (auto& specializationInfo : specializationInfos)
				setVkVariantIndex(&specializationInfo, i);
		}

		auto result = vulkanAPI->device.createRayTracingPipelineKHR(nullptr, vulkanAPI->pipelineCache, pipelineInfo);
		vk::detail::resultCheck(result.result, "vk::Device::createRayTracingPipelineKHR");

		if (variantCount > 1)
			((void**)this->instance)[i] = result.value;
		else
			this->instance = result.value;
	}

	destroyShaders(shaders);
}

//**********************************************************************************************************************
RayTracingPipeline::RayTracingPipeline(RayTracingCreateData& createData, 
	bool asyncRecording) : Pipeline(createData, asyncRecording)
{
	this->rayGenGroupCount = (uint32)createData.rayGenGroups.size();
	this->missGroupCount = (uint32)createData.missGroups.size();
	this->hitGroupCount = (uint32)createData.hitGroups.size();
	this->callGroupCount = (uint32)createData.callGroups.size();
	
	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		createVkInstance(createData);
	else abort();
}

//**********************************************************************************************************************
void RayTracingPipeline::createSBT(ID<Buffer>& sbtBuffer, vector<SbtGroupRegions>& sbtGroupRegions)
{
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(!GraphicsAPI::get()->currentFramebuffer);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer != GraphicsAPI::get()->frameCommandBuffer);

	auto groupCount = rayGenGroupCount + missGroupCount + hitGroupCount + callGroupCount;	
	sbtGroupRegions.resize(variantCount);

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		auto handleSize = vulkanAPI->rtProperties.shaderGroupHandleSize;
		auto handleAlignment = vulkanAPI->rtProperties.shaderGroupHandleAlignment;
		auto baseAlignment = vulkanAPI->rtProperties.shaderGroupBaseAlignment;
		auto handleSizeAligned = alignSize(handleSize, handleAlignment);

		auto rayGenRegionSize = alignSize(rayGenGroupCount * handleSizeAligned, baseAlignment);  
		auto missRegionSize = alignSize(missGroupCount * handleSizeAligned, baseAlignment);
		auto hitRegionSize = alignSize(hitGroupCount * handleSizeAligned, baseAlignment);
		auto callRegionSize = alignSize(callGroupCount * handleSizeAligned, baseAlignment);
		auto sbtSize = (rayGenRegionSize + missRegionSize + hitRegionSize + callRegionSize) * variantCount + baseAlignment;

		sbtBuffer = vulkanAPI->bufferPool.create(Buffer::Usage::TransferDst | 
			Buffer::Usage::SBT | Buffer::Usage::DeviceAddress, Buffer::CpuAccess::None, 
			Buffer::Location::PreferGPU, Buffer::Strategy::Size, sbtSize, 0);
		auto sbtBufferView = vulkanAPI->bufferPool.get(sbtBuffer);
		auto sbtAddress = alignSize(sbtBufferView->getDeviceAddress(), (uint64)baseAlignment);
		auto stbOffset = sbtAddress - sbtBufferView->getDeviceAddress();

		auto stagingBuffer = vulkanAPI->bufferPool.create(Buffer::Usage::TransferSrc, 
			Buffer::CpuAccess::RandomReadWrite, Buffer::Location::Auto, Buffer::Strategy::Speed, sbtSize, 0);
		auto stagingBufferView = vulkanAPI->bufferPool.get(stagingBuffer);
		auto stagingMap = stagingBufferView->getMap() + stbOffset;
		vector<uint8> handles(groupCount * handleSize);
		auto handleData = handles.data();
	
		for (uint8 i = 0; i < variantCount; i++)
		{
			auto& sbtGroupRegion = sbtGroupRegions[i];
			sbtGroupRegion.rayGenRegion.deviceAddress = sbtAddress;
			sbtGroupRegion.rayGenRegion.stride = rayGenRegionSize;
			sbtGroupRegion.rayGenRegion.size = rayGenRegionSize; // Note: must be equal to its stride member.
			sbtAddress += rayGenRegionSize;

			sbtGroupRegion.missRegion.deviceAddress = sbtAddress;
			sbtGroupRegion.missRegion.size = missRegionSize;
			sbtGroupRegion.missRegion.stride = handleSizeAligned;
			sbtAddress += missRegionSize;

			sbtGroupRegion.hitRegion.deviceAddress = sbtAddress;
			sbtGroupRegion.hitRegion.size = hitRegionSize;
			sbtGroupRegion.hitRegion.stride = handleSizeAligned;
			sbtAddress += hitRegionSize;

			if (callRegionSize > 0)
			{
				sbtGroupRegion.callRegion.deviceAddress = sbtAddress;
				sbtGroupRegion.callRegion.size = callRegionSize;
				sbtGroupRegion.callRegion.stride = handleSizeAligned;
				sbtAddress += callRegionSize;
			}

			vk::Pipeline pipeline = variantCount > 1 ? ((VkPipeline*)instance)[i] : (VkPipeline)instance;
			auto result = vulkanAPI->device.getRayTracingShaderGroupHandlesKHR(
				pipeline, 0, groupCount, handles.size(), handles.data());
			vk::detail::resultCheck(result, "vk::Device::getRayTracingShaderGroupHandlesKHR");

			uint32 handleIndex = 0; auto sbtMap = stagingMap;
			for (uint8 i = 0; i < rayGenGroupCount; i++)
			{
				memcpy(sbtMap, handleData + (handleIndex * handleSize), handleSize);
				sbtMap += handleSizeAligned; handleIndex++;
			}

			sbtMap = stagingMap + rayGenRegionSize;
			for (uint8 i = 0; i < missGroupCount; i++)
			{
				memcpy(sbtMap, handleData + (handleIndex * handleSize), handleSize);
				sbtMap += handleSizeAligned; handleIndex++;
			}

			sbtMap = stagingMap + rayGenRegionSize + missRegionSize;
			for (uint8 i = 0; i < hitGroupCount; i++)
			{
				memcpy(sbtMap, handleData + (handleIndex * handleSize), handleSize);
				sbtMap += handleSizeAligned; handleIndex++;
			}

			sbtMap = stagingMap + rayGenRegionSize + missRegionSize + hitRegionSize;
			for (uint8 i = 0; i < callGroupCount; i++)
			{
				memcpy(sbtMap, handleData + (handleIndex * handleSize), handleSize);
				sbtMap += handleSizeAligned; handleIndex++;
			}
		}

		stagingBufferView->flush();
		Buffer::copy(stagingBuffer, sbtBuffer);
		vulkanAPI->bufferPool.destroy(stagingBuffer);
	}
	else abort();
}

//**********************************************************************************************************************
void RayTracingPipeline::traceRays(const vector<SbtGroupRegions>& sbtGroupRegions, uint3 count)
{
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(!sbtGroupRegions.empty());
	GARDEN_ASSERT(areAllTrue(count > uint3::zero));
	GARDEN_ASSERT(!GraphicsAPI::get()->currentFramebuffer);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);

	auto graphicsAPI = GraphicsAPI::get();
	auto currentVariant = graphicsAPI->currentPipelineVariants[0];

	TraceRaysCommand command;
	command.groupCount = count;
	command.sbt = sbtGroupRegions[currentVariant];
	graphicsAPI->currentCommandBuffer->addCommand(command);
}