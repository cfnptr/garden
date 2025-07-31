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

#include "garden/graphics/pipeline.hpp"
#include "garden/graphics/vulkan/api.hpp"

using namespace garden;
using namespace garden::graphics;

//**********************************************************************************************************************
static vector<void*> createVkPipelineSamplers(const Pipeline::Uniforms& uniforms, 
	const Pipeline::SamplerStates& samplerStates, tsl::robin_map<string, vk::Sampler>& immutableSamplers, 
	const fs::path& pipelinePath, const Pipeline::SamplerStates& samplerStateOverrides)
{
	auto vulkanAPI = VulkanAPI::get();
	vector<void*> samplers(samplerStates.size());
	uint32 i = 0;

	for (auto it = samplerStates.begin(); it != samplerStates.end(); it++, i++)
	{
		auto& uniform = uniforms.at(it->first);
		if (uniform.isMutable)
		{
			GARDEN_ASSERT_MSG(samplerStateOverrides.find(it->first) == samplerStateOverrides.end(), 
				"Can not override 'mutable' shader uniform [" + it->first + "] sampler state");
			continue;
		}

		auto samplerStateSearch = samplerStateOverrides.find(it->first);
		auto state = samplerStateSearch == samplerStateOverrides.end() ?
			it->second : samplerStateSearch->second;
		auto samplerInfo = getVkSamplerCreateInfo(state);
		auto sampler = vulkanAPI->device.createSampler(samplerInfo);
		samplers[i] = (VkSampler)sampler;
		immutableSamplers.emplace(it->first, sampler);

		#if GARDEN_DEBUG // Note: No GARDEN_EDITOR
		if (vulkanAPI->hasDebugUtils)
		{
			auto name = "sampler." + pipelinePath.generic_string() + "." + it->first;
			vk::DebugUtilsObjectNameInfoEXT nameInfo(
				vk::ObjectType::eSampler, (uint64)(VkSampler)sampler, name.c_str());
			vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo);
		}
		#endif
	}
	
	return samplers;
}

//**********************************************************************************************************************
static void createVkDescriptorSetLayouts(vector<void*>& descriptorSetLayouts, vector<void*>& descriptorPools,
	const Pipeline::Uniforms& uniforms, const tsl::robin_map<string, vk::Sampler>& immutableSamplers,
	const fs::path& pipelinePath, uint32 maxBindlessCount)
{
	auto vulkanAPI = VulkanAPI::get();
	vector<vk::DescriptorSetLayoutBinding> descriptorSetBindings;
	vector<vk::DescriptorBindingFlags> descriptorBindingFlags;
	vector<vector<vk::Sampler>> samplerArrays;
	
	for (uint8 dsIndex = 0; dsIndex < (uint8)descriptorSetLayouts.size(); dsIndex++)
	{
		vector<vk::DescriptorPoolSize> descriptorPoolSizes;
		uint32 bindingIndex = 0; auto isBindless = false;

		if (descriptorSetBindings.size() < uniforms.size())
			descriptorSetBindings.resize(uniforms.size());

		for	(const auto& pair : uniforms)
		{
			auto uniform = pair.second;
			if (uniform.descriptorSetIndex != dsIndex)
				continue;

			auto& descriptorSetBinding = descriptorSetBindings[bindingIndex];
			descriptorSetBinding.binding = (uint32)uniform.bindingIndex;
			descriptorSetBinding.descriptorType = toVkDescriptorType(uniform.type);
			descriptorSetBinding.stageFlags = toVkShaderStages(uniform.shaderStages);

			if (uniform.arraySize > 0)
			{
				if (!uniform.isMutable && isSamplerType(uniform.type))
				{
					if (uniform.arraySize > 1)
					{
						// TODO: allow to specify sampler states for separate uniform array elements?
						vector<vk::Sampler> samplers(uniform.arraySize, *&immutableSamplers.at(pair.first));
						samplerArrays.push_back(std::move(samplers));
						descriptorSetBinding.pImmutableSamplers = samplerArrays[samplerArrays.size() - 1].data();
					}
					else
					{
						descriptorSetBinding.pImmutableSamplers = &immutableSamplers.at(pair.first);
					}
				}
				descriptorSetBinding.descriptorCount = uniform.arraySize;
			}
			else
			{
				GARDEN_ASSERT_MSG(maxBindlessCount > 0, "Can not use bindless uniforms inside non bindless pipeline");

				if (descriptorBindingFlags.size() < uniforms.size())
					descriptorBindingFlags.resize(uniforms.size());
				if (descriptorPoolSizes.empty())
				{
					descriptorPoolSizes =
					{
						vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 0),
						vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, 0),
						vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 0),
						vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 0),
						vk::DescriptorPoolSize(vk::DescriptorType::eAccelerationStructureKHR, 0),
					};
				}

				switch (descriptorSetBinding.descriptorType)
				{
				case vk::DescriptorType::eCombinedImageSampler:
					descriptorPoolSizes[0].descriptorCount += maxBindlessCount; break;
				case vk::DescriptorType::eStorageImage:
					descriptorPoolSizes[1].descriptorCount += maxBindlessCount; break;
				case vk::DescriptorType::eUniformBuffer:
					descriptorPoolSizes[2].descriptorCount += maxBindlessCount; break;
				case vk::DescriptorType::eStorageBuffer:
					descriptorPoolSizes[3].descriptorCount += maxBindlessCount; break;
				case vk::DescriptorType::eAccelerationStructureKHR:
					descriptorPoolSizes[4].descriptorCount += maxBindlessCount; break;
				default: abort();
				}

				if (!uniform.isMutable && isSamplerType(uniform.type))
				{
					vector<vk::Sampler> samplers(maxBindlessCount, *&immutableSamplers.at(pair.first));
					samplerArrays.push_back(std::move(samplers));
					descriptorSetBinding.pImmutableSamplers = samplerArrays[samplerArrays.size() - 1].data();
				}

				descriptorBindingFlags[bindingIndex] = vk::DescriptorBindingFlagBits::eUpdateAfterBind | 
					vk::DescriptorBindingFlagBits::ePartiallyBound;
				descriptorSetBinding.descriptorCount = maxBindlessCount;
				isBindless = true;
			}

			bindingIndex++;
		}

		vk::DescriptorSetLayoutCreateInfo descriptorSetLayoutInfo({}, bindingIndex, descriptorSetBindings.data());
		vk::DescriptorSetLayoutBindingFlagsCreateInfo descriptorSetFlagsInfo;

		if (isBindless)
		{
			descriptorSetFlagsInfo.bindingCount = bindingIndex;
			descriptorSetFlagsInfo.pBindingFlags = descriptorBindingFlags.data();
			descriptorSetLayoutInfo.flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
			descriptorSetLayoutInfo.pNext = &descriptorSetFlagsInfo;
	
			uint32 maxSetCount = 0; 
			for (auto i = descriptorPoolSizes.begin(); i != descriptorPoolSizes.end();)
			{
				// TODO: reverse delete or back-swap would be faster.
				if (i->descriptorCount > 0)
				{
					maxSetCount += i->descriptorCount;
					i++;
				}
				else
				{
					i = descriptorPoolSizes.erase(i);
				}
			}
			GARDEN_ASSERT(maxSetCount > 0);

			vk::DescriptorPoolCreateInfo descriptorPoolInfo(vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind, 
				maxSetCount, (uint32)descriptorPoolSizes.size(), descriptorPoolSizes.data());
			descriptorPools[dsIndex] = vulkanAPI->device.createDescriptorPool(descriptorPoolInfo);

			#if GARDEN_DEBUG // Note: No GARDEN_EDITOR
			if (vulkanAPI->hasDebugUtils)
			{
				auto name = "descriptorPool." + pipelinePath.generic_string() + to_string(dsIndex);
				vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eDescriptorPool,
					(uint64)(VkSampler)descriptorPools[dsIndex], name.c_str());
				vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo);
			}
			#endif
		}

		descriptorSetLayouts[dsIndex] = vulkanAPI->device.createDescriptorSetLayout(descriptorSetLayoutInfo);
		samplerArrays.clear();

		#if GARDEN_DEBUG // Note: No GARDEN_EDITOR
		if (vulkanAPI->hasDebugUtils)
		{
			auto name = "descriptorSetLayout." + pipelinePath.generic_string() + to_string(dsIndex);
			vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eDescriptorSetLayout,
				(uint64)descriptorSetLayouts[dsIndex], name.c_str());
			vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo);
		}
		#endif
	}
}

//**********************************************************************************************************************
static vk::PipelineLayout createVkPipelineLayout(uint16 pushConstantsSize, ShaderStage pushConstantsStages,
	const vector<void*>& descriptorSetLayouts, const fs::path& pipelinePath)
{
	vector<vk::PushConstantRange> pushConstantRanges;

	if (hasAnyFlag(pushConstantsStages, ShaderStage::Vertex))
		pushConstantRanges.emplace_back(vk::ShaderStageFlagBits::eVertex, 0, pushConstantsSize);
	if (hasAnyFlag(pushConstantsStages, ShaderStage::Fragment))
		pushConstantRanges.emplace_back(vk::ShaderStageFlagBits::eFragment, 0, pushConstantsSize);
	if (hasAnyFlag(pushConstantsStages, ShaderStage::Compute))
		pushConstantRanges.emplace_back(vk::ShaderStageFlagBits::eCompute, 0, pushConstantsSize);
	if (hasAnyFlag(pushConstantsStages, ShaderStage::RayGeneration))
		pushConstantRanges.emplace_back(vk::ShaderStageFlagBits::eRaygenKHR, 0, pushConstantsSize);
	if (hasAnyFlag(pushConstantsStages, ShaderStage::Intersection))
		pushConstantRanges.emplace_back(vk::ShaderStageFlagBits::eIntersectionKHR, 0, pushConstantsSize);
	if (hasAnyFlag(pushConstantsStages, ShaderStage::AnyHit))
		pushConstantRanges.emplace_back(vk::ShaderStageFlagBits::eAnyHitKHR, 0, pushConstantsSize);
	if (hasAnyFlag(pushConstantsStages, ShaderStage::ClosestHit))
		pushConstantRanges.emplace_back(vk::ShaderStageFlagBits::eClosestHitKHR, 0, pushConstantsSize);
	if (hasAnyFlag(pushConstantsStages, ShaderStage::Miss))
		pushConstantRanges.emplace_back(vk::ShaderStageFlagBits::eMissKHR, 0, pushConstantsSize);
	if (hasAnyFlag(pushConstantsStages, ShaderStage::Callable))
		pushConstantRanges.emplace_back(vk::ShaderStageFlagBits::eCallableKHR, 0, pushConstantsSize);
	if (hasAnyFlag(pushConstantsStages, ShaderStage::Mesh))
		pushConstantRanges.emplace_back(vk::ShaderStageFlagBits::eMeshEXT, 0, pushConstantsSize);
	if (hasAnyFlag(pushConstantsStages, ShaderStage::Task))
		pushConstantRanges.emplace_back(vk::ShaderStageFlagBits::eTaskEXT, 0, pushConstantsSize);

	vk::PipelineLayoutCreateInfo pipelineLayoutInfo({}, 0, nullptr,
		(uint32)pushConstantRanges.size(), pushConstantRanges.data());

	if (!descriptorSetLayouts.empty())
	{
		pipelineLayoutInfo.setLayoutCount = (uint32)descriptorSetLayouts.size();
		pipelineLayoutInfo.pSetLayouts = (const vk::DescriptorSetLayout*)descriptorSetLayouts.data();
	}

	auto vulkanAPI = VulkanAPI::get();
	auto layout = vulkanAPI->device.createPipelineLayout(pipelineLayoutInfo);

	#if GARDEN_DEBUG // Note: No GARDEN_EDITOR
	if (vulkanAPI->hasDebugUtils)
	{
		auto name = "pipelineLayout." + pipelinePath.generic_string();
		vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::ePipelineLayout,
			(uint64)(VkPipelineLayout)layout, name.c_str());
		vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo);
	}
	#endif

	return layout;
}

//**********************************************************************************************************************
static void destroyVkPipeline(void* instance, void* pipelineLayout, const vector<void*>& samplers,
	const vector<void*>& descriptorSetLayouts, const vector<void*>& descriptorPools, uint8 variantCount)
{
	auto vulkanAPI = VulkanAPI::get();
	if (vulkanAPI->forceResourceDestroy)
	{
		if (variantCount > 1)
		{
			for (uint8 i = 0; i < variantCount; i++)
				vulkanAPI->device.destroyPipeline(((VkPipeline*)instance)[i]);
			free(instance);
		}
		else
		{
			vulkanAPI->device.destroyPipeline((VkPipeline)instance);
		}

		vulkanAPI->device.destroyPipelineLayout((VkPipelineLayout)pipelineLayout);

		for (auto descriptorSetLayout : descriptorSetLayouts)
		{
			vulkanAPI->device.destroyDescriptorSetLayout(vk::DescriptorSetLayout(
				(VkDescriptorSetLayout)descriptorSetLayout));
		}
		for (auto descriptorPool : descriptorPools)
		{
			if (!descriptorPool)
				continue;
			vulkanAPI->device.destroyDescriptorPool(vk::DescriptorPool(
				(VkDescriptorPool)descriptorPool));
		}

		for (auto sampler : samplers)
			vulkanAPI->device.destroySampler((VkSampler)sampler);
	}
	else
	{
		vulkanAPI->destroyResource(GraphicsAPI::DestroyResourceType::Pipeline,
			instance, pipelineLayout, variantCount - 1);

		for (auto descriptorSetLayout : descriptorSetLayouts)
			vulkanAPI->destroyResource(GraphicsAPI::DestroyResourceType::DescriptorSetLayout, descriptorSetLayout);

		for (auto descriptorPool : descriptorPools)
		{
			if (!descriptorPool)
				continue;
			vulkanAPI->destroyResource(GraphicsAPI::DestroyResourceType::DescriptorPool, descriptorPool);
		}

		for (auto sampler : samplers)
			vulkanAPI->destroyResource(GraphicsAPI::DestroyResourceType::Sampler, sampler);
	}
}

//**********************************************************************************************************************
static vector<void*> createVkShaders(const vector<uint8>* codeArray, uint8 shaderCount, const fs::path& pipelinePath)
{
	auto vulkanAPI = VulkanAPI::get();
	vector<void*> shaders(shaderCount);

	for (uint8 i = 0; i < shaderCount; i++)
	{
		const auto& shaderCode = codeArray[i];
		vk::ShaderModuleCreateInfo shaderInfo({},
			(uint32)shaderCode.size(), (const uint32*)shaderCode.data());
		shaders[i] = (VkShaderModule)vulkanAPI->device.createShaderModule(shaderInfo);

		#if GARDEN_DEBUG // Note: No GARDEN_EDITOR
		if (vulkanAPI->hasDebugUtils)
		{
			auto _name = "shaderModule." + pipelinePath.generic_string() + to_string(i);
			vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eShaderModule, (uint64)shaders[i], _name.c_str());
			vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo);
		}
		#endif
	}

	return shaders;
}

//**********************************************************************************************************************
Pipeline::Pipeline(CreateData& createData, bool asyncRecording)
{
	this->uniforms = std::move(createData.uniforms);
	this->pipelineVersion = createData.pipelineVersion;
	this->pushConstantsSize = createData.pushConstantsSize;
	this->variantCount = createData.variantCount;

	if (createData.descriptorSetCount > 0)
	{
		descriptorSetLayouts.resize(createData.descriptorSetCount);
		descriptorPools.resize(createData.descriptorSetCount);
	}

	auto graphicsAPI = GraphicsAPI::get();

	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		if (createData.maxBindlessCount > 0 && !VulkanAPI::get()->features.descriptorIndexing)
		{
			throw GardenError("Bindless descriptors are not supported on this GPU. ("
				"pipeline: )" + createData.shaderPath.generic_string() + ")");
		}

		this->pushConstantsMask = (uint32)toVkShaderStages(createData.pushConstantsStages);

		tsl::robin_map<string, vk::Sampler> immutableSamplers;
		this->samplers = createVkPipelineSamplers(uniforms, createData.samplerStates,
			immutableSamplers, createData.shaderPath, createData.samplerStateOverrides);

		createVkDescriptorSetLayouts(descriptorSetLayouts, descriptorPools, uniforms,
			immutableSamplers, createData.shaderPath, createData.maxBindlessCount);
		this->pipelineLayout = createVkPipelineLayout(pushConstantsSize,
			createData.pushConstantsStages, descriptorSetLayouts, createData.shaderPath);
	}
	else abort();
}

bool Pipeline::destroy()
{
	if (!instance || busyLock > 0)
		return false;

	#if GARDEN_DEBUG
	auto graphicsAPI = GraphicsAPI::get();
	if (!graphicsAPI->forceResourceDestroy)
	{
		auto pipelineInstance = graphicsAPI->getPipeline(type, this);
		for (auto& descriptorSet : graphicsAPI->descriptorSetPool)
		{
			if (!ResourceExt::getInstance(descriptorSet) || descriptorSet.getPipelineType() != type)
				continue;
			GARDEN_ASSERT_MSG(pipelineInstance != descriptorSet.getPipeline(), 
				"Descriptor set [" + descriptorSet.getDebugName() + "] is "
				"still using destroyed pipeline [" + debugName + "]");
		}
	}
	#endif

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		destroyVkPipeline(instance, pipelineLayout, 
			samplers, descriptorSetLayouts, descriptorPools, variantCount);
	}
	else abort();

	return true;
}

vector<void*> Pipeline::createShaders(const vector<uint8>* codeArray, uint8 shaderCount, const fs::path& pipelinePath)
{
	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		return createVkShaders(codeArray, shaderCount, pipelinePath);
	else abort();
}
void Pipeline::destroyShaders(const vector<void*>& shaders)
{
	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		for (auto shader : shaders)
			vulkanAPI->device.destroyShaderModule((VkShaderModule)shader);
	}
	else abort();
}

//**********************************************************************************************************************
void Pipeline::fillVkSpecConsts(const fs::path& path, void* specInfo, const Pipeline::SpecConsts& specConsts, 
	const Pipeline::SpecConstValues& specConstValues, ShaderStage shaderStage, uint8 variantCount)
{
	auto info = (vk::SpecializationInfo*)specInfo;
	uint32 dataSize = 0, entryCount = 0;

	if (variantCount > 1)
	{
		dataSize = sizeof(uint32);
		entryCount = 1;
	}

	for (const auto& pair : specConsts)
	{
		if (!hasAnyFlag(pair.second.shaderStages, shaderStage))
			continue;
		dataSize += sizeof(uint32);
		entryCount++;
	}

	if (entryCount == 0)
		return;

	auto data = malloc<uint8>(dataSize);
	auto entries = malloc<vk::SpecializationMapEntry>(entryCount);

	uint32 dataOffset = 0, itemIndex = 0;
	if (variantCount > 1)
	{
		vk::SpecializationMapEntry entry(0, 0, sizeof(uint32));
		entries[itemIndex] = entry;
		dataOffset = sizeof(uint32);
		itemIndex = 1;
	}

	for (const auto& pair : specConsts)
	{
		if (!hasAnyFlag(pair.second.shaderStages, shaderStage))
			continue;

		#if GARDEN_DEBUG
		if (specConstValues.find(pair.first) == specConstValues.end())
		{
			throw GardenError("Missing required pipeline spec const. ("
				"specConst: " + pair.first + ", "
				"pipelinePath: " + path.generic_string() + ")");
		}
		#endif

		const auto& value = specConstValues.at(pair.first);
		GARDEN_ASSERT_MSG(value.constBase.type == pair.second.dataType, "Different pipeline "
			"spec const [" + pair.first + "] and provided value types");
		vk::SpecializationMapEntry entry(pair.second.index, dataOffset, sizeof(uint32));
		entries[itemIndex++] = entry;
		memcpy(data + dataOffset, &value.constBase.data, sizeof(uint32));
		dataOffset += sizeof(uint32);
	}
	
	info->mapEntryCount = entryCount;
	info->pMapEntries = entries;
	info->dataSize = dataSize;
	info->pData = data;
}
void Pipeline::setVkVariantIndex(void* specInfo, uint8 variantIndex)
{
	auto info = (vk::SpecializationInfo*)specInfo;
	*((uint32*)info->pData) = variantIndex;
}

//**********************************************************************************************************************
void Pipeline::updateDescriptorsLock(const DescriptorSet::Range* descriptorSetRange, uint8 rangeCount)
{
	auto graphicsAPI = GraphicsAPI::get();
	if (graphicsAPI->currentCommandBuffer == graphicsAPI->frameCommandBuffer)
		return;

	auto currentCommandBuffer = graphicsAPI->currentCommandBuffer;
	for (uint8 i = 0; i < rangeCount; i++)
	{
		auto descriptorSet = descriptorSetRange[i].set;
		auto dsView = graphicsAPI->descriptorSetPool.get(descriptorSet);
		ResourceExt::getBusyLock(**dsView)++;
		currentCommandBuffer->addLockedResource(descriptorSet);

		auto dsPipelineView = graphicsAPI->getPipelineView(dsView->getPipelineType(), dsView->getPipeline());
		const auto& pipelineUniforms = dsPipelineView->getUniforms();
		const auto& dsUniforms = dsView->getUniforms();

		for (const auto& dsUniform : dsUniforms)
		{
			auto searchResult = pipelineUniforms.find(dsUniform.first);
			if (searchResult == pipelineUniforms.end())
				continue;

			const auto& pipelineUniform = searchResult->second;
			auto uniformType = pipelineUniform.type;

			if (isSamplerType(uniformType) || isImageType(uniformType) ||
				uniformType == GslUniformType::SubpassInput)
			{
				for (const auto& resourceArray : dsUniform.second.resourceSets)
				{
					for (auto resource : resourceArray)
					{
						if (!resource)
							continue; // TODO: maybe separate into 2 paths: bindless/nonbindless?

						auto imageViewView = graphicsAPI->imageViewPool.get(ID<ImageView>(resource));
						auto imageView = graphicsAPI->imagePool.get(imageViewView->getImage());
						ResourceExt::getBusyLock(**imageViewView)++;
						ResourceExt::getBusyLock(**imageView)++;
						currentCommandBuffer->addLockedResource(ID<ImageView>(resource));
						currentCommandBuffer->addLockedResource(imageViewView->getImage());

						#if GARDEN_DEBUG
						if (graphicsAPI->currentCommandBuffer == graphicsAPI->computeCommandBuffer)
						{
							GARDEN_ASSERT_MSG(hasAnyFlag(imageView->getUsage(), Image::Usage::ComputeQ), 
								"Image [" + imageView->getDebugName() + "] does not have compute queue flag");
						}
						#endif
					}
				}
			}
			else if (isBufferType(uniformType))
			{
				for (const auto& resourceArray : dsUniform.second.resourceSets)
				{
					for (auto resource : resourceArray)
					{
						if (!resource)
							continue;

						auto bufferView = graphicsAPI->bufferPool.get(ID<Buffer>(resource));
						ResourceExt::getBusyLock(**bufferView)++;
						currentCommandBuffer->addLockedResource(ID<Buffer>(resource));

						#if GARDEN_DEBUG
						if (graphicsAPI->currentCommandBuffer == graphicsAPI->computeCommandBuffer)
						{
							GARDEN_ASSERT_MSG(hasAnyFlag(bufferView->getUsage(), Buffer::Usage::ComputeQ), 
								"Buffer [" + bufferView->getDebugName() + "] does not have compute queue flag");
						}
						#endif
					}
				}
			}
			else if (uniformType == GslUniformType::AccelerationStructure)
			{
				for (const auto& resourceArray : dsUniform.second.resourceSets)
				{
					for (auto resource : resourceArray)
					{
						if (!resource)
							continue;

						auto tlasView = graphicsAPI->tlasPool.get(ID<Tlas>(resource));
						ResourceExt::getBusyLock(**tlasView)++;
						currentCommandBuffer->addLockedResource(ID<Tlas>(resource));

						auto& instances = TlasExt::getInstances(**tlasView);
						for (const auto& instance : instances)
						{
							auto blasView = graphicsAPI->blasPool.get(instance.blas);
							ResourceExt::getBusyLock(**blasView)++;
							currentCommandBuffer->addLockedResource(instance.blas);

							#if GARDEN_DEBUG
							if (graphicsAPI->currentCommandBuffer == graphicsAPI->computeCommandBuffer)
							{
								auto bufferView = graphicsAPI->bufferPool.get(blasView->getStorageBuffer());
								GARDEN_ASSERT_MSG(hasAnyFlag(bufferView->getUsage(), Buffer::Usage::ComputeQ), 
									"BLAS buffer [" + bufferView->getDebugName() + "] does not have compute queue flag");
							}
							#endif
						}

						#if GARDEN_DEBUG
						if (graphicsAPI->currentCommandBuffer == graphicsAPI->computeCommandBuffer)
						{
							auto bufferView = graphicsAPI->bufferPool.get(tlasView->getStorageBuffer());
							GARDEN_ASSERT_MSG(hasAnyFlag(bufferView->getUsage(), Buffer::Usage::ComputeQ), 
								"TLAS buffer [" + bufferView->getDebugName() + "] does not have compute queue flag");
						}
						#endif
					}
				}
			}
			else abort();
		}
	}
}

bool Pipeline::checkThreadIndex(int32 threadIndex)
{
	return threadIndex >= 0 && threadIndex < GraphicsAPI::get()->threadCount;
}

//**********************************************************************************************************************
void Pipeline::bind(uint8 variant)
{
	GARDEN_ASSERT_MSG(variant < variantCount, "Assert " + debugName);
	GARDEN_ASSERT_MSG(!GraphicsAPI::get()->isCurrentRenderPassAsync, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(instance, "Pipeline [" + debugName + "] is not ready");

	auto graphicsAPI = GraphicsAPI::get();
	auto pipeline = graphicsAPI->getPipeline(type, this);

	if (type == PipelineType::Graphics)
	{
		GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentFramebuffer, "Assert " + debugName);
		if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
		{
			busyLock++;
			graphicsAPI->currentCommandBuffer->addLockedResource(ID<GraphicsPipeline>(pipeline));
		}
	}
	else if (type == PipelineType::Compute)
	{
		if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
		{
			busyLock++;
			graphicsAPI->currentCommandBuffer->addLockedResource(ID<ComputePipeline>(pipeline));
		}
	}
	else if (type == PipelineType::RayTracing)
	{
		if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
		{
			busyLock++;
			graphicsAPI->currentCommandBuffer->addLockedResource(ID<RayTracingPipeline>(pipeline));
		}
	}
	else abort();

	BindPipelineCommand command;
	command.pipelineType = type;
	command.variant = variant;
	command.pipeline = pipeline;
	graphicsAPI->currentCommandBuffer->addCommand(command);
}

//**********************************************************************************************************************
void Pipeline::bindAsync(uint8 variant, int32 threadIndex)
{
	GARDEN_ASSERT_MSG(asyncRecording, "Assert " + debugName);
	GARDEN_ASSERT_MSG(variant < variantCount, "Assert " + debugName);
	GARDEN_ASSERT_MSG(threadIndex < GraphicsAPI::get()->threadCount, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->isCurrentRenderPassAsync, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(instance, "Pipeline [" + debugName + "] is not ready");

	auto graphicsAPI = GraphicsAPI::get();
	auto pipeline = graphicsAPI->getPipeline(type, this);

	graphicsAPI->currentCommandBuffer->commandMutex.lock();
	if (type == PipelineType::Graphics)
	{
		if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
		{
			busyLock++;
			graphicsAPI->currentCommandBuffer->addLockedResource(ID<GraphicsPipeline>(pipeline));
		}
	}
	else if (type == PipelineType::Compute)
	{
		if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
		{
			busyLock++;
			graphicsAPI->currentCommandBuffer->addLockedResource(ID<ComputePipeline>(pipeline));
		}
	}
	else if (type == PipelineType::RayTracing)
	{
		if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
		{
			busyLock++;
			graphicsAPI->currentCommandBuffer->addLockedResource(ID<RayTracingPipeline>(pipeline));
		}
	}
	else abort();
	graphicsAPI->currentCommandBuffer->commandMutex.unlock();

	auto autoThreadCount = graphicsAPI->calcAutoThreadCount(threadIndex);
	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		auto vkBindPoint = toVkPipelineBindPoint(type);

		while (threadIndex < autoThreadCount)
		{
			if (pipeline != graphicsAPI->currentPipelines[threadIndex] ||
				type != graphicsAPI->currentPipelineTypes[threadIndex] ||
				variant != graphicsAPI->currentPipelineVariants[threadIndex])
			{
				vk::Pipeline vkPipeline = variantCount > 1 ? ((VkPipeline*)instance)[variant] : (VkPipeline)instance;
				vulkanAPI->secondaryCommandBuffers[threadIndex].bindPipeline(vkBindPoint, vkPipeline);

				vulkanAPI->currentPipelines[threadIndex] = pipeline;
				vulkanAPI->currentPipelineTypes[threadIndex] = type;
				vulkanAPI->currentPipelineVariants[threadIndex] = variant;
			}
			threadIndex++;
		}
	}
	else abort();
}

//**********************************************************************************************************************
void Pipeline::bindDescriptorSets(const DescriptorSet::Range* descriptorSetRange, uint8 rangeCount)
{
	GARDEN_ASSERT_MSG(descriptorSetRange, "Assert " + debugName);
	GARDEN_ASSERT_MSG(rangeCount > 0, "Assert " + debugName);
	GARDEN_ASSERT_MSG(!GraphicsAPI::get()->isCurrentRenderPassAsync, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(instance, "Pipeline [" + debugName + "] is not ready");
	auto graphicsAPI = GraphicsAPI::get();

	#if GARDEN_DEBUG
	for (uint8 i = 0; i < rangeCount; i++)
	{
		auto descriptor = descriptorSetRange[i];
		GARDEN_ASSERT_MSG(descriptor.set, "Pipeline [" + debugName + "] "
			"descriptor set [" + to_string(i) +  "] is null");
		auto descriptorSetView = graphicsAPI->descriptorSetPool.get(descriptor.set);
		GARDEN_ASSERT_MSG(descriptor.offset + descriptor.count <= descriptorSetView->getSetCount(), 
			"Out of pipeline [" + debugName + "] descriptor set count range");
		auto pipeline = graphicsAPI->getPipeline(descriptorSetView->getPipelineType(), this);
		GARDEN_ASSERT_MSG(pipeline == descriptorSetView->getPipeline(), "Descriptor set [" + 
			to_string(i) +  "] pipeline is different from this pipeline [" + debugName + "]");
	}
	#endif
	
	BindDescriptorSetsCommand command;
	command.rangeCount = rangeCount;
	command.descriptorSetRange = descriptorSetRange;
	graphicsAPI->currentCommandBuffer->addCommand(command);

	updateDescriptorsLock(descriptorSetRange, rangeCount);
}

//**********************************************************************************************************************
void Pipeline::bindDescriptorSetsAsync(const DescriptorSet::Range* descriptorSetRange, uint8 rangeCount, int32 threadIndex)
{
	GARDEN_ASSERT_MSG(asyncRecording, "Assert " + debugName);
	GARDEN_ASSERT_MSG(descriptorSetRange, "Assert " + debugName);
	GARDEN_ASSERT_MSG(rangeCount > 0, "Assert " + debugName);
	GARDEN_ASSERT_MSG(threadIndex < GraphicsAPI::get()->threadCount, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->isCurrentRenderPassAsync, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(instance, "Pipeline [" + debugName + "] is not ready");
	auto graphicsAPI = GraphicsAPI::get();

	#if GARDEN_DEBUG
	for (uint8 i = 0; i < rangeCount; i++)
	{
		auto descriptor = descriptorSetRange[i];
		GARDEN_ASSERT_MSG(descriptor.set,"Pipeline [" + debugName + "] "
			"descriptor set [" + to_string(i) +  "] is null");
		auto thisPipeline = graphicsAPI->getPipeline(type, this);
		auto descriptorSetView = graphicsAPI->descriptorSetPool.get(descriptor.set);
		GARDEN_ASSERT_MSG(thisPipeline == descriptorSetView->getPipeline(), "Descriptor set [" + 
			to_string(i) +  "] pipeline is different from this pipeline [" + debugName + "]");
		GARDEN_ASSERT_MSG(descriptor.offset + descriptor.count <= descriptorSetView->getSetCount(),
			"Out of pipeline [" + debugName + "] descriptor set count range");
	}
	#endif

	auto autoThreadCount = graphicsAPI->calcAutoThreadCount(threadIndex);
	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		auto& vkDescriptorSets = vulkanAPI->bindDescriptorSets[threadIndex];

		for (uint8 i = 0; i < rangeCount; i++)
		{
			auto descriptor = descriptorSetRange[i];
			auto descriptorSetView = graphicsAPI->descriptorSetPool.get(descriptor.set);
			auto instance = (vk::DescriptorSet*)ResourceExt::getInstance(**descriptorSetView);

			if (descriptorSetView->getSetCount() > 1)
			{
				auto count = descriptor.offset + descriptor.count;
				for (uint32 j = descriptor.offset; j < count; j++)
					vkDescriptorSets.push_back(instance[j]);
			}
			else
			{
				vkDescriptorSets.push_back((VkDescriptorSet)instance);
			}
		}

		auto bindPoint = toVkPipelineBindPoint(type);
		while (threadIndex < autoThreadCount)
		{
			vulkanAPI->secondaryCommandBuffers[threadIndex++].bindDescriptorSets(
				bindPoint, (VkPipelineLayout)pipelineLayout, 0, 
				(uint32)vkDescriptorSets.size(), vkDescriptorSets.data(), 0, nullptr);
		}

		vkDescriptorSets.clear();
	}
	else abort();

	BindDescriptorSetsCommand command;
	command.asyncRecording = true;
	command.rangeCount = rangeCount;
	command.descriptorSetRange = descriptorSetRange;

	auto currentCommandBuffer = graphicsAPI->currentCommandBuffer;
	currentCommandBuffer->commandMutex.lock();
	currentCommandBuffer->addCommand(command);
	updateDescriptorsLock(descriptorSetRange, rangeCount);
	currentCommandBuffer->commandMutex.unlock();
}

//**********************************************************************************************************************
void Pipeline::pushConstants(const void* data)
{
	GARDEN_ASSERT_MSG(data, "Assert " + debugName);
	GARDEN_ASSERT_MSG(pushConstantsSize > 0, "Assert " + debugName);
	GARDEN_ASSERT_MSG(!GraphicsAPI::get()->isCurrentRenderPassAsync, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(instance, "Pipeline [" + debugName + "] is not ready");

	PushConstantsCommand command;
	command.dataSize = pushConstantsSize;
	command.shaderStages = pushConstantsMask;
	command.pipelineLayout = pipelineLayout;
	command.data = data;
	GraphicsAPI::get()->currentCommandBuffer->addCommand(command);
}
void Pipeline::pushConstantsAsync(const void* data, int32 threadIndex)
{
	GARDEN_ASSERT_MSG(data, "Assert " + debugName);
	GARDEN_ASSERT_MSG(asyncRecording, "Assert " + debugName);
	GARDEN_ASSERT_MSG(pushConstantsSize > 0, "Assert " + debugName);
	GARDEN_ASSERT_MSG(threadIndex >= 0, "Assert " + debugName);
	GARDEN_ASSERT_MSG(threadIndex < GraphicsAPI::get()->threadCount, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->isCurrentRenderPassAsync, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(instance, "Pipeline [" + debugName + "] is not ready");

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		VulkanAPI::get()->secondaryCommandBuffers[threadIndex].pushConstants(
			vk::PipelineLayout((VkPipelineLayout)pipelineLayout), 
			(vk::ShaderStageFlags)pushConstantsMask, 0, pushConstantsSize, data);
	}
	else abort();
}