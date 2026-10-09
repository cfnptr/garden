// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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
	const Pipeline::SamplerStates& samplerStates, absl::flat_hash_map<string, vk::Sampler>& immutableSamplers, 
	const fs::path& pipelinePath, const Pipeline::SamplerStates& samplerStateOverrides)
{
	auto vulkanAPI = VulkanAPI::get();
	vector<void*> samplers(samplerStates.size());
	auto samplerData = samplers.data();

	uint32 i = 0;
	for (auto it = samplerStates.begin(); it != samplerStates.end(); it++, i++)
	{
		auto& uniform = uniforms.at(it->first);
		if (uniform.isMutable)
		{
			GARDEN_ASSERT_MSG(samplerStateOverrides.find(it->first) == samplerStateOverrides.end(), 
				"Can't override 'mutable' shader uniform [" + it->first + "] sampler state");
			continue;
		}

		auto samplerStateSearch = samplerStateOverrides.find(it->first);
		auto state = samplerStateSearch == samplerStateOverrides.end() ?
			it->second : samplerStateSearch->second;
		auto samplerInfo = getVkSamplerCreateInfo(state);
		auto sampler = vulkanAPI->device.createSampler(samplerInfo);
		samplerData[i] = (VkSampler)sampler;
		auto emplaceResult = immutableSamplers.emplace(it->first, sampler);
		GARDEN_ASSERT_MSG(emplaceResult.second, "Detected memory corruption");

		#if GARDEN_DEBUG // Note: No GARDEN_EDITOR
		if (vulkanAPI->features.debugUtils)
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
	const Pipeline::Uniforms& pipelineUniforms, const absl::flat_hash_map<string, vk::Sampler>& immutableSamplers,
	const fs::path& pipelinePath, uint32 maxBindlessCount, uint8 descriptorSetCount)
{
	auto vulkanAPI = VulkanAPI::get();
	vector<vk::DescriptorSetLayoutBinding> descriptorSetBindings(pipelineUniforms.size());
	vector<vk::DescriptorBindingFlags> descriptorBindingFlags;
	vector<vector<vk::Sampler>> samplerArrays;
	vector<vk::DescriptorPoolSize> descriptorPoolSizes;
	descriptorSetLayouts.reserve(descriptorSetCount);
	descriptorPools.reserve(descriptorSetCount);

	for (uint8 dsIndex = 0; dsIndex < descriptorSetCount; dsIndex++)
	{
		uint32 bindingIndex = 0; auto isBindless = false;
		for	(const auto& uniformPair : pipelineUniforms)
		{
			auto pipelineUniform = uniformPair.second;
			if (pipelineUniform.descriptorSetIndex != dsIndex)
				continue;

			auto& descriptorSetBinding = descriptorSetBindings[bindingIndex];
			descriptorSetBinding.binding = (uint32)pipelineUniform.bindingIndex;
			descriptorSetBinding.descriptorType = toVkDescriptorType(pipelineUniform.type);
			descriptorSetBinding.stageFlags = toVkShaderStages(pipelineUniform.pipelineStages);

			if (pipelineUniform.arraySize > 0)
			{
				if (!pipelineUniform.isMutable && pipelineUniform.isSamplerType)
				{
					if (pipelineUniform.arraySize > 1)
					{
						// TODO: allow to specify sampler states for separate uniform array elements?
						vector<vk::Sampler> samplers(pipelineUniform.arraySize, *&immutableSamplers.at(uniformPair.first));
						samplerArrays.push_back(std::move(samplers));
						descriptorSetBinding.pImmutableSamplers = samplerArrays[samplerArrays.size() - 1].data();
					}
					else
					{
						descriptorSetBinding.pImmutableSamplers = &immutableSamplers.at(uniformPair.first);
					}
				}
				descriptorSetBinding.descriptorCount = pipelineUniform.arraySize;
			}
			else
			{
				GARDEN_ASSERT_MSG(maxBindlessCount > 0, "Can't use bindless uniforms inside non bindless pipeline");

				if (descriptorBindingFlags.size() < pipelineUniforms.size())
					descriptorBindingFlags.resize(pipelineUniforms.size());
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

				if (!pipelineUniform.isMutable && pipelineUniform.isSamplerType)
				{
					vector<vk::Sampler> samplers(maxBindlessCount, *&immutableSamplers.at(uniformPair.first));
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
			for (auto i = descriptorPoolSizes.begin(); i != descriptorPoolSizes.end(); i++)
			{
				if (i->descriptorCount > 0)
				{
					maxSetCount += i->descriptorCount;
					continue;
				}

				auto size = descriptorPoolSizes.size();
				if (size > 0) i = descriptorPoolSizes.end() - 1;
				descriptorPoolSizes.resize(size - 1);
			}
			GARDEN_ASSERT(maxSetCount > 0);

			vk::DescriptorPoolCreateInfo descriptorPoolInfo(vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind, 
				maxSetCount, (uint32)descriptorPoolSizes.size(), descriptorPoolSizes.data());
			descriptorPools.push_back(vulkanAPI->device.createDescriptorPool(descriptorPoolInfo));
			descriptorPoolSizes.clear();

			#if GARDEN_DEBUG // Note: No GARDEN_EDITOR
			if (vulkanAPI->features.debugUtils)
			{
				auto name = "descriptorPool." + pipelinePath.generic_string() + to_string(dsIndex);
				vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eDescriptorPool,
					(uint64)(VkSampler)descriptorPools[dsIndex], name.c_str());
				vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo);
			}
			#endif
		}
		else descriptorPools.push_back(nullptr);

		descriptorSetLayouts.push_back(vulkanAPI->device.createDescriptorSetLayout(descriptorSetLayoutInfo));
		samplerArrays.clear();

		#if GARDEN_DEBUG // Note: No GARDEN_EDITOR
		if (vulkanAPI->features.debugUtils)
		{
			auto name = "descriptorSetLayout." + pipelinePath.generic_string() + to_string(dsIndex);
			vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eDescriptorSetLayout,
				(uint64)descriptorSetLayouts.back(), name.c_str());
			vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo);
		}
		#endif
	}
}

//**********************************************************************************************************************
static vk::PipelineLayout createVkPipelineLayout(uint16 pushConstantsSize, PipelineStage pushConstantStages,
	const vector<void*>& descriptorSetLayouts, const fs::path& pipelinePath)
{
	vector<vk::PushConstantRange> pushConstantRanges;

	if (hasAnyFlag(pushConstantStages, PipelineStage::Vertex))
		pushConstantRanges.emplace_back(vk::ShaderStageFlagBits::eVertex, 0, pushConstantsSize);
	if (hasAnyFlag(pushConstantStages, PipelineStage::Fragment))
		pushConstantRanges.emplace_back(vk::ShaderStageFlagBits::eFragment, 0, pushConstantsSize);
	if (hasAnyFlag(pushConstantStages, PipelineStage::Compute))
		pushConstantRanges.emplace_back(vk::ShaderStageFlagBits::eCompute, 0, pushConstantsSize);
	if (hasAnyFlag(pushConstantStages, PipelineStage::RayGeneration))
		pushConstantRanges.emplace_back(vk::ShaderStageFlagBits::eRaygenKHR, 0, pushConstantsSize);
	if (hasAnyFlag(pushConstantStages, PipelineStage::Intersection))
		pushConstantRanges.emplace_back(vk::ShaderStageFlagBits::eIntersectionKHR, 0, pushConstantsSize);
	if (hasAnyFlag(pushConstantStages, PipelineStage::AnyHit))
		pushConstantRanges.emplace_back(vk::ShaderStageFlagBits::eAnyHitKHR, 0, pushConstantsSize);
	if (hasAnyFlag(pushConstantStages, PipelineStage::ClosestHit))
		pushConstantRanges.emplace_back(vk::ShaderStageFlagBits::eClosestHitKHR, 0, pushConstantsSize);
	if (hasAnyFlag(pushConstantStages, PipelineStage::Miss))
		pushConstantRanges.emplace_back(vk::ShaderStageFlagBits::eMissKHR, 0, pushConstantsSize);
	if (hasAnyFlag(pushConstantStages, PipelineStage::Callable))
		pushConstantRanges.emplace_back(vk::ShaderStageFlagBits::eCallableKHR, 0, pushConstantsSize);
	if (hasAnyFlag(pushConstantStages, PipelineStage::Mesh))
		pushConstantRanges.emplace_back(vk::ShaderStageFlagBits::eMeshEXT, 0, pushConstantsSize);
	if (hasAnyFlag(pushConstantStages, PipelineStage::Task))
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
	if (vulkanAPI->features.debugUtils)
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
static vector<void*> createVkShaders(const raw_vector<uint8>* codeArray, uint8 shaderCount, const fs::path& pipelinePath)
{
	auto vulkanAPI = VulkanAPI::get();
	vector<void*> shaders(shaderCount);
	auto shaderData = shaders.data();

	for (uint8 i = 0; i < shaderCount; i++)
	{
		const auto& shaderCode = codeArray[i];
		vk::ShaderModuleCreateInfo shaderInfo({}, (uint32)shaderCode.size(), (const uint32*)shaderCode.data());
		auto shader = (VkShaderModule)vulkanAPI->device.createShaderModule(shaderInfo);
		shaderData[i] = shader;

		#if GARDEN_DEBUG // Note: No GARDEN_EDITOR
		if (vulkanAPI->features.debugUtils)
		{
			auto _name = "shaderModule." + pipelinePath.generic_string() + to_string(i);
			vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eShaderModule, (uint64)shader, _name.c_str());
			vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo);
		}
		#endif
	}

	return shaders;
}

//**********************************************************************************************************************
Pipeline::Pipeline(CreateData& createData)
{
	this->uniforms = std::move(createData.uniforms);
	this->pipelineVersion = createData.pipelineVersion;
	this->pushConstantStages = createData.pushConstantStages;
	this->pushConstantsSize = createData.pushConstantsSize;
	this->variantCount = createData.variantCount;

	auto graphicsAPI = GraphicsAPI::get();
	auto graphicsBackend = graphicsAPI->getBackendType();

	if (graphicsBackend == GraphicsBackend::VulkanAPI)
	{
		absl::flat_hash_map<string, vk::Sampler> immutableSamplers;
		this->samplers = createVkPipelineSamplers(uniforms, createData.samplerStates,
			immutableSamplers, createData.shaderPath, createData.samplerStateOverrides);

		if (createData.descriptorSetCount > 0)
		{
			createVkDescriptorSetLayouts(descriptorSetLayouts, descriptorPools, uniforms, immutableSamplers, 
				createData.shaderPath, createData.maxBindlessCount, createData.descriptorSetCount);
		}
		
		this->pipelineLayout = createVkPipelineLayout(pushConstantsSize,
			createData.pushConstantStages, descriptorSetLayouts, createData.shaderPath);
	}
	else abort();
}

bool Pipeline::destroy()
{
	if (!instance || isLocked())
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

	auto graphicsBackend = GraphicsAPI::get()->getBackendType();
	if (graphicsBackend == GraphicsBackend::VulkanAPI)
	{
		destroyVkPipeline(instance, pipelineLayout, 
			samplers, descriptorSetLayouts, descriptorPools, variantCount);
	}
	else abort();

	return true;
}

vector<void*> Pipeline::createShaders(const raw_vector<uint8>* codeArray, uint8 shaderCount, const fs::path& pipelinePath)
{
	auto graphicsBackend = GraphicsAPI::get()->getBackendType();
	if (graphicsBackend == GraphicsBackend::VulkanAPI)
		return createVkShaders(codeArray, shaderCount, pipelinePath);
	else abort();
}
void Pipeline::destroyShaders(const vector<void*>& shaders)
{
	auto graphicsBackend = GraphicsAPI::get()->getBackendType();
	if (graphicsBackend == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		for (auto shader : shaders)
			vulkanAPI->device.destroyShaderModule((VkShaderModule)shader);
	}
	else abort();
}

//**********************************************************************************************************************
void Pipeline::fillVkSpecConsts(const fs::path& path, void* specInfo, const Pipeline::SpecConsts& specConsts, 
	const Pipeline::SpecConstValues& specConstValues, PipelineStage pipelineStage, uint8 variantCount)
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
		if (!hasAnyFlag(pair.second.pipelineStages, pipelineStage))
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
		if (!hasAnyFlag(pair.second.pipelineStages, pipelineStage))
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
		GARDEN_ASSERT_MSG(value.constBase.type == pair.second.type, "Different pipeline "
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
void Pipeline::freeVkSpecConsts(void* specInfo)
{
	auto info = (vk::SpecializationInfo*)specInfo;
	free((void*)info->pData); free((void*)info->pMapEntries);
}
void Pipeline::setVkVariantIndex(void* specInfo, uint8 variantIndex) noexcept
{
	auto info = (vk::SpecializationInfo*)specInfo;
	*((uint32*)info->pData) = variantIndex;
}

//**********************************************************************************************************************
void Pipeline::updateDescriptorsLock(const DescriptorSet::Range* ranges, uint8 rangeCount, int32 threadIndex)
{
	auto graphicsAPI = GraphicsAPI::get();
	auto currentCommandBuffer = graphicsAPI->currentCommandBuffer;
	auto commandBufferType = currentCommandBuffer->getType();
	if (commandBufferType == CommandBufferType::Frame)
		return;

	for (uint8 i = 0; i < rangeCount; i++) // TODO: make non async variant with lock() or multithread this?
	{
		auto descriptorSet = ranges[i].set;
		currentCommandBuffer->lockResource(descriptorSet, threadIndex);

		auto dsView = graphicsAPI->descriptorSetPool.get(descriptorSet);
		auto dsPipelineView = graphicsAPI->getPipelineView(dsView->getPipelineType(), dsView->getPipeline());
		const auto& pipelineUniforms = dsPipelineView->getUniforms();
		const auto& dsUniforms = dsView->getUniforms();

		for (const auto& dsUniform : dsUniforms)
		{
			auto uniformPair = pipelineUniforms.find(dsUniform.first);
			if (uniformPair == pipelineUniforms.end())
				continue;

			auto pipelineUniform = uniformPair->second;
			if (pipelineUniform.isSamplerType | pipelineUniform.isImageType)
			{
				for (const auto& resourceArray : dsUniform.second.resourceSets)
				{
					for (auto resource : resourceArray)
					{
						if (!resource)
							continue; // TODO: maybe separate into 2 paths: bindless/nonbindless?

						auto imageViewView = graphicsAPI->imageViewPool.get(ID<ImageView>(resource));
						currentCommandBuffer->lockResource(ID<ImageView>(resource), threadIndex);
						currentCommandBuffer->lockResource(imageViewView->getImage(), threadIndex);

						#if GARDEN_DEBUG
						if (commandBufferType == CommandBufferType::Compute)
						{
							auto imageView = graphicsAPI->imagePool.get(imageViewView->getImage());
							GARDEN_ASSERT_MSG(hasAnyFlag(imageView->getUsage(), Image::Usage::ComputeQ), 
								"Image [" + imageView->getDebugName() + "] does not have compute queue flag");
						}
						#endif
					}
				}
			}
			else if (pipelineUniform.isBufferType)
			{
				for (const auto& resourceArray : dsUniform.second.resourceSets)
				{
					for (auto resource : resourceArray)
					{
						if (!resource)
							continue;
						currentCommandBuffer->lockResource(ID<Buffer>(resource), threadIndex);

						#if GARDEN_DEBUG
						if (commandBufferType == CommandBufferType::Compute)
						{
							auto bufferView = graphicsAPI->bufferPool.get(ID<Buffer>(resource));
							GARDEN_ASSERT_MSG(hasAnyFlag(bufferView->getUsage(), Buffer::Usage::ComputeQ), 
								"Buffer [" + bufferView->getDebugName() + "] does not have compute queue flag");
						}
						#endif
					}
				}
			}
			else if (pipelineUniform.type == GslUniformType::AccelerationStructure)
			{
				for (const auto& resourceArray : dsUniform.second.resourceSets)
				{
					for (auto resource : resourceArray)
					{
						if (!resource)
							continue;
						currentCommandBuffer->lockResource(ID<Tlas>(resource), threadIndex);

						auto tlasView = graphicsAPI->tlasPool.get(ID<Tlas>(resource));
						auto& instances = TlasExt::getInstances(**tlasView);
		
						for (const auto& instance : instances)
						{
							currentCommandBuffer->lockResource(instance.blas, threadIndex);

							#if GARDEN_DEBUG
							if (commandBufferType == CommandBufferType::Compute)
							{
								auto blasView = graphicsAPI->blasPool.get(instance.blas);
								auto bufferView = graphicsAPI->bufferPool.get(blasView->getStorageBuffer());
								GARDEN_ASSERT_MSG(hasAnyFlag(bufferView->getUsage(), Buffer::Usage::ComputeQ), 
									"BLAS buffer [" + bufferView->getDebugName() + "] does not have compute queue flag");
							}
							#endif
						}

						#if GARDEN_DEBUG
						if (commandBufferType == CommandBufferType::Compute)
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

//**********************************************************************************************************************
void Pipeline::bind(uint8 variant)
{
	auto graphicsAPI = GraphicsAPI::get();
	auto currentCommandBuffer = graphicsAPI->currentCommandBuffer;
	GARDEN_ASSERT_MSG(variant < variantCount, "Assert " + debugName);
	GARDEN_ASSERT_MSG(!graphicsAPI->isRenderPassAsync, "Assert " + debugName);
	GARDEN_ASSERT_MSG(currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(isLoaded(), "Pipeline [" + debugName + "] is not loaded");

	auto pipeline = graphicsAPI->getPipeline(type, this);
	if (pipeline == graphicsAPI->currentPipelines.front() && type == graphicsAPI->currentPipelineTypes.front() &&
		variant == graphicsAPI->currentPipelineVariants.front())
	{
		return;
	}

	if (currentCommandBuffer->getType() != CommandBufferType::Frame)
	{
		if (type == PipelineType::Graphics)
		{
			GARDEN_ASSERT_MSG(graphicsAPI->renderPassFramebuffer, "Assert " + debugName);
			currentCommandBuffer->lockResource(ID<GraphicsPipeline>(pipeline));
		}
		else if (type == PipelineType::Compute)
		{
			GARDEN_ASSERT_MSG(!graphicsAPI->renderPassFramebuffer, "Assert " + debugName);
			currentCommandBuffer->lockResource(ID<ComputePipeline>(pipeline));
		}
		else if (type == PipelineType::RayTracing)
		{
			GARDEN_ASSERT_MSG(!graphicsAPI->renderPassFramebuffer, "Assert " + debugName);
			currentCommandBuffer->lockResource(ID<RayTracingPipeline>(pipeline));
		}
		else abort();
	}

	BindPipelineCommand command;
	command.pipelineType = type;
	command.variant = variant;
	command.pipeline = pipeline;
	currentCommandBuffer->addCommand(command);

	graphicsAPI->currentPipelines.front() = pipeline;
	graphicsAPI->currentPipelineTypes.front() = type;
	graphicsAPI->currentPipelineVariants.front() = variant;
}

//**********************************************************************************************************************
void Pipeline::bindAsync(uint8 variant, int32 threadIndex)
{
	auto graphicsAPI = GraphicsAPI::get();
	auto currentCommandBuffer = graphicsAPI->currentCommandBuffer;
	GARDEN_ASSERT_MSG(variant < variantCount, "Assert " + debugName);
	GARDEN_ASSERT_MSG(graphicsAPI->isRenderPassAsync, "Assert " + debugName);
	GARDEN_ASSERT_MSG(currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(isLoaded(), "Pipeline [" + debugName + "] is not loaded");

	auto graphicsBackend = graphicsAPI->getBackendType();
	auto pipeline = graphicsAPI->getPipeline(type, this);
	auto autoThreadCount = graphicsAPI->calcAutoThreadCount(threadIndex);

	if (graphicsBackend == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		auto vkBindPoint = toVkPipelineBindPoint(type);
		auto& secondaryCommandBuffers = vulkanAPI->secondaryCommandBuffers;
	
		while (threadIndex < autoThreadCount)
		{
			if (currentCommandBuffer->getType() != CommandBufferType::Frame)
			{
				if (type == PipelineType::Graphics)
					currentCommandBuffer->lockResource(ID<GraphicsPipeline>(pipeline));
				else if (type == PipelineType::Compute)
					currentCommandBuffer->lockResource(ID<ComputePipeline>(pipeline));
				else if (type == PipelineType::RayTracing)
					currentCommandBuffer->lockResource(ID<RayTracingPipeline>(pipeline));
				else abort();
			}

			if (pipeline != graphicsAPI->currentPipelines[threadIndex] ||
				type != graphicsAPI->currentPipelineTypes[threadIndex] ||
				variant != graphicsAPI->currentPipelineVariants[threadIndex])
			{
				vk::Pipeline vkPipeline = variantCount > 1 ? ((VkPipeline*)instance)[variant] : (VkPipeline)instance;
				secondaryCommandBuffers[threadIndex].bindPipeline(vkBindPoint, vkPipeline);

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
void Pipeline::bindDescriptorSets(const DescriptorSet::Range* ranges, uint8 rangeCount)
{
	auto graphicsAPI = GraphicsAPI::get();
	auto currentCommandBuffer = graphicsAPI->currentCommandBuffer;
	GARDEN_ASSERT_MSG(ranges, "Assert " + debugName);
	GARDEN_ASSERT_MSG(rangeCount > 0, "Assert " + debugName);
	GARDEN_ASSERT_MSG(!graphicsAPI->isRenderPassAsync, "Assert " + debugName);
	GARDEN_ASSERT_MSG(currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(ID<Pipeline>(graphicsAPI->getPipeline(type, this)) == 
		graphicsAPI->currentPipelines.front(), "Assert " + debugName);
	GARDEN_ASSERT_MSG(isLoaded(), "Pipeline [" + debugName + "] is not loaded");

	#if GARDEN_DEBUG
	for (uint8 i = 0; i < rangeCount; i++)
	{
		auto descriptorSetRange = ranges[i];
		GARDEN_ASSERT_MSG(descriptorSetRange.set, "Pipeline [" + debugName + "] "
			"descriptor set [" + to_string(i) +  "] is null");
		auto descriptorSetView = graphicsAPI->descriptorSetPool.get(descriptorSetRange.set);
		GARDEN_ASSERT_MSG(descriptorSetRange.offset + descriptorSetRange.count <= descriptorSetView->getSetCount(), 
			"Out of pipeline [" + debugName + "] descriptor set count range");
		auto pipeline = graphicsAPI->getPipeline(descriptorSetView->getPipelineType(), this);
		GARDEN_ASSERT_MSG(pipeline == descriptorSetView->getPipeline(), "Descriptor set [" + 
			to_string(i) +  "] pipeline is different from this pipeline [" + debugName + "]");
	}
	#endif
	
	BindDescriptorSetsCommand command;
	command.rangeCount = rangeCount;
	command.ranges = ranges;
	currentCommandBuffer->addCommand(command);
	updateDescriptorsLock(ranges, rangeCount);
}

//**********************************************************************************************************************
void Pipeline::bindDescriptorSetsAsync(const DescriptorSet::Range* ranges, uint8 rangeCount, int32 threadIndex)
{
	auto graphicsAPI = GraphicsAPI::get();
	auto currentCommandBuffer = graphicsAPI->currentCommandBuffer;
	GARDEN_ASSERT_MSG(ranges, "Assert " + debugName);
	GARDEN_ASSERT_MSG(rangeCount > 0, "Assert " + debugName);
	GARDEN_ASSERT_MSG(graphicsAPI->isRenderPassAsync, "Assert " + debugName);
	GARDEN_ASSERT_MSG(currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(isLoaded(), "Pipeline [" + debugName + "] is not loaded");

	#if GARDEN_DEBUG
	for (uint8 i = 0; i < rangeCount; i++)
	{
		auto descriptorSetRange = ranges[i];
		GARDEN_ASSERT_MSG(descriptorSetRange.set,"Pipeline [" + debugName + "] "
			"descriptor set [" + to_string(i) +  "] is null");
		auto thisPipeline = graphicsAPI->getPipeline(type, this);
		auto descriptorSetView = graphicsAPI->descriptorSetPool.get(descriptorSetRange.set);
		GARDEN_ASSERT_MSG(thisPipeline == descriptorSetView->getPipeline(), "Descriptor set [" + 
			to_string(i) +  "] pipeline is different from this pipeline [" + debugName + "]");
		GARDEN_ASSERT_MSG(descriptorSetRange.offset + descriptorSetRange.count <= descriptorSetView->getSetCount(),
			"Out of pipeline [" + debugName + "] descriptor set count range");
	}
	#endif

	BindDescriptorSetsCommand command;
	command.rangeCount = rangeCount;
	command.ranges = ranges;

	auto graphicsBackend = graphicsAPI->getBackendType();
	auto autoThreadCount = graphicsAPI->calcAutoThreadCount(threadIndex);
	GARDEN_ASSERT_MSG(ID<Pipeline>(graphicsAPI->getPipeline(type, this)) == 
		graphicsAPI->currentPipelines[threadIndex], "Assert " + debugName);

	if (graphicsBackend == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		auto& bindDescriptorSets = vulkanAPI->bindDescriptorSets[threadIndex];

		for (uint8 i = 0; i < rangeCount; i++)
		{
			auto descriptorSetRange = ranges[i];
			auto descriptorSetView = graphicsAPI->descriptorSetPool.get(descriptorSetRange.set);
			auto instance = (vk::DescriptorSet*)ResourceExt::getInstance(**descriptorSetView);

			if (descriptorSetView->getSetCount() > 1)
			{
				auto setCount = descriptorSetRange.offset + descriptorSetRange.count;
				for (uint32 j = descriptorSetRange.offset; j < setCount; j++)
					bindDescriptorSets.push_back(instance[j]);
			}
			else bindDescriptorSets.push_back((VkDescriptorSet)instance);
		}

		auto bindPoint = toVkPipelineBindPoint(type);
		auto& secondaryCommandBuffers = vulkanAPI->secondaryCommandBuffers;
		auto vkPipelineLayout = vk::PipelineLayout((VkPipelineLayout)pipelineLayout);
		auto bindDescriptorSetData = bindDescriptorSets.data();
		auto bindDescriptorSetCount = (uint32)bindDescriptorSets.size();

		while (threadIndex < autoThreadCount)
		{
			secondaryCommandBuffers[threadIndex].bindDescriptorSets(bindPoint, 
				vkPipelineLayout, 0, bindDescriptorSetCount, bindDescriptorSetData, 0, nullptr);
			currentCommandBuffer->addCommand(command, threadIndex);
			updateDescriptorsLock(ranges, rangeCount, threadIndex);
			threadIndex++;
		}
		bindDescriptorSets.clear();
	}
	else abort();
}

//**********************************************************************************************************************
void Pipeline::pushConstants(const void* data)
{
	auto graphicsAPI = GraphicsAPI::get();
	auto currentCommandBuffer = graphicsAPI->currentCommandBuffer;
	GARDEN_ASSERT_MSG(data, "Assert " + debugName);
	GARDEN_ASSERT_MSG(pushConstantsSize > 0, "Assert " + debugName);
	GARDEN_ASSERT_MSG(!graphicsAPI->isRenderPassAsync, "Assert " + debugName);
	GARDEN_ASSERT_MSG(graphicsAPI->currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(isLoaded(), "Pipeline [" + debugName + "] is not loaded");

	PushConstantsCommand command;
	command.dataSize = pushConstantsSize;
	command.pipelineStages = pushConstantStages;
	command.pipelineLayout = pipelineLayout;
	command.data = data;
	currentCommandBuffer->addCommand(command);
}
void Pipeline::pushConstantsAsync(const void* data, int32 threadIndex)
{
	auto graphicsAPI = GraphicsAPI::get();
	GARDEN_ASSERT_MSG(data, "Assert " + debugName);
	GARDEN_ASSERT_MSG(pushConstantsSize > 0, "Assert " + debugName);
	GARDEN_ASSERT_MSG(threadIndex >= 0, "Assert " + debugName);
	GARDEN_ASSERT_MSG(graphicsAPI->isRenderPassAsync, "Assert " + debugName);
	GARDEN_ASSERT_MSG(graphicsAPI->currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(isLoaded(), "Pipeline [" + debugName + "] is not loaded");

	graphicsAPI->calcAutoThreadIndex(threadIndex);

	auto graphicsBackend = graphicsAPI->getBackendType();
	if (graphicsBackend == GraphicsBackend::VulkanAPI)
	{
		VulkanAPI::get()->secondaryCommandBuffers[threadIndex].pushConstants(
			vk::PipelineLayout((VkPipelineLayout)pipelineLayout), 
			toVkShaderStages(pushConstantStages), 0, pushConstantsSize, data);
	}
	else abort();
}