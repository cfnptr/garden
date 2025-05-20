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

using namespace math;
using namespace garden;
using namespace garden::graphics;

#if GARDEN_DEBUG || GARDEN_EDITOR
uint32 DescriptorSet::combinedSamplerCount = 0;
uint32 DescriptorSet::uniformBufferCount = 0;
uint32 DescriptorSet::storageImageCount = 0;
uint32 DescriptorSet::storageBufferCount = 0;
uint32 DescriptorSet::inputAttachmentCount = 0;
uint32 DescriptorSet::accelStructureCount = 0;
#endif

//**********************************************************************************************************************
static void* createVkDescriptorSet(ID<Pipeline> pipeline, PipelineType pipelineType, 
	const DescriptorSet::Uniforms& uniforms, uint8 index)
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
		case vk::DescriptorType::eCombinedImageSampler:
			DescriptorSet::combinedSamplerCount += setCount;
			GARDEN_ASSERT(DescriptorSet::combinedSamplerCount < GARDEN_DS_POOL_COMBINED_SAMPLER_COUNT);
			break;
		case vk::DescriptorType::eUniformBuffer:
			DescriptorSet::uniformBufferCount += setCount;
			GARDEN_ASSERT(DescriptorSet::uniformBufferCount < GARDEN_DS_POOL_UNIFORM_BUFFER_COUNT);
			break;
		case vk::DescriptorType::eStorageImage:
			DescriptorSet::storageImageCount += setCount;
			GARDEN_ASSERT(DescriptorSet::storageImageCount < GARDEN_DS_POOL_STORAGE_IMAGE_COUNT);
			break;
		case vk::DescriptorType::eStorageBuffer:
			DescriptorSet::storageBufferCount += setCount;
			GARDEN_ASSERT(DescriptorSet::storageBufferCount < GARDEN_DS_POOL_STORAGE_BUFFER_COUNT);
			break;
		case vk::DescriptorType::eInputAttachment:
			DescriptorSet::inputAttachmentCount += setCount;
			GARDEN_ASSERT(DescriptorSet::inputAttachmentCount < GARDEN_DS_POOL_INPUT_ATTACHMENT_COUNT);
			break;
		case vk::DescriptorType::eAccelerationStructureKHR:
			DescriptorSet::accelStructureCount += setCount;
			GARDEN_ASSERT(DescriptorSet::accelStructureCount < GARDEN_DS_POOL_ACCEL_STRUCTURE_COUNT);
			break;
		default: abort();
		}
	}
	#endif

	return instance;
}

static void destroyVkDescriptorSet(void* instance, ID<Pipeline> pipeline, 
	PipelineType pipelineType, const DescriptorSet::Uniforms& uniforms, uint8 index)
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
			auto setCount = (uint32)uniforms.begin()->second.resourceSets.size();
			vulkanAPI->destroyResource(GraphicsAPI::DestroyResourceType::DescriptorSet,
				instance, nullptr, setCount > 1 ? setCount : 0);
		}
	}

	#if GARDEN_DEBUG || GARDEN_EDITOR
	auto pipelineView = vulkanAPI->getPipelineView(pipelineType, pipeline);
	const auto& pipelineUniforms = pipelineView->getUniforms();
	auto setCount = (uint32)uniforms.begin()->second.resourceSets.size();

	for	(const auto& pair : pipelineUniforms)
	{
		if (pair.second.descriptorSetIndex != index)
			continue;
		
		auto descriptorType = toVkDescriptorType(pair.second.type);
		switch (descriptorType)
		{
		case vk::DescriptorType::eCombinedImageSampler: DescriptorSet::combinedSamplerCount -= setCount; break;
		case vk::DescriptorType::eUniformBuffer: DescriptorSet::uniformBufferCount -= setCount; break;
		case vk::DescriptorType::eStorageImage: DescriptorSet::storageImageCount -= setCount; break;
		case vk::DescriptorType::eStorageBuffer: DescriptorSet::storageBufferCount -= setCount; break;
		case vk::DescriptorType::eInputAttachment: DescriptorSet::inputAttachmentCount -= setCount; break;
		case vk::DescriptorType::eAccelerationStructureKHR: DescriptorSet::accelStructureCount -= setCount; break;
		default: abort();
		}
	}
	#endif
}

//**********************************************************************************************************************
static void recreateVkDescriptorSet(const DescriptorSet::Uniforms& oldUniforms, 
	const DescriptorSet::Uniforms& newUniforms, const DescriptorSet::Samplers& samplers, uint8 index, 
	void*& instance, vk::DescriptorPool descriptorPool, vk::DescriptorSetLayout descriptorSetLayout,
	const Pipeline::Uniforms& pipelineUniforms)
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
	uint32 imageInfoCapacity = 0, bufferInfoCapacity = 0, asInfoCapacity = 0, tlasCount = 0;

	for	(const auto& pair : pipelineUniforms)
	{
		if (pair.second.descriptorSetIndex != index)
			continue;

		const auto& dsUniform = newUniforms.at(pair.first);
		auto uniformType = pair.second.type;

		if (isSamplerType(uniformType) || isImageType(uniformType) ||
			uniformType == GslUniformType::SubpassInput)
		{
			for (uint32 i = 0; i < newSetCount; i++)
				imageInfoCapacity += (uint32)dsUniform.resourceSets[i].size();
		}
		else if (isBufferType(uniformType))
		{
			for (uint32 i = 0; i < newSetCount; i++)
				bufferInfoCapacity += (uint32)dsUniform.resourceSets[i].size();
		}
		else if (uniformType == GslUniformType::AccelerationStructure)
		{
			for (uint32 i = 0; i < newSetCount; i++)
				asInfoCapacity += (uint32)dsUniform.resourceSets[i].size();
			tlasCount++;
		}
		else abort();
	}

	if (vulkanAPI->descriptorImageInfos.capacity() < imageInfoCapacity)
		vulkanAPI->descriptorImageInfos.reserve(imageInfoCapacity);
	if (vulkanAPI->descriptorBufferInfos.capacity() < bufferInfoCapacity)
		vulkanAPI->descriptorBufferInfos.reserve(bufferInfoCapacity);
	if (vulkanAPI->asDescriptorInfos.capacity() < asInfoCapacity)
		vulkanAPI->asDescriptorInfos.reserve(asInfoCapacity);
	if (vulkanAPI->asWriteDescriptorSets.capacity() < tlasCount)
		vulkanAPI->asWriteDescriptorSets.reserve(tlasCount);

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
			writeDescriptorSet.pNext = nullptr;

			if (pipelineUniform.isMutable)
			{
				auto sampler = samplers.at(pair.first);
				auto samplerView = vulkanAPI->samplerPool.get(sampler);
				imageInfo.sampler = (VkSampler)ResourceExt::getInstance(**samplerView);
			}
			else
			{
				// You should add 'mutable' keyword to the uniform to use dynamic samplers.
				GARDEN_ASSERT(samplers.find(pair.first) == samplers.end());
			}

			for (uint32 i = 0; i < newSetCount; i++)
			{
				const auto& resourceArray = dsUniform.resourceSets[i];
				auto infoOffset = vulkanAPI->descriptorImageInfos.size();
				auto hasAnyResource = false;

				for (auto resource : resourceArray)
				{
					if (resource)
					{
						auto imageView = vulkanAPI->imageViewPool.get(ID<ImageView>(resource));
						imageInfo.imageView = (VkImageView)ResourceExt::getInstance(**imageView);
						hasAnyResource = true;
					}
					else
					{
						imageInfo.imageView = nullptr;
					}
					vulkanAPI->descriptorImageInfos.push_back(imageInfo);
				}
				
				if (!hasAnyResource)
					continue;

				writeDescriptorSet.dstSet = instances[i];
				writeDescriptorSet.descriptorCount = (uint32)resourceArray.size();
				writeDescriptorSet.pImageInfo = &vulkanAPI->descriptorImageInfos[infoOffset];
				vulkanAPI->writeDescriptorSets.push_back(writeDescriptorSet);
			}
		}
		else if (isBufferType(uniformType))
		{
			vk::DescriptorBufferInfo bufferInfo({}, 0, 0);
			writeDescriptorSet.pImageInfo = nullptr;
			writeDescriptorSet.pNext = nullptr;

			for (uint32 i = 0; i < newSetCount; i++)
			{
				const auto& resourceArray = dsUniform.resourceSets[i];
				auto infoOffset = vulkanAPI->descriptorBufferInfos.size();
				auto hasAnyResource = false;

				for (auto resource : resourceArray)
				{
					if (resource)
					{
						auto buffer = vulkanAPI->bufferPool.get(ID<Buffer>(resource));
						bufferInfo.buffer = (VkBuffer)ResourceExt::getInstance(**buffer);
						bufferInfo.range = buffer->getBinarySize(); // TODO: support part of the buffer mapping?
						hasAnyResource = true;
					}
					else
					{
						bufferInfo.buffer = nullptr;
						bufferInfo.range = 0;
					}
					vulkanAPI->descriptorBufferInfos.push_back(bufferInfo);
				}
				
				if (!hasAnyResource)
					continue;

				writeDescriptorSet.dstSet = instances[i];
				writeDescriptorSet.descriptorCount = (uint32)resourceArray.size();
				writeDescriptorSet.pBufferInfo = &vulkanAPI->descriptorBufferInfos[infoOffset];
				vulkanAPI->writeDescriptorSets.push_back(writeDescriptorSet);
			}
		}
		else if (uniformType == GslUniformType::AccelerationStructure)
		{
			writeDescriptorSet.pImageInfo = nullptr;
			writeDescriptorSet.pBufferInfo = nullptr;

			vk::WriteDescriptorSetAccelerationStructureKHR asWriteDescriptorSet;
			for (uint32 i = 0; i < newSetCount; i++)
			{
				const auto& resourceArray = dsUniform.resourceSets[i];
				auto infoOffset = vulkanAPI->asDescriptorInfos.size();
				auto hasAnyResource = false;

				for (auto resource : resourceArray)
				{
					vk::AccelerationStructureKHR accelerationStructure;
					if (resource)
					{
						auto tlas = vulkanAPI->tlasPool.get(ID<Tlas>(resource));
						accelerationStructure = (VkAccelerationStructureKHR)ResourceExt::getInstance(**tlas);
						hasAnyResource = true;
					}
					else
					{
						accelerationStructure = nullptr;
					}

					vulkanAPI->asDescriptorInfos.push_back(accelerationStructure);
				}
				
				if (!hasAnyResource)
					continue;

				asWriteDescriptorSet.accelerationStructureCount = (uint32)resourceArray.size();
				asWriteDescriptorSet.pAccelerationStructures = &vulkanAPI->asDescriptorInfos[infoOffset];
				vulkanAPI->asWriteDescriptorSets.push_back(asWriteDescriptorSet);

				writeDescriptorSet.dstSet = instances[i];
				writeDescriptorSet.descriptorCount = (uint32)resourceArray.size();
				writeDescriptorSet.pNext = &vulkanAPI->asWriteDescriptorSets[
					vulkanAPI->asWriteDescriptorSets.size() - 1];
				vulkanAPI->writeDescriptorSets.push_back(writeDescriptorSet);
			}
		}
	}

	vulkanAPI->descriptorImageInfos.clear();
	vulkanAPI->descriptorBufferInfos.clear();
	vulkanAPI->asDescriptorInfos.clear();

	if (vulkanAPI->writeDescriptorSets.empty())
		return;

	vulkanAPI->device.updateDescriptorSets(vulkanAPI->writeDescriptorSets, {});
	vulkanAPI->writeDescriptorSets.clear();
	vulkanAPI->asWriteDescriptorSets.clear();
}

//**********************************************************************************************************************
static void updateVkDescriptorSetUniform(void* instance, const Pipeline::Uniform& pipelineUniform, 
	const DescriptorSet::Uniform& uniform, uint32 elementOffset, uint32 setIndex)
{
	auto vulkanAPI = VulkanAPI::get();
	auto uniformType = pipelineUniform.type;
	const auto& resourceArray = uniform.resourceSets[setIndex];
	auto descriptorCount = (uint32)resourceArray.size();

	if (isSamplerType(uniformType) || isImageType(uniformType) ||
		uniformType == GslUniformType::SubpassInput)
	{
		if (vulkanAPI->descriptorImageInfos.capacity() < descriptorCount)
			vulkanAPI->descriptorImageInfos.reserve(descriptorCount);
	}
	else if (isBufferType(uniformType))
	{
		if (vulkanAPI->descriptorBufferInfos.capacity() < descriptorCount)
			vulkanAPI->descriptorBufferInfos.reserve(descriptorCount);
	}
	else if (uniformType == GslUniformType::AccelerationStructure)
	{
		if (vulkanAPI->asDescriptorInfos.capacity() < descriptorCount)
			vulkanAPI->asDescriptorInfos.reserve(descriptorCount);
	}
	else abort();

	vk::WriteDescriptorSet writeDescriptorSet((VkDescriptorSet)instance, pipelineUniform.bindingIndex, 
		elementOffset, descriptorCount, toVkDescriptorType(uniformType));
	vk::WriteDescriptorSetAccelerationStructureKHR asWriteDescriptorSet;
	auto hasAnyResource = false;

	if (isSamplerType(uniformType) || isImageType(uniformType))
	{
		vk::DescriptorImageInfo imageInfo({}, {}, isImageType(uniformType) ? 
			vk::ImageLayout::eGeneral : vk::ImageLayout::eShaderReadOnlyOptimal);
		auto infoOffset = vulkanAPI->descriptorBufferInfos.size();

		for (auto resource : resourceArray)
		{
			if (resource)
			{
				auto imageView = vulkanAPI->imageViewPool.get(ID<ImageView>(resource));
				imageInfo.imageView = (VkImageView)ResourceExt::getInstance(**imageView);
				hasAnyResource = true;
			}
			else
			{
				imageInfo.imageView = nullptr;
			}
			vulkanAPI->descriptorImageInfos.push_back(imageInfo);
		}

		writeDescriptorSet.pImageInfo = &vulkanAPI->descriptorImageInfos[infoOffset];
	}
	else if (isBufferType(uniformType))
	{
		vk::DescriptorBufferInfo bufferInfo({}, 0, 0);
		auto infoOffset = vulkanAPI->descriptorBufferInfos.size();

		for (auto resource : resourceArray)
		{
			if (resource)
			{
				auto buffer = vulkanAPI->bufferPool.get(ID<Buffer>(resource));
				bufferInfo.buffer = (VkBuffer)ResourceExt::getInstance(**buffer);
				bufferInfo.range = buffer->getBinarySize();
				hasAnyResource = true;
			}
			else
			{
				bufferInfo.buffer = nullptr;
				bufferInfo.range = 0;
			}
			vulkanAPI->descriptorBufferInfos.push_back(bufferInfo);
		}

		writeDescriptorSet.pBufferInfo = &vulkanAPI->descriptorBufferInfos[infoOffset];
	}
	else if (uniformType == GslUniformType::AccelerationStructure)
	{
		auto infoOffset = vulkanAPI->descriptorBufferInfos.size();

		for (auto resource : resourceArray)
		{
			vk::AccelerationStructureKHR accelerationStructure;
			if (resource)
			{
				auto tlas = vulkanAPI->tlasPool.get(ID<Tlas>(resource));
				accelerationStructure = (VkAccelerationStructureKHR)ResourceExt::getInstance(**tlas);
				hasAnyResource = true;
			}
			else
			{
				accelerationStructure = nullptr;
			}

			vulkanAPI->asDescriptorInfos.push_back(accelerationStructure);
		}

		asWriteDescriptorSet.accelerationStructureCount = descriptorCount;
		asWriteDescriptorSet.pAccelerationStructures = &vulkanAPI->asDescriptorInfos[infoOffset];
	}

	vulkanAPI->descriptorImageInfos.clear();
	vulkanAPI->descriptorBufferInfos.clear();
	vulkanAPI->asDescriptorInfos.clear();

	if (!hasAnyResource)
		return;

	vulkanAPI->device.updateDescriptorSets(1, &writeDescriptorSet, 0, nullptr);
}

//**********************************************************************************************************************
DescriptorSet::DescriptorSet(ID<Pipeline> pipeline, PipelineType pipelineType, Uniforms&& uniforms, 
	Samplers&& samplers, uint8 index) : pipeline(pipeline), pipelineType(pipelineType), index(index)
{
	GARDEN_ASSERT(pipeline);
	GARDEN_ASSERT(!uniforms.empty());

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		this->instance = createVkDescriptorSet(pipeline, pipelineType, uniforms, index);
	else abort();

	recreate(std::move(uniforms), std::move(samplers));
}

bool DescriptorSet::destroy()
{
	if (!instance || busyLock > 0)
		return false;

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		destroyVkDescriptorSet(instance, pipeline, pipelineType, uniforms, index);
	else abort();

	return true;
}

//**********************************************************************************************************************
void DescriptorSet::recreate(Uniforms&& uniforms, Samplers&& samplers)
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

			for (auto resource : resourceArray)
			{
				if (isSamplerType(uniformType) || isImageType(uniformType) ||
					uniformType == GslUniformType::SubpassInput)
				{
					GARDEN_ASSERT(dsUniform.getDebugType() == typeid(ImageView));
					auto imageView = graphicsAPI->imageViewPool.get(ID<ImageView>(resource));
					GARDEN_ASSERT(toImageType(uniformType) == imageView->getType());

					auto image = graphicsAPI->imagePool.get(imageView->getImage());
					if (isSamplerType(uniformType))
					{
						GARDEN_ASSERT(hasAnyFlag(image->getUsage(), Image::Usage::Sampled));
					}
					else if (isImageType(uniformType))
					{
						GARDEN_ASSERT(hasAnyFlag(image->getUsage(), Image::Usage::Storage));
					}
					else
					{
						GARDEN_ASSERT(hasAnyFlag(image->getUsage(), Image::Usage::InputAttachment));
					}
				}
				else if (isBufferType(uniformType))
				{
					GARDEN_ASSERT(dsUniform.getDebugType() == typeid(Buffer));
					auto buffer = graphicsAPI->bufferPool.get(ID<Buffer>(resource));

					if (uniformType == GslUniformType::UniformBuffer)
					{
						GARDEN_ASSERT(hasAnyFlag(buffer->getUsage(), Buffer::Usage::Uniform));
					}
					else
					{
						GARDEN_ASSERT(hasAnyFlag(buffer->getUsage(), Buffer::Usage::Storage));
					}
				}
				else if (uniformType == GslUniformType::AccelerationStructure)
				{
					GARDEN_ASSERT(dsUniform.getDebugType() == typeid(Tlas));
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
		recreateVkDescriptorSet(this->uniforms, uniforms, samplers, index, 
			instance, descriptorPool, descriptorSetLayout, pipelineUniforms);
	}
	else abort();

	this->uniforms = std::move(uniforms);
	this->samplers = std::move(samplers);
}

//**********************************************************************************************************************
void DescriptorSet::updateUniform(string_view name, const Uniform& uniform, uint32 elementIndex, uint32 setIndex)
{
	GARDEN_ASSERT(!name.empty());
	GARDEN_ASSERT(uniform.resourceSets.size() == 1);

	auto graphicsAPI = GraphicsAPI::get();
	auto pipelineView = graphicsAPI->getPipelineView(pipelineType, pipeline);
	const auto& pipelineUniforms = pipelineView->getUniforms();
	auto pipelineUniform = pipelineUniforms.find(name);
	if (pipelineUniform == pipelineUniforms.end())
		throw GardenError("Missing required pipeline uniform. (" + string(name) + ")");
	GARDEN_ASSERT(elementIndex <= pipelineView->getMaxBindlessCount());

	#if GARDEN_DEBUG
	auto uniformType = pipelineUniform->second.type;
	if (isSamplerType(uniformType) || isImageType(uniformType))
	{
		GARDEN_ASSERT(uniform.getDebugType() == typeid(ImageView));
		const auto& resourceArray = uniform.resourceSets[0];

		for (auto resource : resourceArray)
		{
			if (!resource)
				continue;

			if (isSamplerType(uniformType) || isImageType(uniformType))
			{
				auto imageView = graphicsAPI->imageViewPool.get(ID<ImageView>(resource));
				GARDEN_ASSERT(toImageType(uniformType) == imageView->getType());

				auto image = graphicsAPI->imagePool.get(imageView->getImage());
				if (isSamplerType(uniformType))
				{
					GARDEN_ASSERT(hasAnyFlag(image->getUsage(), Image::Usage::Sampled));
				}
				else
				{
					GARDEN_ASSERT(hasAnyFlag(image->getUsage(), Image::Usage::Storage));
				}
			}
			else if (isBufferType(uniformType))
			{
				auto buffer = graphicsAPI->bufferPool.get(ID<Buffer>(resource));
				if (uniformType == GslUniformType::UniformBuffer)
				{
					GARDEN_ASSERT(hasAnyFlag(buffer->getUsage(), Buffer::Usage::Uniform));
				}
				else
				{
					GARDEN_ASSERT(hasAnyFlag(buffer->getUsage(), Buffer::Usage::Storage));
				}
			}
			else if (uniformType == GslUniformType::AccelerationStructure) { }
			else abort();
		}
	}
	#endif

	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
		updateVkDescriptorSetUniform(instance, pipelineUniform->second, uniform, elementIndex, setIndex);
	else abort();

	auto thisUniform = this->uniforms.find(name);
	if (thisUniform == this->uniforms.end())
		throw GardenError("Missing required this pipeline uniform. (" + string(name) + ")");
	memcpy((ID<Resource>*)thisUniform->second.resourceSets[setIndex].data() + elementIndex, 
		uniform.resourceSets[0].data(), uniform.resourceSets[0].size() * sizeof(ID<Resource>));
}

#if GARDEN_DEBUG || GARDEN_EDITOR
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