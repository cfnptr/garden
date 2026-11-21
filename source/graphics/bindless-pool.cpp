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

#include "garden/graphics/bindless-pool.hpp"
#include "garden/graphics/api.hpp"

using namespace garden;
using namespace garden::graphics;

/** TODO: create local vulkan variables for global shared bindless descriptor pool.
static void createVkDescriptorSetLayouts(vector<void*>& descriptorSetLayouts, vector<void*>& descriptorPools,
	const Pipeline::Uniforms& pipelineUniforms, const absl::flat_hash_map<string, vk::Sampler>& immutableSamplers,
	const fs::path& pipelinePath, uint8 descriptorSetCount)
{
	auto vulkanAPI = VulkanAPI::getInstance();
	vector<vk::DescriptorSetLayoutBinding> descriptorSetBindings(pipelineUniforms.size());
	vector<vk::DescriptorBindingFlags> descriptorBindingFlags;
	vector<vk::DescriptorPoolSize> descriptorPoolSizes;
	vector<vector<vk::Sampler>> samplerArrays;
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
					auto& immutableSampler = immutableSamplers.at(uniformPair.first);
					if (pipelineUniform.arraySize > 1)
					{
						// TODO: allow to specify sampler states for separate uniform array elements?
						vector<vk::Sampler> samplers(pipelineUniform.arraySize, immutableSampler);
						samplerArrays.push_back(std::move(samplers));
						descriptorSetBinding.pImmutableSamplers = samplerArrays[samplerArrays.size() - 1].data();
					}
					else descriptorSetBinding.pImmutableSamplers = &immutableSampler;
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
				default: GARDEN_UNREACHABLE();
				}

				if (!pipelineUniform.isMutable && pipelineUniform.isSamplerType)
				{
					auto& immutableSampler = immutableSamplers.at(uniformPair.first);
					vector<vk::Sampler> samplers(maxBindlessCount, immutableSampler);
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

			#if GARDEN_USE_GAPI_VALIDATIONS
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

		#if GARDEN_USE_GAPI_VALIDATIONS
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
 */

//**********************************************************************************************************************
BindlessPool::BindlessPool(ID<Pipeline> pipeline, PipelineType pipelineType, 
	DescriptorSet::Uniforms&& uniforms, DescriptorSet::Samplers&& samplers, uint8 index)
{
	GARDEN_ASSERT(pipeline);

	auto graphicsAPI = GraphicsAPI::getInstance();

	descriptorSet = graphicsAPI->descriptorSetPool.create(pipeline, 
		pipelineType, std::move(uniforms), std::move(samplers), index);
	/*
	auto graphicsAPI = GraphicsAPI::getInstance();
	auto pipelineView = graphicsAPI->getPipelineView(pipelineType, pipeline);
	GARDEN_ASSERT_MSG(!uniforms.empty(), "Assert " + pipelineView->getDebugName());

	uniformData.reserve(uniforms.size());
	for (auto i = uniforms.begin(); i != uniforms.end(); i++)
	{
		auto& resourceSets = i->second.resourceSets;
		GARDEN_ASSERT_MSG(!resourceSets.empty(), "No resource set for bindless uniform [" + i->first + "]");
		auto defaultValue = resourceSets.front().front();
		GARDEN_ASSERT_MSG(defaultValue, "No default resource for bindless uniform [" + i->first + "]");
		resourceSets.front().assign(maxBindlessCount, defaultValue);

		UniformData allocData;
		auto emplaceResult = uniformData.emplace(i->first, std::move(allocData));
		GARDEN_ASSERT_MSG(emplaceResult.second, "Detected memory corruption");
	}

	descriptorSet = graphicsAPI->descriptorSetPool.create(pipeline, 
		pipelineType, std::move(uniforms), std::move(samplers), index);
	*/
}
void BindlessPool::destroy()
{
	GraphicsAPI::getInstance()->descriptorSetPool.destroy(descriptorSet);
	uniformData.clear();
}

//**********************************************************************************************************************
uint32 BindlessPool::allocate(string_view name, ID<Resource> resource, uint64 frameIndex)
{
	GARDEN_ASSERT(!name.empty());
	GARDEN_ASSERT_MSG(resource, "Assert " + string(name));
	GARDEN_ASSERT_MSG(descriptorSet, "Assert " + string(name));

	auto descriptorSetView = GraphicsAPI::getInstance()->descriptorSetPool.get(descriptorSet);
	auto& dsUniforms = descriptorSetView->getUniforms();
	auto uniform = dsUniforms.find(name);
	if (uniform == dsUniforms.end())
		throw GardenError("Missing required descriptor set uniform. (" + string(name) + ")");

	auto& allocData = uniformData.at(name);

	uint32 allocation = UINT32_MAX;
	if (!allocData.freeAllocs.empty())
	{
		auto& freeAllocs = allocData.freeAllocs;
		auto freeAllocData = freeAllocs.data();
		auto freeAllocCount = freeAllocs.size();

		for (psize i = 0; i < freeAllocCount; i++)
		{
			const auto& freeAlloc = freeAllocData[i];
			if (frameIndex < freeAlloc.second)
				continue;
			allocation = freeAlloc.first;
			freeAllocCount--;
			freeAllocData[i] = freeAllocData[freeAllocCount];
			break;
		}
		freeAllocs.resize(freeAllocCount);
	}
	if (allocation == UINT32_MAX)
	{
		GARDEN_ASSERT_MSG(allocData.occupancy < uniform->second.resourceSets.front().size(),
			"Out of maximum bindless descriptor set count. (" + string(name) + ")");
		allocation = allocData.occupancy++;
	}

	auto& resourceSet = uniform->second.resourceSets.front();
	GARDEN_ASSERT_MSG(allocation < resourceSet.size(), "Assert " + string(name));
	// resourceSet[allocation] = resource;
	return allocation;
}
void BindlessPool::update(string_view name, uint32 allocation, ID<Resource> resource, uint64 frameIndex)
{
	GARDEN_ASSERT(!name.empty());
	GARDEN_ASSERT_MSG(resource, "Assert " + string(name));
	GARDEN_ASSERT_MSG(descriptorSet, "Assert " + string(name));

	auto descriptorSetView = GraphicsAPI::getInstance()->descriptorSetPool.get(descriptorSet);
	auto& dsUniforms = descriptorSetView->getUniforms();
	auto uniform = dsUniforms.find(name);
	if (uniform == dsUniforms.end())
		throw GardenError("Missing required descriptor set uniform. (" + string(name) + ")");

	auto& resourceSet = uniform->second.resourceSets.front();
	GARDEN_ASSERT_MSG(allocation < resourceSet.size(), "Assert " + string(name));
	// resourceSet[allocation] = resource;
}
void BindlessPool::free(string_view name, uint32 allocation, uint64 frameIndex)
{
	GARDEN_ASSERT(!name.empty());
	GARDEN_ASSERT_MSG(descriptorSet, "Assert " + string(name));

	if (allocation == UINT32_MAX)
		return;

	auto descriptorSetView = GraphicsAPI::getInstance()->descriptorSetPool.get(descriptorSet);
	auto& dsUniforms = descriptorSetView->getUniforms();
	auto uniform = dsUniforms.find(name);
	if (uniform == dsUniforms.end())
		throw GardenError("Missing required descriptor set uniform. (" + string(name) + ")");

	auto& allocData = uniformData.at(name);
	#if GARDEN_DEBUG
	GARDEN_ASSERT_MSG(allocation < allocData.occupancy, "Assert " + string(name));
	for (auto freeAlloc : allocData.freeAllocs)
	{
		GARDEN_ASSERT_MSG(allocation != freeAlloc.first, 
			"Already freed allocation. (" + string(name) + ")");
	}
	#endif

	auto& resourceSet = uniform->second.resourceSets.front();
	GARDEN_ASSERT_MSG(allocation < resourceSet.size(), "Assert " + string(name));
	// resourceSet[allocation] = {};
	allocData.freeAllocs.emplace_back(allocation, frameIndex + (inFlightCount + 1));
}

void BindlessPool::flush(string_view name)
{
	GARDEN_ASSERT_MSG(descriptorSet, "Assert " + string(name));
	auto descriptorSetView = GraphicsAPI::getInstance()->descriptorSetPool.get(descriptorSet);
	auto& allocData = uniformData.at(name);
	descriptorSetView->updateResources(name, allocData.occupancy);
}