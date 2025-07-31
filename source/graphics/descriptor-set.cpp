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
	const DescriptorSet::Uniforms& uniforms, uint8 index, uint8& setCount)
{
	auto vulkanAPI = VulkanAPI::get();
	auto pipelineView = vulkanAPI->getPipelineView(pipelineType, pipeline);
	const auto& descriptorSetLayouts = PipelineExt::getDescriptorSetLayouts(**pipelineView);
	GARDEN_ASSERT_MSG(index < descriptorSetLayouts.size(), "Out of pipeline [" + pipelineView->getDebugName() + 
		"] descriptor set count [" + to_string(descriptorSetLayouts.size()) + "]");
	GARDEN_ASSERT_MSG(descriptorSetLayouts[index], "Assert " + pipelineView->getDebugName());

	vk::DescriptorSetLayout descriptorSetLayout = (VkDescriptorSetLayout)descriptorSetLayouts[index];
	vk::DescriptorPool descriptorPool = (VkDescriptorPool)PipelineExt::getDescriptorPools(**pipelineView)[index];
	setCount = (uint32)uniforms.begin()->second.resourceSets.size();

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
	PipelineType pipelineType, const DescriptorSet::Uniforms& uniforms, uint8 index, uint8 setCount)
{
	auto vulkanAPI = VulkanAPI::get();
	auto pipelineView = vulkanAPI->getPipelineView(pipelineType, pipeline);
	auto isBindless = PipelineExt::getDescriptorPools(**pipelineView)[index];

	if (vulkanAPI->forceResourceDestroy)
	{
		if (isBindless)
		{
			if (setCount > 1)
				free(instance);
		}
		else
		{
			vulkanAPI->device.freeDescriptorSets(vulkanAPI->descriptorPool, setCount, 
				setCount > 1 ? (vk::DescriptorSet*)instance : (vk::DescriptorSet*)&instance);
		}
	}
	else
	{
		if (isBindless)
		{
			if (setCount > 1)
				free(instance);
		}
		else
		{
			vulkanAPI->destroyResource(GraphicsAPI::DestroyResourceType::DescriptorSet,
				instance, nullptr, setCount > 1 ? setCount : 0);
		}
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
	const DescriptorSet::Uniforms& newUniforms, const DescriptorSet::Samplers& samplers, 
	uint8 index, void*& instance, vk::DescriptorPool descriptorPool, 
	vk::DescriptorSetLayout descriptorSetLayout, const Pipeline::Uniforms& pipelineUniforms)
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
					GARDEN_ASSERT_MSG(resource, "New descriptor set uniform [" + pair.first + "] resource is null");
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
	auto instances = newSetCount > 1 ? (vk::DescriptorSet*)instance : (vk::DescriptorSet*)&instance;
	uint32 imageInfoCount = 0, bufferInfoCount = 0, asInfoCount = 0, tlasCount = 0;

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
				imageInfoCount += (uint32)dsUniform.resourceSets[i].size();
		}
		else if (isBufferType(uniformType))
		{
			for (uint32 i = 0; i < newSetCount; i++)
				bufferInfoCount += (uint32)dsUniform.resourceSets[i].size();
		}
		else if (uniformType == GslUniformType::AccelerationStructure)
		{
			for (uint32 i = 0; i < newSetCount; i++)
				asInfoCount += (uint32)dsUniform.resourceSets[i].size();
			tlasCount += newSetCount;
		}
		else abort();
	}

	vulkanAPI->descriptorImageInfos.resize(imageInfoCount);
	vulkanAPI->descriptorBufferInfos.resize(bufferInfoCount);
	vulkanAPI->asDescriptorInfos.resize(asInfoCount);
	vulkanAPI->asWriteDescriptorSets.resize(tlasCount);
	imageInfoCount = 0, bufferInfoCount = 0, asInfoCount = 0, tlasCount = 0;

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
				GARDEN_ASSERT_MSG(samplers.find(pair.first) == samplers.end(), 
					"Shader uniform [" + pair.first + "] is not marked as 'mutable'");
			}

			for (uint32 i = 0; i < newSetCount; i++)
			{
				const auto& resourceArray = dsUniform.resourceSets[i];
				auto hasAnyResource = false;

				for (uint32 j = 0; j < (uint32)resourceArray.size(); j++)
				{
					auto resource = resourceArray[j];
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
					vulkanAPI->descriptorImageInfos[imageInfoCount + j] = imageInfo;
				}

				if (!hasAnyResource)
					continue;

				writeDescriptorSet.dstSet = instances[i];
				writeDescriptorSet.descriptorCount = (uint32)resourceArray.size();
				writeDescriptorSet.pImageInfo = &vulkanAPI->descriptorImageInfos[imageInfoCount];
				vulkanAPI->writeDescriptorSets.push_back(writeDescriptorSet);
				imageInfoCount += writeDescriptorSet.descriptorCount;
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
				auto hasAnyResource = false;

				for (uint32 j = 0; j < (uint32)resourceArray.size(); j++)
				{
					auto resource = resourceArray[j];
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
					vulkanAPI->descriptorBufferInfos[bufferInfoCount + j] = bufferInfo;
				}
				
				if (!hasAnyResource)
					continue;

				writeDescriptorSet.dstSet = instances[i];
				writeDescriptorSet.descriptorCount = (uint32)resourceArray.size();
				writeDescriptorSet.pBufferInfo = &vulkanAPI->descriptorBufferInfos[bufferInfoCount];
				vulkanAPI->writeDescriptorSets.push_back(writeDescriptorSet);
				bufferInfoCount += writeDescriptorSet.descriptorCount;
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
				auto hasAnyResource = false;

				for (uint32 j = 0; j < (uint32)resourceArray.size(); j++)
				{
					auto resource = resourceArray[j];
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
					vulkanAPI->asDescriptorInfos[asInfoCount + j] = accelerationStructure;
				}
				
				if (!hasAnyResource)
					continue;

				asWriteDescriptorSet.accelerationStructureCount = (uint32)resourceArray.size();
				asWriteDescriptorSet.pAccelerationStructures = &vulkanAPI->asDescriptorInfos[asInfoCount];
				auto asWriteDescriptorSetPtr = &vulkanAPI->asWriteDescriptorSets[tlasCount++];
				*asWriteDescriptorSetPtr = asWriteDescriptorSet;

				writeDescriptorSet.dstSet = instances[i];
				writeDescriptorSet.descriptorCount = asWriteDescriptorSet.accelerationStructureCount;
				writeDescriptorSet.pNext = asWriteDescriptorSetPtr;
				vulkanAPI->writeDescriptorSets.push_back(writeDescriptorSet);
				asInfoCount += writeDescriptorSet.descriptorCount;
			}
		}
	}

	if (!vulkanAPI->writeDescriptorSets.empty())
		vulkanAPI->device.updateDescriptorSets(vulkanAPI->writeDescriptorSets, {});
	vulkanAPI->writeDescriptorSets.clear();
}

//**********************************************************************************************************************
static void updateVkDescriptorSetResources(void* instance, const Pipeline::Uniform& pipelineUniform, 
	const DescriptorSet::Uniform& dsUniform, uint32 elementCount, uint32 elementOffset, uint32 setIndex)
{
	auto vulkanAPI = VulkanAPI::get();
	auto uniformType = pipelineUniform.type;
	auto resourceArray = dsUniform.resourceSets[setIndex].data();

	if (isSamplerType(uniformType) || isImageType(uniformType) ||
		uniformType == GslUniformType::SubpassInput)
	{
		vulkanAPI->descriptorImageInfos.resize(elementCount);
	}
	else if (isBufferType(uniformType))
	{
		vulkanAPI->descriptorBufferInfos.resize(elementCount);
	}
	else if (uniformType == GslUniformType::AccelerationStructure)
	{
		vulkanAPI->asDescriptorInfos.resize(elementCount);
	}
	else abort();

	vk::WriteDescriptorSet writeDescriptorSet((VkDescriptorSet)instance, 
		pipelineUniform.bindingIndex, elementOffset, elementCount, toVkDescriptorType(uniformType));
	auto count = elementOffset + elementCount;
	auto hasAnyResource = false;

	if (isSamplerType(uniformType) || isImageType(uniformType))
	{
		vk::DescriptorImageInfo imageInfo({}, {}, isImageType(uniformType) ? 
			vk::ImageLayout::eGeneral : vk::ImageLayout::eShaderReadOnlyOptimal);
		for (uint32 i = 0, e = elementOffset; e < count; i++, e++)
		{
			auto resource = resourceArray[e];
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
			vulkanAPI->descriptorImageInfos[i] = imageInfo;
		}

		if (hasAnyResource)
			writeDescriptorSet.pImageInfo = vulkanAPI->descriptorImageInfos.data();
	}
	else if (isBufferType(uniformType))
	{
		vk::DescriptorBufferInfo bufferInfo({}, 0, 0);
		for (uint32 i = 0, e = elementOffset; e < count; i++, e++)
		{
			auto resource = resourceArray[e];
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
			vulkanAPI->descriptorBufferInfos[i] = bufferInfo;
		}

		if (hasAnyResource)
			writeDescriptorSet.pBufferInfo = vulkanAPI->descriptorBufferInfos.data();
	}
	else if (uniformType == GslUniformType::AccelerationStructure)
	{
		for (uint32 i = 0, e = elementOffset; e < count; i++, e++)
		{
			auto resource = resourceArray[e];
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
			vulkanAPI->asDescriptorInfos[i] = accelerationStructure;
		}

		if (hasAnyResource)
		{
			vk::WriteDescriptorSetAccelerationStructureKHR asWriteDescriptorSet;
			asWriteDescriptorSet.accelerationStructureCount = elementCount;
			asWriteDescriptorSet.pAccelerationStructures = vulkanAPI->asDescriptorInfos.data();
			writeDescriptorSet.pNext = &asWriteDescriptorSet;
		}
	}

	if (!vulkanAPI->writeDescriptorSets.empty())
		vulkanAPI->device.updateDescriptorSets(1, &writeDescriptorSet, 0, nullptr);
}

//**********************************************************************************************************************
DescriptorSet::DescriptorSet(ID<Pipeline> pipeline, PipelineType pipelineType, Uniforms&& uniforms, 
	Samplers&& samplers, uint8 index) : pipeline(pipeline), pipelineType(pipelineType), index(index)
{
	GARDEN_ASSERT(pipeline);
	GARDEN_ASSERT(!uniforms.empty());

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		this->instance = createVkDescriptorSet(pipeline, pipelineType, uniforms, index, setCount);
	else abort();

	recreate(std::move(uniforms), std::move(samplers));
}

bool DescriptorSet::destroy()
{
	if (!instance || busyLock > 0)
		return false;

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		destroyVkDescriptorSet(instance, pipeline, pipelineType, uniforms, index, setCount);
	else abort();

	return true;
}

//**********************************************************************************************************************
void DescriptorSet::recreate(Uniforms&& uniforms, Samplers&& samplers)
{
	GARDEN_ASSERT_MSG(this->uniforms.size() == 0 || 
		uniforms.size() == this->uniforms.size(), "Assert " + debugName);

	auto graphicsAPI = GraphicsAPI::get();
	auto pipelineView = graphicsAPI->getPipelineView(pipelineType, pipeline);
	const auto& pipelineUniforms = pipelineView->getUniforms();

	#if GARDEN_DEBUG
	for (const auto& pair : uniforms)
	{
		GARDEN_ASSERT_MSG(setCount == pair.second.resourceSets.size(), "Different descriptor set [" + 
			debugName + "] count and resource sets array size");
		GARDEN_ASSERT_MSG(!pair.first.empty(), "Descriptor set [" + debugName + "] uniform name is empty");
	}

	auto maxBindlessCount = pipelineView->getMaxBindlessCount();
	for	(const auto& pair : pipelineUniforms)
	{
		if (pair.second.descriptorSetIndex != index)
			continue;

		if (uniforms.find(pair.first) == uniforms.end())
			throw GardenError("Missing required pipeline uniform. (" + pair.first + ")");

		for (uint32 i = 0; i < setCount; i++)
		{
			const auto& pipelineUniform = pair.second;
			const auto& dsUniform = uniforms.at(pair.first);
			const auto& resourceArray = dsUniform.resourceSets[i];
			auto uniformType = pipelineUniform.type;

			if (pipelineUniform.arraySize > 0)
			{
				GARDEN_ASSERT_MSG(resourceArray.size() == pipelineUniform.arraySize, "Different descriptor set [" +
					debugName + "] and pipeline [" + pipelineView->getDebugName() + "] array uniform size");
			}
			else
			{
				GARDEN_ASSERT_MSG(resourceArray.size() == maxBindlessCount, "Descriptor set [" + debugName + "] resource "
					"array size is different from pipeline [" + pipelineView->getDebugName() + "] maximum bindless size");
			}

			for (auto resource : resourceArray)
			{
				if (!resource)
					continue;

				if (isSamplerType(uniformType) || isImageType(uniformType) ||
					uniformType == GslUniformType::SubpassInput)
				{
					auto imageView = graphicsAPI->imageViewPool.get(ID<ImageView>(resource));
					GARDEN_ASSERT_MSG(toImageType(uniformType) == imageView->getType(), "Different descriptor set [" +
						debugName + "] and pipeline uniform [" + pair.first + "] types");

					auto image = graphicsAPI->imagePool.get(imageView->getImage());
					if (isSamplerType(uniformType))
					{
						GARDEN_ASSERT_MSG(hasAnyFlag(image->getUsage(), Image::Usage::Sampled), "Missing "
							"descriptor set [" + debugName + "] pipeline uniform [" + pair.first + "] flag");
					}
					else if (isImageType(uniformType))
					{
						GARDEN_ASSERT_MSG(hasAnyFlag(image->getUsage(), Image::Usage::Storage), "Missing "
							"descriptor set [" + debugName + "] pipeline uniform [" + pair.first + "] flag");
					}
					else
					{
						GARDEN_ASSERT_MSG(hasAnyFlag(image->getUsage(), Image::Usage::InputAttachment), "Missing "
							"descriptor set [" + debugName + "] pipeline uniform [" + pair.first + "] flag");
					}
				}
				else if (isBufferType(uniformType))
				{
					auto bufferView = graphicsAPI->bufferPool.get(ID<Buffer>(resource));
					if (uniformType == GslUniformType::UniformBuffer)
					{
						GARDEN_ASSERT_MSG(hasAnyFlag(bufferView->getUsage(), Buffer::Usage::Uniform), "Missing "
							"descriptor set [" + debugName + "] pipeline uniform [" + pair.first + "] flag");
					}
					else
					{
						GARDEN_ASSERT_MSG(hasAnyFlag(bufferView->getUsage(), Buffer::Usage::Storage), "Missing "
							"descriptor set [" + debugName + "] pipeline uniform [" + pair.first + "] flag");
					}
				}
				else if (uniformType == GslUniformType::AccelerationStructure)
				{
					auto tlasView = graphicsAPI->tlasPool.get(ID<Tlas>(resource));
					auto bufferView = graphicsAPI->bufferPool.get(tlasView->getStorageBuffer());
					GARDEN_ASSERT_MSG(hasAnyFlag(bufferView->getUsage(), Buffer::Usage::StorageAS), "Missing "
						"descriptor set [" + debugName + "] pipeline uniform [" + pair.first + "] flag");
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
}

//**********************************************************************************************************************
void DescriptorSet::updateUniform(string_view name, 
	const UniformResource& uniform, uint32 elementIndex, uint32 setIndex)
{
	GARDEN_ASSERT_MSG(!name.empty(), "Assert " + debugName);
	GARDEN_ASSERT_MSG(uniform.resource, "Assert " + debugName);

	auto graphicsAPI = GraphicsAPI::get();
	auto pipelineView = graphicsAPI->getPipelineView(pipelineType, pipeline);
	GARDEN_ASSERT_MSG(elementIndex <= pipelineView->getMaxBindlessCount(), "Assert " + debugName);

	const auto& pipelineUniforms = pipelineView->getUniforms();
	auto pipelineUniform = pipelineUniforms.find(name);
	if (pipelineUniform == pipelineUniforms.end())
		throw GardenError("Missing required pipeline uniform. (" + string(name) + ")");
	auto dsUniform = this->uniforms.find(name);
	if (dsUniform == this->uniforms.end())
		throw GardenError("Missing required descriptor set uniform. (" + string(name) + ")");

	#if GARDEN_DEBUG
	auto uniformType = pipelineUniform->second.type;
	if (isSamplerType(uniformType) || isImageType(uniformType))
	{
		if (isSamplerType(uniformType) || isImageType(uniformType))
		{
			auto imageView = graphicsAPI->imageViewPool.get(ID<ImageView>(uniform.resource));
			GARDEN_ASSERT(toImageType(uniformType) == imageView->getType());

			auto image = graphicsAPI->imagePool.get(imageView->getImage());
			if (isSamplerType(uniformType))
			{
				GARDEN_ASSERT_MSG(hasAnyFlag(image->getUsage(), Image::Usage::Sampled), "Missing "
					"descriptor set [" + debugName + "] pipeline uniform [" + string(name) + "] flag");
			}
			else if (isImageType(uniformType))
			{
				GARDEN_ASSERT_MSG(hasAnyFlag(image->getUsage(), Image::Usage::Storage), "Missing "
					"descriptor set [" + debugName + "] pipeline uniform [" + string(name) + "] flag");
			}
			else
			{
				GARDEN_ASSERT_MSG(hasAnyFlag(image->getUsage(), Image::Usage::InputAttachment), "Missing "
					"descriptor set [" + debugName + "] pipeline uniform [" + string(name) + "] flag");
			}
		}
		else if (isBufferType(uniformType))
		{
			auto bufferView = graphicsAPI->bufferPool.get(ID<Buffer>(uniform.resource));
			if (uniformType == GslUniformType::UniformBuffer)
			{
				GARDEN_ASSERT_MSG(hasAnyFlag(bufferView->getUsage(), Buffer::Usage::Uniform), "Missing "
					"descriptor set [" + debugName + "] pipeline uniform [" + string(name) + "] flag");
			}
			else
			{
				GARDEN_ASSERT_MSG(hasAnyFlag(bufferView->getUsage(), Buffer::Usage::Storage), "Missing "
					"descriptor set [" + debugName + "] pipeline uniform [" + string(name) + "] flag");
			}
		}
		else if (uniformType == GslUniformType::AccelerationStructure)
		{
			auto tlasView = graphicsAPI->tlasPool.get(ID<Tlas>(uniform.resource));
			auto bufferView = graphicsAPI->bufferPool.get(tlasView->getStorageBuffer());
			GARDEN_ASSERT_MSG(hasAnyFlag(bufferView->getUsage(), Buffer::Usage::StorageAS), "Missing "
				"descriptor set [" + debugName + "] pipeline uniform [" + string(name) + "] flag");
		}
		else abort();
	}
	#endif

	dsUniform.value().resourceSets[setIndex][elementIndex] = uniform.resource;
}

void DescriptorSet::updateResources(string_view name, uint32 elementCount, uint32 elementOffset, uint32 setIndex)
{
	GARDEN_ASSERT_MSG(!name.empty(), "Assert " + debugName);
	GARDEN_ASSERT_MSG(elementCount > 0, "Assert " + debugName);

	auto graphicsAPI = GraphicsAPI::get();
	auto pipelineView = graphicsAPI->getPipelineView(pipelineType, pipeline);
	GARDEN_ASSERT_MSG(elementCount + elementOffset <= pipelineView->getMaxBindlessCount(), "Assert " + debugName);

	const auto& pipelineUniforms = pipelineView->getUniforms();
	auto pipelineUniform = pipelineUniforms.find(name);
	if (pipelineUniform == pipelineUniforms.end())
		throw GardenError("Missing required pipeline uniform. (" + string(name) + ")");
	auto dsUniform = this->uniforms.find(name);
	if (dsUniform == this->uniforms.end())
		throw GardenError("Missing required descriptor set uniform. (" + string(name) + ")");

	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		updateVkDescriptorSetResources(instance, pipelineUniform->second, 
			dsUniform->second, elementCount, elementOffset, setIndex);
	}
	else abort();
}

#if GARDEN_DEBUG || GARDEN_EDITOR
//**********************************************************************************************************************
void DescriptorSet::setDebugName(const string& name)
{
	Resource::setDebugName(name);

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		#if GARDEN_DEBUG // Note: No GARDEN_EDITOR
		auto vulkanAPI = VulkanAPI::get();
		if (!vulkanAPI->hasDebugUtils)
			return;

		vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eDescriptorSet, 0);
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
		#endif
	}
	else abort();
}
#endif