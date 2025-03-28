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

#include "garden/graphics/descriptor-set.hpp"
#include "garden/graphics/vulkan/api.hpp"
#include "garden/graphics/pipeline/graphics.hpp"

using namespace math;
using namespace garden;
using namespace garden::graphics;

#if GARDEN_DEBUG || GARDEN_EDITOR
uint32 DescriptorSet::combinedSamplerCount = 0;
uint32 DescriptorSet::uniformBufferCount = 0;
uint32 DescriptorSet::storageImageCount = 0;
uint32 DescriptorSet::storageBufferCount = 0;
uint32 DescriptorSet::inputAttachmentCount = 0;
#endif

//**********************************************************************************************************************
static void* createVkDescriptorSet(ID<Pipeline> pipeline, PipelineType pipelineType, 
	const map<string, DescriptorSet::Uniform>& uniforms, uint8 index)
{
	auto vulkanAPI = VulkanAPI::get();
	auto pipelineView = vulkanAPI->getPipelineView(pipelineType, pipeline);
	vk::DescriptorSetLayout descriptorSetLayout = (VkDescriptorSetLayout)
		PipelineExt::getDescriptorSetLayouts(**pipelineView)[index];
	vk::DescriptorPool descriptorPool = (VkDescriptorPool)PipelineExt::getDescriptorPools(**pipelineView)[index];
	auto setCount = (uint32)uniforms.begin()->second.resourceSets.size();
	GARDEN_ASSERT(descriptorSetLayout);

	vk::DescriptorSetAllocateInfo allocateInfo(descriptorPool ?
		descriptorPool : vulkanAPI->descriptorPool, setCount);

	void* instance = nullptr;
	if (setCount > 1)
	{
		auto descriptorSets = malloc<vk::DescriptorSet>(setCount);
		vulkanAPI->descriptorSetLayouts.assign(setCount, descriptorSetLayout);
		allocateInfo.pSetLayouts = vulkanAPI->descriptorSetLayouts.data();
		auto allocateResult = vulkanAPI->device.allocateDescriptorSets(&allocateInfo, descriptorSets);
		vk::detail::resultCheck(allocateResult, "vk::Device::allocateDescriptorSets");
		instance = descriptorSets;
	}
	else
	{
		allocateInfo.pSetLayouts = &descriptorSetLayout;
		auto allocateResult = vulkanAPI->device.allocateDescriptorSets(&allocateInfo, (vk::DescriptorSet*)&instance);
		vk::detail::resultCheck(allocateResult, "vk::Device::allocateDescriptorSets");
	}

	#if GARDEN_DEBUG || GARDEN_EDITOR
	const auto& pipelineUniforms = pipelineView->getUniforms();
	for	(const auto& pair : pipelineUniforms)
	{
		if (pair.second.descriptorSetIndex != index)
			continue;
		
		auto descriptorType = toVkDescriptorType(pair.second.type);
		switch (descriptorType)
		{
		case vk::DescriptorType::eCombinedImageSampler: DescriptorSet::combinedSamplerCount++; break;
		case vk::DescriptorType::eStorageImage: DescriptorSet::storageImageCount++; break;
		case vk::DescriptorType::eInputAttachment: DescriptorSet::inputAttachmentCount++; break;
		case vk::DescriptorType::eUniformBuffer: DescriptorSet::uniformBufferCount++; break;
		case vk::DescriptorType::eStorageBuffer: DescriptorSet::storageBufferCount++; break;
		default: abort();
		}
	}
	#endif

	return instance;
}

static void destroyVkDescriptorSet(void* instance, ID<Pipeline> pipeline, PipelineType pipelineType, 
	const map<string, DescriptorSet::Uniform>& uniforms, uint8 index)
{
	auto vulkanAPI = VulkanAPI::get();
	if (vulkanAPI->forceResourceDestroy)
	{
		if (uniforms.begin()->second.resourceSets.size() > 1)
			free(instance);
	}
	else
	{
		auto pipelineView = vulkanAPI->getPipelineView(pipelineType, pipeline);
		auto isBindless = PipelineExt::getDescriptorPools(**pipelineView)[index];

		if (isBindless)
		{
			if (uniforms.begin()->second.resourceSets.size() > 1)
				free(instance);
		}
		else
		{
			auto setCount = uniforms.begin()->second.resourceSets.size();
			vulkanAPI->destroyResource(GraphicsAPI::DestroyResourceType::DescriptorSet,
				instance, nullptr, setCount > 1 ? setCount : 0);
		}
	}

	#if GARDEN_DEBUG || GARDEN_EDITOR
	auto pipelineView = vulkanAPI->getPipelineView(pipelineType, pipeline);
	const auto& pipelineUniforms = pipelineView->getUniforms();

	for	(const auto& pair : pipelineUniforms)
	{
		if (pair.second.descriptorSetIndex != index)
			continue;
		
		auto descriptorType = toVkDescriptorType(pair.second.type);
		switch (descriptorType)
		{
		case vk::DescriptorType::eCombinedImageSampler: DescriptorSet::combinedSamplerCount--; break;
		case vk::DescriptorType::eStorageImage: DescriptorSet::storageImageCount--; break;
		case vk::DescriptorType::eInputAttachment: DescriptorSet::inputAttachmentCount--; break;
		case vk::DescriptorType::eUniformBuffer: DescriptorSet::uniformBufferCount--; break;
		case vk::DescriptorType::eStorageBuffer: DescriptorSet::storageBufferCount--; break;
		default: abort();
		}
	}
	#endif
}

//**********************************************************************************************************************
static void recreateVkDescriptorSet(const map<string, DescriptorSet::Uniform>& oldUniforms, 
	const map<string, DescriptorSet::Uniform>& newUniforms, uint8 index, void*& instance, 
	vk::DescriptorPool descriptorPool, vk::DescriptorSetLayout descriptorSetLayout,
	const map<string, Pipeline::Uniform>& pipelineUniforms)
{
	auto vulkanAPI = VulkanAPI::get();
	#if GARDEN_DEBUG
	if (!descriptorPool)
	{
		for (const auto& pair : newUniforms)
		{
			for (const auto& resourceArray : pair.second.resourceSets)
			{
				for (auto resource : resourceArray)
					GARDEN_ASSERT(resource);
			}
		}
	}
	#endif

	// Note: Checks if descriptor set is just now created. 
	auto oldSetCount = !oldUniforms.empty() ? (uint32)oldUniforms.begin()->second.resourceSets.size() : (uint32)0;
	auto newSetCount = (uint32)newUniforms.begin()->second.resourceSets.size();

	if (oldSetCount != 0 && newSetCount != oldSetCount)
	{
		vk::DescriptorSetAllocateInfo allocateInfo(descriptorPool ?
			descriptorPool : vulkanAPI->descriptorPool, newSetCount);

		// TODO: We can only allocate or free required array part.
		// 		 But I'm not sure if it faster or better.
		if (oldSetCount > 1)
		{
			vulkanAPI->device.freeDescriptorSets(descriptorPool ? descriptorPool : 
				vulkanAPI->descriptorPool, oldSetCount, (vk::DescriptorSet*)instance);
			instance = realloc<vk::DescriptorSet>((vk::DescriptorSet*)instance, newSetCount);

			vulkanAPI->descriptorSetLayouts.assign(newSetCount, descriptorSetLayout);
			allocateInfo.pSetLayouts = vulkanAPI->descriptorSetLayouts.data();
			auto allocateResult = vulkanAPI->device.allocateDescriptorSets(
				&allocateInfo, (vk::DescriptorSet*)instance);
			vk::detail::resultCheck(allocateResult, "vk::Device::allocateDescriptorSets");
		}
		else
		{
			vulkanAPI->device.freeDescriptorSets(descriptorPool ?
				descriptorPool : vulkanAPI->descriptorPool, 1, (vk::DescriptorSet*)&instance);

			allocateInfo.pSetLayouts = &descriptorSetLayout;
			auto allocateResult = vulkanAPI->device.allocateDescriptorSets(
				&allocateInfo, (vk::DescriptorSet*)&instance);
			vk::detail::resultCheck(allocateResult, "vk::Device::allocateDescriptorSets");
		}
	}

	vk::WriteDescriptorSet writeDescriptorSet;
	auto uniformCount = pipelineUniforms.size();
	auto bufferSize = uniformCount * newSetCount;
	auto instances = newSetCount > 1 ? (vk::DescriptorSet*)instance : (vk::DescriptorSet*)&instance;
	vulkanAPI->writeDescriptorSets.reserve(bufferSize);
	vulkanAPI->descriptorImageInfos.reserve(bufferSize);
	vulkanAPI->descriptorBufferInfos.reserve(bufferSize);

	for	(const auto& pair : pipelineUniforms)
	{
		if (pair.second.descriptorSetIndex != index)
			continue;

		const auto& pipelineUniform = pair.second;
		const auto& dsUniform = newUniforms.at(pair.first);
		auto uniformType = pipelineUniform.type;
		writeDescriptorSet.dstBinding = (uint32)pipelineUniform.bindingIndex;
		writeDescriptorSet.descriptorType = toVkDescriptorType(uniformType);

		if (isSamplerType(uniformType) || isImageType(uniformType) ||
			uniformType == GslUniformType::SubpassInput)
		{
			vk::DescriptorImageInfo imageInfo({}, {}, isImageType(uniformType) ? 
				vk::ImageLayout::eGeneral : vk::ImageLayout::eShaderReadOnlyOptimal);
			writeDescriptorSet.pBufferInfo = nullptr;

			for (uint32 i = 0; i < newSetCount; i++)
			{
				uint32 resourceCount = 0;
				const auto& resourceArray = dsUniform.resourceSets[i];

				for (uint32 j = 0; j < (uint32)resourceArray.size(); j++)
				{
					if (!resourceArray[j])
						continue;

					auto imageView = vulkanAPI->imageViewPool.get(ID<ImageView>(resourceArray[j]));
					imageInfo.imageView = (VkImageView)ResourceExt::getInstance(**imageView);
					vulkanAPI->descriptorImageInfos.push_back(imageInfo);
					resourceCount++;
				}
				
				if (resourceCount == 0)
					continue;

				writeDescriptorSet.dstSet = instances[i];
				writeDescriptorSet.descriptorCount = (uint32)resourceArray.size();
				writeDescriptorSet.pImageInfo = &vulkanAPI->descriptorImageInfos[(uint32)(
					vulkanAPI->descriptorImageInfos.size() - resourceCount)];
				vulkanAPI->writeDescriptorSets.push_back(writeDescriptorSet);
			}
		}
		else if (isBufferType(uniformType))
		{
			writeDescriptorSet.pImageInfo = nullptr;

			for (uint32 i = 0; i < newSetCount; i++)
			{
				uint32 resourceCount = 0;
				const auto& resourceArray = dsUniform.resourceSets[i];

				for (uint32 j = 0; j < (uint32)resourceArray.size(); j++)
				{
					if (!resourceArray[j])
						continue;

					auto buffer = vulkanAPI->bufferPool.get(ID<Buffer>(resourceArray[j]));
					// TODO: support part of the buffer mapping?
					vulkanAPI->descriptorBufferInfos.emplace_back((VkBuffer)
						ResourceExt::getInstance(**buffer), 0, buffer->getBinarySize());
					resourceCount++;
				}
				
				if (resourceCount == 0)
					continue;

				writeDescriptorSet.dstSet = instances[i];
				writeDescriptorSet.descriptorCount = (uint32)resourceArray.size();
				writeDescriptorSet.pBufferInfo = &vulkanAPI->descriptorBufferInfos[(uint32)(
					vulkanAPI->descriptorBufferInfos.size() - resourceArray.size())];
				vulkanAPI->writeDescriptorSets.push_back(writeDescriptorSet);
			}
		}
		else abort();
	}

	if (!vulkanAPI->writeDescriptorSets.empty())
	{
		vulkanAPI->device.updateDescriptorSets(vulkanAPI->writeDescriptorSets, {});

		vulkanAPI->writeDescriptorSets.clear();
		vulkanAPI->descriptorImageInfos.clear();
		vulkanAPI->descriptorBufferInfos.clear();
	}
}

//**********************************************************************************************************************
static void updateVkDescriptorSetUniform(void* instance, const Pipeline::Uniform& pipelineUniform, 
	const DescriptorSet::Uniform& uniform, uint32 elementOffset)
{
	auto vulkanAPI = VulkanAPI::get();
	auto uniformType = pipelineUniform.type;

	vk::WriteDescriptorSet writeDescriptorSet(
		(VkDescriptorSet)instance, pipelineUniform.bindingIndex, elementOffset, 
		(uint32)uniform.resourceSets[0].size(), toVkDescriptorType(uniformType));

	if (isSamplerType(uniformType) || isImageType(uniformType))
	{
		vk::DescriptorImageInfo imageInfo({}, {}, isImageType(uniformType) ? 
			vk::ImageLayout::eGeneral : vk::ImageLayout::eShaderReadOnlyOptimal);
		writeDescriptorSet.pBufferInfo = nullptr;

		const auto& resourceArray = uniform.resourceSets[0];
		for (uint32 i = 0; i < (uint32)resourceArray.size(); i++)
		{
			if (!resourceArray[i])
				continue;

			auto imageView = vulkanAPI->imageViewPool.get(ID<ImageView>(resourceArray[i]));
			imageInfo.imageView = (VkImageView)ResourceExt::getInstance(**imageView);
			vulkanAPI->descriptorImageInfos.push_back(imageInfo);
		}

		writeDescriptorSet.pImageInfo = &vulkanAPI->descriptorImageInfos[(uint32)(
			vulkanAPI->descriptorImageInfos.size() - resourceArray.size())];
	}
	else if (isBufferType(uniformType))
	{
		writeDescriptorSet.pImageInfo = nullptr;

		const auto& resourceArray = uniform.resourceSets[0];
		for (uint32 i = 0; i < (uint32)resourceArray.size(); i++)
		{
			if (!resourceArray[i])
				continue;

			auto buffer = vulkanAPI->bufferPool.get(ID<Buffer>(resourceArray[0]));
			vulkanAPI->descriptorBufferInfos.emplace_back((VkBuffer)
				ResourceExt::getInstance(**buffer), 0, buffer->getBinarySize());
		}

		writeDescriptorSet.pBufferInfo = &vulkanAPI->descriptorBufferInfos[(uint32)(
			vulkanAPI->descriptorBufferInfos.size() - resourceArray.size())];
	}
	else abort();

	if (!vulkanAPI->descriptorImageInfos.empty() || !vulkanAPI->descriptorBufferInfos.empty())
	{
		vulkanAPI->device.updateDescriptorSets(1, &writeDescriptorSet, 0, nullptr);

		vulkanAPI->descriptorImageInfos.clear();
		vulkanAPI->descriptorBufferInfos.clear();
	}
}

//**********************************************************************************************************************
DescriptorSet::DescriptorSet(ID<Pipeline> pipeline, PipelineType pipelineType, map<string, Uniform>&& uniforms, 
	uint8 index) : pipeline(pipeline), pipelineType(pipelineType), index(index)
{
	GARDEN_ASSERT(pipeline);
	GARDEN_ASSERT(!uniforms.empty());

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		this->instance = createVkDescriptorSet(pipeline, pipelineType, uniforms, index);
	else abort();

	recreate(std::move(uniforms));
}

bool DescriptorSet::destroy()
{
	if (!instance || readyLock > 0)
		return false;

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		destroyVkDescriptorSet(instance, pipeline, pipelineType, uniforms, index);
	else abort();

	instance = nullptr;
	return true;
}

//**********************************************************************************************************************
void DescriptorSet::recreate(map<string, Uniform>&& uniforms)
{
	GARDEN_ASSERT(this->uniforms.size() == 0 || uniforms.size() == this->uniforms.size());

	auto graphicsAPI = GraphicsAPI::get();
	auto pipelineView = graphicsAPI->getPipelineView(pipelineType, pipeline);
	const auto& pipelineUniforms = pipelineView->getUniforms();

	#if GARDEN_DEBUG
	const auto& firstUniform = uniforms.begin()->second;
	auto setCount = firstUniform.resourceSets.size();

	for (const auto& pair : uniforms)
	{
		GARDEN_ASSERT(!pair.first.empty());
		GARDEN_ASSERT(setCount == pair.second.resourceSets.size());
	}

	auto maxBindlessCount = pipelineView->getMaxBindlessCount();
	auto newSetCount = (uint32)uniforms.begin()->second.resourceSets.size();

	for	(const auto& pair : pipelineUniforms)
	{
		if (pair.second.descriptorSetIndex != index)
			continue;

		if (uniforms.find(pair.first) == uniforms.end())
			throw GardenError("Missing required pipeline uniform. (" + pair.first + ")");

		for (uint32 i = 0; i < newSetCount; i++)
		{
			const auto& pipelineUniform = pair.second;
			const auto& dsUniform = uniforms.at(pair.first);
			const auto& resourceArray = dsUniform.resourceSets[i];
			auto uniformType = pipelineUniform.type;

			if (pipelineUniform.arraySize > 0)
			{
				GARDEN_ASSERT(resourceArray.size() == pipelineUniform.arraySize);
			}
			else
			{
				GARDEN_ASSERT(resourceArray.size() == maxBindlessCount);
			}

			for (uint32 j = 0; j < (uint32)resourceArray.size(); j++)
			{
				if (isSamplerType(uniformType) || isImageType(uniformType) ||
					uniformType == GslUniformType::SubpassInput)
				{
					GARDEN_ASSERT(dsUniform.getDebugType() == typeid(ImageView));
					auto imageView = graphicsAPI->imageViewPool.get(ID<ImageView>(resourceArray[j]));
					GARDEN_ASSERT(toImageType(uniformType) == imageView->getType());

					auto image = graphicsAPI->imagePool.get(imageView->getImage());
					if (isSamplerType(uniformType))
					{
						GARDEN_ASSERT(hasAnyFlag(image->getBind(), Image::Bind::Sampled));
					}
					else if (isImageType(uniformType))
					{
						GARDEN_ASSERT(hasAnyFlag(image->getBind(), Image::Bind::Storage));
					}
					else
					{
						GARDEN_ASSERT(hasAnyFlag(image->getBind(), Image::Bind::InputAttachment));
					}
				}
				else if (isBufferType(uniformType))
				{
					GARDEN_ASSERT(dsUniform.getDebugType() == typeid(Buffer));
					auto buffer = graphicsAPI->bufferPool.get(ID<Buffer>(resourceArray[j]));

					if (uniformType == GslUniformType::UniformBuffer)
					{
						GARDEN_ASSERT(hasAnyFlag(buffer->getBind(), Buffer::Bind::Uniform));
					}
					else
					{
						GARDEN_ASSERT(hasAnyFlag(buffer->getBind(), Buffer::Bind::Storage));
					}
				}
				else abort();
			}
		}
	}
	#endif

	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		vk::DescriptorPool descriptorPool = (VkDescriptorPool)
			PipelineExt::getDescriptorPools(**pipelineView)[index];
		vk::DescriptorSetLayout descriptorSetLayout = (VkDescriptorSetLayout)
			PipelineExt::getDescriptorSetLayouts(**pipelineView)[index];
		recreateVkDescriptorSet(this->uniforms, uniforms, index, instance, 
			descriptorPool, descriptorSetLayout, pipelineUniforms);
	}
	else abort();

	this->uniforms = std::move(uniforms);
}

//**********************************************************************************************************************
void DescriptorSet::updateUniform(const string& name, const Uniform& uniform, uint32 elementOffset)
{
	GARDEN_ASSERT(!name.empty());
	GARDEN_ASSERT(uniform.resourceSets.size() == 1);
	// TODO: Maybe allow to specific target descriptor set index in the DS array?

	auto graphicsAPI = GraphicsAPI::get();
	auto pipelineView = graphicsAPI->getPipelineView(pipelineType, pipeline);
	const auto& pipelineUniforms = pipelineView->getUniforms();
	const auto& pipelineUniform = pipelineUniforms.at(name);
	GARDEN_ASSERT(uniform.resourceSets[0].size() + elementOffset <= pipelineView->getMaxBindlessCount());

	#if GARDEN_DEBUG
	if (pipelineUniforms.find(name) == pipelineUniforms.end())
		throw GardenError("Missing required pipeline uniform. (" + name + ")");

	auto uniformType = pipelineUniform.type;
	if (isSamplerType(uniformType) || isImageType(uniformType))
	{
		GARDEN_ASSERT(uniform.getDebugType() == typeid(ImageView));
		const auto& resourceArray = uniform.resourceSets[0];

		for (uint32 i = 0; i < (uint32)resourceArray.size(); i++)
		{
			if (!resourceArray[i])
				continue;

			if (isSamplerType(uniformType) || isImageType(uniformType))
			{
				auto imageView = graphicsAPI->imageViewPool.get(ID<ImageView>(resourceArray[i]));
				GARDEN_ASSERT(toImageType(uniformType) == imageView->getType());

				auto image = graphicsAPI->imagePool.get(imageView->getImage());
				if (isSamplerType(uniformType))
				{
					GARDEN_ASSERT(hasAnyFlag(image->getBind(), Image::Bind::Sampled));
				}
				else
				{
					GARDEN_ASSERT(hasAnyFlag(image->getBind(), Image::Bind::Storage));
				}
			}
			else if (isBufferType(uniformType))
			{
				auto buffer = graphicsAPI->bufferPool.get(ID<Buffer>(resourceArray[0]));
				if (uniformType == GslUniformType::UniformBuffer)
				{
					GARDEN_ASSERT(hasAnyFlag(buffer->getBind(), Buffer::Bind::Uniform));
				}
				else
				{
					GARDEN_ASSERT(hasAnyFlag(buffer->getBind(), Buffer::Bind::Storage));
				}
			}
			else abort();
		}
	}
	#endif

	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
		updateVkDescriptorSetUniform(instance, pipelineUniform, uniform, elementOffset);
	else abort();

	memcpy(this->uniforms.at(name).resourceSets[0].data() + elementOffset, 
		uniform.resourceSets[0].data(), uniform.resourceSets[0].size() * sizeof(ID<Resource>));
}

#if GARDEN_DEBUG
//**********************************************************************************************************************
void DescriptorSet::setDebugName(const string& name)
{
	Resource::setDebugName(name);

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		if (!vulkanAPI->hasDebugUtils)
			return;

		vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eDescriptorSet, 0);
		auto setCount = (uint32)uniforms.begin()->second.resourceSets.size();

		if (setCount > 1)
		{
			auto instances = (void**)instance;
			for (uint32 i = 0; i < setCount; i++)
			{
				nameInfo.objectHandle = (uint64)instances[i];
				auto itemName = name + to_string(i);
				nameInfo.pObjectName = itemName.c_str();
				vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo);
			}
		}
		else
		{
			nameInfo.objectHandle = (uint64)instance;
			nameInfo.pObjectName = name.c_str();
			vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo);
		}
	}
	else abort();
}
#endif