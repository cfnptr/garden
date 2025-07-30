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

	auto groupCount = rayGenGroupCount + missGroupCount + callGroupCount + hitGroupCount;
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
	for (uint8 i = 0; i < callGroupCount; i++)
	{
		shaderGroupInfos[groupIndex++] = vk::RayTracingShaderGroupCreateInfoKHR(
			vk::RayTracingShaderGroupTypeKHR::eGeneral, stageIndex++);
	}
	for (const auto& hitGroup : createData.hitGroups)
	{
		auto groupType = hitGroup.intersectionCode.empty() ? 
			vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup : 
			vk::RayTracingShaderGroupTypeKHR::eProceduralHitGroup;
		vk::RayTracingShaderGroupCreateInfoKHR groupInfo(groupType);

		if (!hitGroup.closestHitCode.empty())
			groupInfo.closestHitShader = stageIndex++;
		if (!hitGroup.anyHitCode.empty())
			groupInfo.anyHitShader = stageIndex++;
		if (!hitGroup.intersectionCode.empty())
			groupInfo.intersectionShader = stageIndex++;
		shaderGroupInfos[groupIndex++] = groupInfo;
	}

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
	for (const auto& callCode : createData.callGroups)
	{
		shaderStages.push_back(ShaderStage::Callable);
		codeArray.push_back(std::move(callCode));
	}
	for (const auto& hitGroup : createData.hitGroups)
	{
		if (!hitGroup.closestHitCode.empty())
		{
			shaderStages.push_back(ShaderStage::ClosestHit);
			codeArray.push_back(std::move(hitGroup.closestHitCode));
		}
		if (!hitGroup.anyHitCode.empty())
		{
			shaderStages.push_back(ShaderStage::AnyHit);
			codeArray.push_back(std::move(hitGroup.anyHitCode));
		}
		if (!hitGroup.intersectionCode.empty())
		{
			shaderStages.push_back(ShaderStage::Intersection);
			codeArray.push_back(std::move(hitGroup.intersectionCode));
		}
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

	auto vulkanAPI = VulkanAPI::get();
	GARDEN_ASSERT(createData.rayRecursionDepth > 0);
	GARDEN_ASSERT(createData.rayRecursionDepth <= vulkanAPI->rtProperties.maxRayRecursionDepth);

	vk::RayTracingPipelineCreateInfoKHR pipelineInfo({}, 
		stageCount, stageInfos.data(), groupCount, shaderGroupInfos.data(), createData.rayRecursionDepth, 
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
	this->callGroupCount = (uint32)createData.callGroups.size();
	this->hitGroupCount = (uint32)createData.hitGroups.size();
	
	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		createVkInstance(createData);
	else abort();
}

//**********************************************************************************************************************
RayTracingPipeline::SBT RayTracingPipeline::createSBT(Buffer::Usage flags)
{
	GARDEN_ASSERT_MSG(!GraphicsAPI::get()->currentFramebuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentCommandBuffer != 
		GraphicsAPI::get()->frameCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(instance, "Ray tracing pipeline [" + debugName + "] is not ready");

	SBT sbt;
	sbt.groupRegions.resize(variantCount);
	auto groupCount = rayGenGroupCount + missGroupCount + callGroupCount + hitGroupCount;	

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		auto handleSize = vulkanAPI->rtProperties.shaderGroupHandleSize;
		auto handleAlignment = vulkanAPI->rtProperties.shaderGroupHandleAlignment;
		auto baseAlignment = vulkanAPI->rtProperties.shaderGroupBaseAlignment;
		auto handleSizeAligned = alignSize(handleSize, handleAlignment);

		auto rayGenRegionSize = alignSize(rayGenGroupCount * handleSizeAligned, baseAlignment);  
		auto missRegionSize = alignSize(missGroupCount * handleSizeAligned, baseAlignment);
		auto callRegionSize = alignSize(callGroupCount * handleSizeAligned, baseAlignment);
		auto hitRegionSize = alignSize(hitGroupCount * handleSizeAligned, baseAlignment);
		auto sbtSize = (rayGenRegionSize + missRegionSize + callRegionSize + hitRegionSize) * variantCount + baseAlignment;

		constexpr auto sbtUsage = Buffer::Usage::SBT | Buffer::Usage::DeviceAddress | Buffer::Usage::TransferDst;
		sbt.buffer = vulkanAPI->bufferPool.create(sbtUsage | flags, Buffer::CpuAccess::None, 
			Buffer::Location::PreferGPU, Buffer::Strategy::Size, sbtSize, 0);
		auto stagingBuffer = vulkanAPI->bufferPool.create(Buffer::Usage::TransferSrc, 
			Buffer::CpuAccess::RandomReadWrite, Buffer::Location::Auto, Buffer::Strategy::Speed, sbtSize, 0);

		auto sbtBufferView = vulkanAPI->bufferPool.get(sbt.buffer);
		auto stagingBufferView = vulkanAPI->bufferPool.get(stagingBuffer);
		auto sbtAddress = alignSize(sbtBufferView->getDeviceAddress(), (uint64)baseAlignment);
		auto sbtOffset = sbtAddress - sbtBufferView->getDeviceAddress();
		auto stagingMap = stagingBufferView->getMap() + sbtOffset;
		vector<uint8> handles(groupCount * handleSize);
		auto handleData = handles.data();

		#if GARDEN_DEBUG || GARDEN_EDITOR
		sbtBufferView->setDebugName("buffer.sbt." + pipelinePath.generic_string());
		stagingBufferView->setDebugName("buffer.staging.sbt." + pipelinePath.generic_string());
		#endif
	
		for (uint8 i = 0; i < variantCount; i++)
		{
			auto& sbtGroupRegion = sbt.groupRegions[i];
			sbtGroupRegion.rayGenRegion.deviceAddress = sbtAddress;
			sbtGroupRegion.rayGenRegion.stride = rayGenRegionSize;
			sbtGroupRegion.rayGenRegion.size = rayGenRegionSize; // Note: Must be equal to its stride member.
			sbtAddress += rayGenRegionSize;

			sbtGroupRegion.missRegion.deviceAddress = sbtAddress;
			sbtGroupRegion.missRegion.stride = handleSizeAligned;
			sbtGroupRegion.missRegion.size = missRegionSize;
			sbtAddress += missRegionSize;

			if (callRegionSize > 0)
			{
				sbtGroupRegion.callRegion.deviceAddress = sbtAddress;
				sbtGroupRegion.callRegion.stride = handleSizeAligned;
				sbtGroupRegion.callRegion.size = callRegionSize;
				sbtAddress += callRegionSize;
			}

			sbtGroupRegion.hitRegion.deviceAddress = sbtAddress;
			sbtGroupRegion.hitRegion.stride = handleSizeAligned;
			sbtGroupRegion.hitRegion.size = hitRegionSize;
			sbtAddress += hitRegionSize;

			vk::Pipeline pipeline = variantCount > 1 ? ((VkPipeline*)instance)[i] : (VkPipeline)instance;
			auto result = vulkanAPI->device.getRayTracingShaderGroupHandlesKHR(
				pipeline, 0, groupCount, handles.size(), handles.data());
			vk::detail::resultCheck(result, "vk::Device::getRayTracingShaderGroupHandlesKHR");

			auto sbtMap = stagingMap;
			for (uint8 i = 0; i < rayGenGroupCount; i++)
			{
				memcpy(sbtMap, handleData, handleSize);
				sbtMap += rayGenRegionSize;
				handleData += handleSize;
			}
			stagingMap += rayGenRegionSize;

			sbtMap = stagingMap;
			for (uint8 i = 0; i < missGroupCount; i++)
			{
				memcpy(sbtMap, handleData, handleSize);
				sbtMap += handleSizeAligned;
				handleData += handleSize;
			}
			stagingMap += missRegionSize;

			sbtMap = stagingMap;
			for (uint8 i = 0; i < callGroupCount; i++)
			{
				memcpy(sbtMap, handleData, handleSize);
				sbtMap += handleSizeAligned;
				handleData += handleSize;
			}
			stagingMap += callRegionSize;

			sbtMap = stagingMap;
			for (uint8 i = 0; i < hitGroupCount; i++)
			{
				memcpy(sbtMap, handleData, handleSize);
				sbtMap += handleSizeAligned;
				handleData += handleSize;
			}
			stagingMap += hitRegionSize;
		}

		stagingBufferView->flush();

		#if GARDEN_DEBUG // Hack: skips queue ownership asserts.
		BufferExt::getUsage(**stagingBufferView) |= Buffer::Usage::TransferQ | Buffer::Usage::ComputeQ;
		#endif

		Buffer::copy(stagingBuffer, sbt.buffer);
		vulkanAPI->bufferPool.destroy(stagingBuffer);
	}
	else abort();

	return sbt;
}

//**********************************************************************************************************************
void RayTracingPipeline::traceRays(const SBT& sbt, uint3 count)
{
	GARDEN_ASSERT_MSG(sbt.buffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(!sbt.groupRegions.empty(), "Assert " + debugName);
	GARDEN_ASSERT_MSG(areAllTrue(count > uint3::zero), "Assert " + debugName);
	GARDEN_ASSERT_MSG(!GraphicsAPI::get()->currentFramebuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(instance, "Ray tracing pipeline [" + debugName + "] is not ready");

	auto graphicsAPI = GraphicsAPI::get();
	#if GARDEN_DEBUG
	auto bufferView = graphicsAPI->bufferPool.get(sbt.buffer);
	if (graphicsAPI->currentCommandBuffer == graphicsAPI->transferCommandBuffer)
	{
		GARDEN_ASSERT_MSG(hasAnyFlag(bufferView->getUsage(), Buffer::Usage::TransferQ),
			"SBT buffer [" + bufferView->getDebugName() + "] does not have transfer queue flag");
	}
	if (graphicsAPI->currentCommandBuffer == graphicsAPI->computeCommandBuffer)
	{
		GARDEN_ASSERT_MSG(hasAnyFlag(bufferView->getUsage(), Buffer::Usage::ComputeQ),
			"SBT buffer [" + bufferView->getDebugName() + "] does not have compute queue flag");
	}
	#endif

	auto currentVariant = graphicsAPI->currentPipelineVariants[0];

	TraceRaysCommand command;
	command.groupCount = count;
	command.sbtRegions = sbt.groupRegions[currentVariant];
	command.sbtBuffer = sbt.buffer;
	graphicsAPI->currentCommandBuffer->addCommand(command);
}