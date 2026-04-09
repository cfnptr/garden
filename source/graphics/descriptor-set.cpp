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

#include "garden/graphics/descriptor-set.hpp"
#include "garden/graphics/vulkan/api.hpp"

using namespace math;
using namespace garden;
using namespace garden::graphics;

//**********************************************************************************************************************
#if GARDEN_DEBUG || GARDEN_EDITOR
uint32 DescriptorSet::combinedSamplerCount = 0;
uint32 DescriptorSet::uniformBufferCount = 0;
uint32 DescriptorSet::storageImageCount = 0;
uint32 DescriptorSet::storageBufferCount = 0;
uint32 DescriptorSet::inputAttachmentCount = 0;
uint32 DescriptorSet::accelStructureCount = 0;
#endif

static void* createVkDescriptorSet(ID<Pipeline> pipeline, PipelineType pipelineType, 
	const DescriptorSet::Uniforms& uniforms, uint8& setCount, uint8 setIndex)
{
	auto vulkanAPI = VulkanAPI::get();
	auto pipelineView = vulkanAPI->getPipelineView(pipelineType, pipeline);
	const auto& descriptorSetLayouts = PipelineExt::getDescriptorSetLayouts(**pipelineView);
	GARDEN_ASSERT_MSG(setIndex < descriptorSetLayouts.size(), "Out of pipeline [" + pipelineView->getDebugName() + 
		"] descriptor set count [" + to_string(descriptorSetLayouts.size()) + "]");
	GARDEN_ASSERT_MSG(descriptorSetLayouts[setIndex], "Assert " + pipelineView->getDebugName());

	vk::DescriptorSetLayout descriptorSetLayout = (VkDescriptorSetLayout)descriptorSetLayouts[setIndex];
	vk::DescriptorPool descriptorPool = (VkDescriptorPool)PipelineExt::getDescriptorPools(**pipelineView)[setIndex];
	vk::DescriptorSetAllocateInfo allocateInfo(descriptorPool ? descriptorPool : 
		vulkanAPI->descriptorPool, (uint32)uniforms.begin()->second.resourceSets.size());

	void* instance = nullptr;
	if (allocateInfo.descriptorSetCount > 1)
	{
		auto descriptorSets = malloc<vk::DescriptorSet>(allocateInfo.descriptorSetCount);
		vulkanAPI->descriptorSetLayouts.assign(allocateInfo.descriptorSetCount, descriptorSetLayout);
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
		if (pair.second.descriptorSetIndex != setIndex)
			continue;
		
		auto descriptorType = toVkDescriptorType(pair.second.type);
		switch (descriptorType)
		{
		case vk::DescriptorType::eCombinedImageSampler:
			DescriptorSet::combinedSamplerCount += allocateInfo.descriptorSetCount;
			GARDEN_ASSERT(DescriptorSet::combinedSamplerCount < GARDEN_DS_POOL_COMBINED_SAMPLER_COUNT);
			break;
		case vk::DescriptorType::eUniformBuffer:
			DescriptorSet::uniformBufferCount += allocateInfo.descriptorSetCount;
			GARDEN_ASSERT(DescriptorSet::uniformBufferCount < GARDEN_DS_POOL_UNIFORM_BUFFER_COUNT);
			break;
		case vk::DescriptorType::eStorageImage:
			DescriptorSet::storageImageCount += allocateInfo.descriptorSetCount;
			GARDEN_ASSERT(DescriptorSet::storageImageCount < GARDEN_DS_POOL_STORAGE_IMAGE_COUNT);
			break;
		case vk::DescriptorType::eStorageBuffer:
			DescriptorSet::storageBufferCount += allocateInfo.descriptorSetCount;
			GARDEN_ASSERT(DescriptorSet::storageBufferCount < GARDEN_DS_POOL_STORAGE_BUFFER_COUNT);
			break;
		case vk::DescriptorType::eInputAttachment:
			DescriptorSet::inputAttachmentCount += allocateInfo.descriptorSetCount;
			GARDEN_ASSERT(DescriptorSet::inputAttachmentCount < GARDEN_DS_POOL_INPUT_ATTACHMENT_COUNT);
			break;
		case vk::DescriptorType::eAccelerationStructureKHR:
			DescriptorSet::accelStructureCount += allocateInfo.descriptorSetCount;
			GARDEN_ASSERT(DescriptorSet::accelStructureCount < GARDEN_DS_POOL_ACCEL_STRUCTURE_COUNT);
			break;
		default: abort();
		}
	}
	#endif

	setCount = allocateInfo.descriptorSetCount;
	return instance;
}

static void destroyVkDescriptorSet(void* instance, ID<Pipeline> pipeline, PipelineType pipelineType, 
	const DescriptorSet::Uniforms& uniforms, uint8 setCount, uint8 setIndex)
{
	auto vulkanAPI = VulkanAPI::get();
	auto pipelineView = vulkanAPI->getPipelineView(pipelineType, pipeline);
	auto isBindless = PipelineExt::getDescriptorPools(**pipelineView)[setIndex];

	if (vulkanAPI->forceResourceDestroy)
	{
		if (isBindless)
		{
			if (setCount > 1)
				free(instance);
		}
		else
		{
			auto result = vulkanAPI->device.freeDescriptorSets(vulkanAPI->descriptorPool, setCount, 
				setCount > 1 ? (vk::DescriptorSet*)instance : (vk::DescriptorSet*)&instance);
			vk::detail::resultCheck(result, "vk::Device::freeDescriptorSets");
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
		if (pair.second.descriptorSetIndex != setIndex)
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

static vk::ImageLayout getVkDsImageLayout(VulkanAPI* vulkanAPI, View<ImageView> imageView, 
	OptView<Framebuffer> framebufferView, bool isImageType)
{
	if (isImageType)
		return vk::ImageLayout::eGeneral;
	if (!framebufferView || !framebufferView->getDepthStencilAttachment().imageView)
		return vk::ImageLayout::eShaderReadOnlyOptimal;

	auto dsImageView = vulkanAPI->imageViewPool.get(
		framebufferView->getDepthStencilAttachment().imageView);
	if (dsImageView->getImage() != imageView->getImage())
		return vk::ImageLayout::eShaderReadOnlyOptimal;
	auto imageLayout = (vk::ImageLayout)FramebufferExt::getDepthStencilLayout(**framebufferView);

	#if GARDEN_DEBUG
	if (imageLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal ||
		imageLayout == vk::ImageLayout::eDepthAttachmentOptimal || 
		imageLayout == vk::ImageLayout::eStencilAttachmentOptimal)
	{
		throw GardenError("Can't render and sample image at the same time.");
	}
	#endif
	return imageLayout;
}

//**********************************************************************************************************************
static void recreateVkDescriptorSet(const DescriptorSet::Uniforms& oldUniforms, 
	const DescriptorSet::Uniforms& newUniforms, const DescriptorSet::Samplers& samplers, 
	const Pipeline::Uniforms& pipelineUniforms, vk::DescriptorPool descriptorPool, 
	vk::DescriptorSetLayout descriptorSetLayout, OptView<Framebuffer> framebufferView, 
	DescriptorSet::Barriers& barriers, void*& instance, uint8 setIndex)
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
			auto result = vulkanAPI->device.freeDescriptorSets(descriptorPool ? descriptorPool : 
				vulkanAPI->descriptorPool, oldSetCount, (vk::DescriptorSet*)instance);
			vk::detail::resultCheck(result, "vk::Device::freeDescriptorSets");
			instance = realloc<vk::DescriptorSet>((vk::DescriptorSet*)instance, newSetCount);

			vulkanAPI->descriptorSetLayouts.assign(newSetCount, descriptorSetLayout);
			allocateInfo.pSetLayouts = vulkanAPI->descriptorSetLayouts.data();
			auto allocateResult = vulkanAPI->device.allocateDescriptorSets(
				&allocateInfo, (vk::DescriptorSet*)instance);
			vk::detail::resultCheck(allocateResult, "vk::Device::allocateDescriptorSets");
		}
		else
		{
			auto result = vulkanAPI->device.freeDescriptorSets(descriptorPool ?
				descriptorPool : vulkanAPI->descriptorPool, 1, (vk::DescriptorSet*)&instance);
			vk::detail::resultCheck(result, "vk::Device::freeDescriptorSets");

			allocateInfo.pSetLayouts = &descriptorSetLayout;
			auto allocateResult = vulkanAPI->device.allocateDescriptorSets(
				&allocateInfo, (vk::DescriptorSet*)&instance);
			vk::detail::resultCheck(allocateResult, "vk::Device::allocateDescriptorSets");
		}
	}

	vk::WriteDescriptorSet writeDescriptorSet;
	auto instances = newSetCount > 1 ? (vk::DescriptorSet*)instance : (vk::DescriptorSet*)&instance;
	uint32 imageInfoCount = 0, bufferInfoCount = 0, asInfoCount = 0, tlasCount = 0;

	for	(const auto& uniformPair : pipelineUniforms)
	{
		if (uniformPair.second.descriptorSetIndex != setIndex)
			continue;

		const auto& dsUniform = newUniforms.at(uniformPair.first);
		auto resourceSetData = dsUniform.resourceSets.data();
		auto pipelineUniform = uniformPair.second;

		if (pipelineUniform.isSamplerType | pipelineUniform.isImageType)
		{
			for (uint32 i = 0; i < newSetCount; i++)
				imageInfoCount += (uint32)resourceSetData[i].size();
		}
		else if (pipelineUniform.isBufferType)
		{
			for (uint32 i = 0; i < newSetCount; i++)
				bufferInfoCount += (uint32)resourceSetData[i].size();
		}
		else if (pipelineUniform.type == GslUniformType::AccelerationStructure)
		{
			for (uint32 i = 0; i < newSetCount; i++)
				asInfoCount += (uint32)resourceSetData[i].size();
			tlasCount += newSetCount;
		}
		else abort();
	}

	if (vulkanAPI->descriptorImageInfos.size() < imageInfoCount)
		vulkanAPI->descriptorImageInfos.resize(imageInfoCount);
	if (vulkanAPI->descriptorBufferInfos.size() < bufferInfoCount)
		vulkanAPI->descriptorBufferInfos.resize(bufferInfoCount);
	if (vulkanAPI->asDescriptorInfos.size() < asInfoCount)
		vulkanAPI->asDescriptorInfos.resize(asInfoCount);
	if (vulkanAPI->asWriteDescriptorSets.size() < tlasCount)
		vulkanAPI->asWriteDescriptorSets.resize(tlasCount);

	for (auto& setBarriers : barriers)
		setBarriers.clear();

	imageInfoCount = 0, bufferInfoCount = 0, asInfoCount = 0, tlasCount = 0;
	for	(const auto& uniformPair : pipelineUniforms)
	{
		if (uniformPair.second.descriptorSetIndex != setIndex)
			continue;

		auto pipelineUniform = uniformPair.second;
		const auto& dsUniform = newUniforms.at(uniformPair.first);
		writeDescriptorSet.dstBinding = (uint32)pipelineUniform.bindingIndex;
		writeDescriptorSet.descriptorType = toVkDescriptorType(pipelineUniform.type);

		Image::LayoutState newResourceState;
		newResourceState.stage = (uint64)toVkPipelineStages(pipelineUniform.pipelineStages);
		if (pipelineUniform.isSamplerType)
			newResourceState.access = (uint64)vk::AccessFlagBits2::eShaderSampledRead;
		else
		{
			if (pipelineUniform.readAccess)
				newResourceState.access = (uint64)vk::AccessFlagBits2::eShaderStorageRead;
			if (pipelineUniform.writeAccess)
				newResourceState.access |= (uint64)vk::AccessFlagBits2::eShaderStorageWrite;
		}

		if (pipelineUniform.isSamplerType | pipelineUniform.isImageType)
		{
			vk::DescriptorImageInfo imageInfo;
			if (pipelineUniform.isMutable)
			{
				auto sampler = samplers.at(uniformPair.first);
				auto samplerView = vulkanAPI->samplerPool.get(sampler);
				imageInfo.sampler = (VkSampler)ResourceExt::getInstance(**samplerView);
			}
			else
			{
				GARDEN_ASSERT_MSG(samplers.find(uniformPair.first) == samplers.end(), 
					"Shader uniform [" + uniformPair.first + "] is not marked as 'mutable'");
			}

			for (uint32 i = 0; i < newSetCount; i++)
			{
				auto& setBarriers = barriers[i];
				const auto& resourceArray = dsUniform.resourceSets[i];
				auto resourceArraySize = (uint32)resourceArray.size();
				auto resourceArrayData = resourceArray.data();

				auto hasAnyResource = false;
				for (uint32 j = 0; j < resourceArraySize; j++)
				{
					auto resource = resourceArrayData[j];
					if (resource)
					{
						auto imageView = vulkanAPI->imageViewPool.get(ID<ImageView>(resource));
						imageInfo.imageView = (VkImageView)ResourceExt::getInstance(**imageView);
						imageInfo.imageLayout = getVkDsImageLayout(vulkanAPI, 
							imageView, framebufferView, pipelineUniform.isImageType);
						newResourceState.layout = (uint32)imageInfo.imageLayout;
						newResourceState.view = ID<ImageView>(resource);
						setBarriers.push_back(newResourceState);
						hasAnyResource = true;
					}
					else imageInfo.imageView = nullptr;
					vulkanAPI->descriptorImageInfos[imageInfoCount + j] = imageInfo;
				}

				if (!hasAnyResource)
					continue;

				writeDescriptorSet.dstSet = instances[i];
				writeDescriptorSet.descriptorCount = resourceArraySize;
				writeDescriptorSet.pImageInfo = &vulkanAPI->descriptorImageInfos[imageInfoCount];
				vulkanAPI->writeDescriptorSets.push_back(writeDescriptorSet);
				imageInfoCount += writeDescriptorSet.descriptorCount;
			}
		}
		else if (pipelineUniform.isBufferType)
		{
			vk::DescriptorBufferInfo bufferInfo;
			for (uint32 i = 0; i < newSetCount; i++)
			{
				auto& setBarriers = barriers[i];
				const auto& resourceArray = dsUniform.resourceSets[i];
				auto resourceArraySize = (uint32)resourceArray.size();
				auto resourceArrayData = resourceArray.data();

				auto hasAnyResource = false;
				for (uint32 j = 0; j < resourceArraySize; j++)
				{
					auto resource = resourceArrayData[j];
					if (resource)
					{
						auto buffer = vulkanAPI->bufferPool.get(ID<Buffer>(resource));
						bufferInfo.buffer = (VkBuffer)ResourceExt::getInstance(**buffer);
						bufferInfo.range = buffer->getBinarySize(); // TODO: support part of the buffer mapping?
						newResourceState.layout = 0; // Marking that this is a buffer.
						newResourceState.view = ID<ImageView>(resource);
						setBarriers.push_back(newResourceState);
						hasAnyResource = true;
					}
					else
					{
						bufferInfo.buffer = nullptr; bufferInfo.range = 0;
					}
					vulkanAPI->descriptorBufferInfos[bufferInfoCount + j] = bufferInfo;
				}
				
				if (!hasAnyResource)
					continue;

				writeDescriptorSet.dstSet = instances[i];
				writeDescriptorSet.descriptorCount = resourceArraySize;
				writeDescriptorSet.pBufferInfo = &vulkanAPI->descriptorBufferInfos[bufferInfoCount];
				vulkanAPI->writeDescriptorSets.push_back(writeDescriptorSet);
				bufferInfoCount += writeDescriptorSet.descriptorCount;
			}
		}
		else if (pipelineUniform.type == GslUniformType::AccelerationStructure)
		{
			writeDescriptorSet.pImageInfo = nullptr;
			writeDescriptorSet.pBufferInfo = nullptr;

			vk::WriteDescriptorSetAccelerationStructureKHR asWriteDescriptorSet;
			for (uint32 i = 0; i < newSetCount; i++)
			{
				auto& setBarriers = barriers[i];
				const auto& resourceArray = dsUniform.resourceSets[i];
				auto resourceArraySize = (uint32)resourceArray.size();
				auto resourceArrayData = resourceArray.data();

				auto hasAnyResource = false;
				for (uint32 j = 0; j < resourceArraySize; j++)
				{
					auto resource = resourceArrayData[j];
					vk::AccelerationStructureKHR accelerationStructure;
					if (resource)
					{
						auto tlas = vulkanAPI->tlasPool.get(ID<Tlas>(resource));
						accelerationStructure = (VkAccelerationStructureKHR)ResourceExt::getInstance(**tlas);
						// Marking that this is just a memory.
						newResourceState.layout = UINT32_MAX; // Marking that this is a TLAS.
						newResourceState.view = ID<ImageView>(resource); 
						setBarriers.push_back(newResourceState);
						hasAnyResource = true;
					}
					else accelerationStructure = nullptr;
					vulkanAPI->asDescriptorInfos[asInfoCount + j] = accelerationStructure;
				}
				
				if (!hasAnyResource)
					continue;

				asWriteDescriptorSet.accelerationStructureCount = resourceArraySize;
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
static void updateVkDescriptorSetResources(void* instance, const DescriptorSet::Uniform& dsUniform, 
	Pipeline::Uniform pipelineUniform, OptView<Framebuffer> framebufferView, vector<Image::LayoutState>& setBarriers, 
	uint32 elementCount, uint32 elementOffset, uint8 setIndex)
{
	auto vulkanAPI = VulkanAPI::get();
	auto resourceArrayData = dsUniform.resourceSets[setIndex].data();

	if (pipelineUniform.isSamplerType | pipelineUniform.isImageType)
	{
		if (vulkanAPI->descriptorImageInfos.size() < elementCount)
			vulkanAPI->descriptorImageInfos.resize(elementCount);
	}
	else if (pipelineUniform.isBufferType)
	{
		if (vulkanAPI->descriptorBufferInfos.size() < elementCount)
			vulkanAPI->descriptorBufferInfos.resize(elementCount);
	}
	else if (pipelineUniform.type == GslUniformType::AccelerationStructure)
	{
		if (vulkanAPI->asDescriptorInfos.size() < elementCount)
			vulkanAPI->asDescriptorInfos.resize(elementCount);
	}
	else abort();

	vk::WriteDescriptorSet writeDescriptorSet((VkDescriptorSet)instance, pipelineUniform.bindingIndex, 
		elementOffset, elementCount, toVkDescriptorType(pipelineUniform.type));
	auto count = elementOffset + elementCount;

	Image::LayoutState newResourceState;
	newResourceState.stage = (uint64)toVkPipelineStages(pipelineUniform.pipelineStages);
	if (pipelineUniform.isSamplerType)
		newResourceState.access = (uint64)vk::AccessFlagBits2::eShaderSampledRead;
	else
	{
		if (pipelineUniform.readAccess)
			newResourceState.access = (uint64)vk::AccessFlagBits2::eShaderStorageRead;
		if (pipelineUniform.writeAccess)
			newResourceState.access |= (uint64)vk::AccessFlagBits2::eShaderStorageWrite;
	}
	setBarriers.clear();

	auto hasAnyResource = false;
	if (pipelineUniform.isSamplerType | pipelineUniform.isImageType)
	{
		vk::DescriptorImageInfo imageInfo;
		auto imageInfoData = vulkanAPI->descriptorImageInfos.data();

		for (uint32 i = 0, e = elementOffset; e < count; i++, e++)
		{
			auto resource = resourceArrayData[e];
			if (resource)
			{
				auto imageView = vulkanAPI->imageViewPool.get(ID<ImageView>(resource));
				imageInfo.imageView = (VkImageView)ResourceExt::getInstance(**imageView);
				imageInfo.imageLayout = getVkDsImageLayout(vulkanAPI, 
					imageView, framebufferView, pipelineUniform.isImageType);
				newResourceState.layout = (uint32)imageInfo.imageLayout;
				newResourceState.view = ID<ImageView>(resource);
				setBarriers.push_back(newResourceState);
				hasAnyResource = true;
			}
			else imageInfo.imageView = nullptr;
			imageInfoData[i] = imageInfo;
		}

		if (hasAnyResource)
			writeDescriptorSet.pImageInfo = imageInfoData;
	}
	else if (pipelineUniform.isBufferType)
	{
		vk::DescriptorBufferInfo bufferInfo({}, 0, 0);
		auto bufferInfoData = vulkanAPI->descriptorBufferInfos.data();

		for (uint32 i = 0, e = elementOffset; e < count; i++, e++)
		{
			auto resource = resourceArrayData[e];
			if (resource)
			{
				auto buffer = vulkanAPI->bufferPool.get(ID<Buffer>(resource));
				bufferInfo.buffer = (VkBuffer)ResourceExt::getInstance(**buffer);
				bufferInfo.range = buffer->getBinarySize();
				newResourceState.layout = 0; // Marking that this is a buffer.
				newResourceState.view = ID<ImageView>(resource);
				setBarriers.push_back(newResourceState);
				hasAnyResource = true;
			}
			else
			{
				bufferInfo.buffer = nullptr; bufferInfo.range = 0;
			}
			bufferInfoData[i] = bufferInfo;
		}

		if (hasAnyResource)
			writeDescriptorSet.pBufferInfo = bufferInfoData;
	}
	else if (pipelineUniform.type == GslUniformType::AccelerationStructure)
	{
		auto asInfoData = vulkanAPI->asDescriptorInfos.data();
		for (uint32 i = 0, e = elementOffset; e < count; i++, e++)
		{
			auto resource = resourceArrayData[e];
			vk::AccelerationStructureKHR accelerationStructure;
			if (resource)
			{
				auto tlas = vulkanAPI->tlasPool.get(ID<Tlas>(resource));
				accelerationStructure = (VkAccelerationStructureKHR)ResourceExt::getInstance(**tlas);
				// Marking that this is just a memory.
				newResourceState.layout = 0; newResourceState.view = {}; 
				setBarriers.push_back(newResourceState);
				hasAnyResource = true;
			}
			else accelerationStructure = nullptr;
			asInfoData[i] = accelerationStructure;
		}

		if (hasAnyResource)
		{
			vk::WriteDescriptorSetAccelerationStructureKHR asWriteDescriptorSet;
			asWriteDescriptorSet.accelerationStructureCount = elementCount;
			asWriteDescriptorSet.pAccelerationStructures = asInfoData;
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
		this->instance = createVkDescriptorSet(pipeline, pipelineType, uniforms, setCount, index);
	else abort();

	barriers.resize(uniforms.begin()->second.resourceSets.size());
	recreate(std::move(uniforms), std::move(samplers));
}

bool DescriptorSet::destroy()
{
	if (!instance || busyLock > 0)
		return false;

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		destroyVkDescriptorSet(instance, pipeline, pipelineType, uniforms, setCount, index);
	else abort();

	return true;
}

void DescriptorSet::recreate(Uniforms&& uniforms, Samplers&& samplers)
{
	GARDEN_ASSERT_MSG(this->uniforms.size() == 0 || 
		uniforms.size() == this->uniforms.size(), "Assert " + debugName);

	auto graphicsAPI = GraphicsAPI::get();
	auto pipelineView = graphicsAPI->getPipelineView(pipelineType, pipeline);
	const auto& pipelineUniforms = pipelineView->getUniforms();

	OptView<Framebuffer> framebufferView = {};
	if (pipelineType == PipelineType::Graphics)
	{
		framebufferView = OptView<Framebuffer>(graphicsAPI->framebufferPool.get(
			View<GraphicsPipeline>(pipelineView)->getFramebuffer()));
	}

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
			auto pipelineUniform = pair.second;
			const auto& dsUniform = uniforms.at(pair.first);
			const auto& resourceArray = dsUniform.resourceSets[i];

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

				if (pipelineUniform.isSamplerType | pipelineUniform.isImageType)
				{
					auto imageView = graphicsAPI->imageViewPool.get(ID<ImageView>(resource));
					GARDEN_ASSERT_MSG(toImageType(pipelineUniform.type) == imageView->getType(), 
						"Different descriptor set [" + debugName + "] and pipeline uniform [" + pair.first + "] types");
					GARDEN_ASSERT_MSG(!isFormatDepthAndStencil(imageView->getFormat()), "Image view is both depth and "
						"stencil format [" + debugName + "] at pipeline uniform [" + pair.first + "]");

					auto image = graphicsAPI->imagePool.get(imageView->getImage());
					if (pipelineUniform.isSamplerType)
					{
						GARDEN_ASSERT_MSG(hasAnyFlag(image->getUsage(), Image::Usage::Sampled), "Missing "
							"descriptor set [" + debugName + "] pipeline uniform [" + pair.first + "] flag");
					}
					else
					{
						GARDEN_ASSERT_MSG(hasAnyFlag(image->getUsage(), Image::Usage::Storage), "Missing "
							"descriptor set [" + debugName + "] pipeline uniform [" + pair.first + "] flag");
					}
				}
				else if (pipelineUniform.isBufferType)
				{
					auto bufferView = graphicsAPI->bufferPool.get(ID<Buffer>(resource));
					if (pipelineUniform.type == GslUniformType::UniformBuffer)
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
				else if (pipelineUniform.type == GslUniformType::AccelerationStructure)
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
		recreateVkDescriptorSet(this->uniforms, uniforms, samplers, pipelineUniforms, 
			descriptorPool, descriptorSetLayout, framebufferView, barriers, instance, index);
	}
	else abort();

	this->uniforms = std::move(uniforms);
}

//**********************************************************************************************************************
void DescriptorSet::updateUniform(string_view name, 
	const UniformResource& uniform, uint32 elementIndex, uint8 setIndex)
{
	GARDEN_ASSERT_MSG(!name.empty(), "Assert " + debugName);
	GARDEN_ASSERT_MSG(uniform.resource, "Assert " + debugName);

	auto graphicsAPI = GraphicsAPI::get();
	auto pipelineView = graphicsAPI->getPipelineView(pipelineType, pipeline);
	GARDEN_ASSERT_MSG(elementIndex <= pipelineView->getMaxBindlessCount(), "Assert " + debugName);

	const auto& pipelineUniforms = pipelineView->getUniforms();
	auto uniformPair = pipelineUniforms.find(name);
	if (uniformPair == pipelineUniforms.end())
		throw GardenError("Missing required pipeline uniform. (" + string(name) + ")");
	auto dsUniform = this->uniforms.find(name);
	if (dsUniform == this->uniforms.end())
		throw GardenError("Missing required descriptor set uniform. (" + string(name) + ")");

	#if GARDEN_DEBUG
	auto pipelineUniform = uniformPair->second;
	if (pipelineUniform.isSamplerType | pipelineUniform.isImageType)
	{
		auto imageView = graphicsAPI->imageViewPool.get(ID<ImageView>(uniform.resource));
		GARDEN_ASSERT(toImageType(pipelineUniform.type) == imageView->getType());

		auto image = graphicsAPI->imagePool.get(imageView->getImage());
		if (pipelineUniform.isSamplerType)
		{
			GARDEN_ASSERT_MSG(hasAnyFlag(image->getUsage(), Image::Usage::Sampled), "Missing "
				"descriptor set [" + debugName + "] pipeline uniform [" + string(name) + "] flag");
		}
		else
		{
			GARDEN_ASSERT_MSG(hasAnyFlag(image->getUsage(), Image::Usage::Storage), "Missing "
				"descriptor set [" + debugName + "] pipeline uniform [" + string(name) + "] flag");
		}
	}
	else if (pipelineUniform.isBufferType)
	{
		auto bufferView = graphicsAPI->bufferPool.get(ID<Buffer>(uniform.resource));
		if (pipelineUniform.type == GslUniformType::UniformBuffer)
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
	else if (pipelineUniform.type == GslUniformType::AccelerationStructure)
	{
		auto tlasView = graphicsAPI->tlasPool.get(ID<Tlas>(uniform.resource));
		auto bufferView = graphicsAPI->bufferPool.get(tlasView->getStorageBuffer());
		GARDEN_ASSERT_MSG(hasAnyFlag(bufferView->getUsage(), Buffer::Usage::StorageAS), "Missing "
			"descriptor set [" + debugName + "] pipeline uniform [" + string(name) + "] flag");
	}
	else abort();
	#endif

	dsUniform.value().resourceSets[setIndex][elementIndex] = uniform.resource;
}

void DescriptorSet::updateResources(string_view name, uint32 elementCount, uint32 elementOffset, uint8 setIndex)
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

	OptView<Framebuffer> framebufferView = {};
	if (pipelineType == PipelineType::Graphics)
	{
		framebufferView = OptView<Framebuffer>(graphicsAPI->framebufferPool.get(
			View<GraphicsPipeline>(pipelineView)->getFramebuffer()));
	}

	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		updateVkDescriptorSetResources(instance, dsUniform->second, pipelineUniform->second, 
			framebufferView, barriers[setIndex], elementCount, elementOffset, setIndex);
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
		if (!vulkanAPI->features.debugUtils)
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