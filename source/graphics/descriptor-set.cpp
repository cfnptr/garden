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
	const DescriptorSet::Uniforms& uniforms, uint8& setCount, uint8 setIndex)
{
	auto vulkanAPI = VulkanAPI::getInstance();
	auto pipelineView = vulkanAPI->getPipelineView(pipeline, pipelineType);
	const auto& descriptorSetLayouts = PipelineExt::getDescriptorSetLayouts(**pipelineView);
	vk::DescriptorSetLayout descriptorSetLayout = (VkDescriptorSetLayout)descriptorSetLayouts[setIndex];

	vk::DescriptorSetAllocateInfo allocateInfo(vulkanAPI->descriptorPool, 
		(uint32)uniforms.begin()->second.resourceSets.size());

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
		default: GARDEN_UNREACHABLE();
		}
	}
	#endif

	setCount = allocateInfo.descriptorSetCount;
	return instance;
}

//**********************************************************************************************************************
static void* createVkDescriptorSet(Pipeline::Uniforms&& pipelineUniforms, 
	DescriptorSet::BindlessData* bindless, uint32 maxBindlessCount, uint8 index)
{
	auto vulkanAPI = VulkanAPI::getInstance();
	vector<vk::DescriptorSetLayoutBinding> layoutBindings;
	layoutBindings.reserve(pipelineUniforms.size());

	vk::DescriptorPoolSize combineSamplerPoolSize(vk::DescriptorType::eCombinedImageSampler, 0);
	vk::DescriptorPoolSize uniformBufferPoolSize(vk::DescriptorType::eUniformBuffer, 0);
	vk::DescriptorPoolSize storageImagePoolSize(vk::DescriptorType::eStorageImage, 0);
	vk::DescriptorPoolSize storageBufferPoolSize(vk::DescriptorType::eStorageBuffer, 0);
	vk::DescriptorPoolSize inputAttachmentPoolSize(vk::DescriptorType::eInputAttachment, 0);
	vk::DescriptorPoolSize accelStructurePoolsSize(vk::DescriptorType::eAccelerationStructureKHR, 0);

	for	(const auto& uniformPair : pipelineUniforms)
	{
		auto pipelineUniform = uniformPair.second;
		GARDEN_ASSERT_MSG(pipelineUniform.descriptorSetIndex == index && pipelineUniform.arraySize == 0, 
			"Use separated descriptor set for non-bindless resources, instead of combined one.");
		if (!pipelineUniform.isMutable && pipelineUniform.isSamplerType) // TODO: do we need support?
			throw GardenError("Immutable samplers are not supported for bindless descriptor sets.");

		vk::DescriptorSetLayoutBinding layoutBinding((uint32)pipelineUniform.bindingIndex, toVkDescriptorType(
			pipelineUniform.type), maxBindlessCount, toVkShaderStages(pipelineUniform.pipelineStages));

		switch (layoutBinding.descriptorType)
		{
			case vk::DescriptorType::eCombinedImageSampler:
				combineSamplerPoolSize.descriptorCount += maxBindlessCount; break;
			case vk::DescriptorType::eUniformBuffer:
				uniformBufferPoolSize.descriptorCount += maxBindlessCount; break;
			case vk::DescriptorType::eStorageImage:
				storageImagePoolSize.descriptorCount += maxBindlessCount; break;
			case vk::DescriptorType::eStorageBuffer:
				storageBufferPoolSize.descriptorCount += maxBindlessCount; break;
			case vk::DescriptorType::eInputAttachment:
				inputAttachmentPoolSize.descriptorCount += maxBindlessCount; break;
			case vk::DescriptorType::eAccelerationStructureKHR:
				accelStructurePoolsSize.descriptorCount += maxBindlessCount; break;
			default: GARDEN_UNREACHABLE();
		}
	}

	vector<vk::DescriptorPoolSize> descriptorPoolSizes;
	descriptorPoolSizes.reserve(GARDEN_DS_POOL_SIZE_COUNT);
	if (combineSamplerPoolSize.descriptorCount > 0)
		descriptorPoolSizes.push_back(combineSamplerPoolSize);
	if (uniformBufferPoolSize.descriptorCount > 0)
		descriptorPoolSizes.push_back(uniformBufferPoolSize);
	if (storageImagePoolSize.descriptorCount > 0)
		descriptorPoolSizes.push_back(storageImagePoolSize);
	if (storageBufferPoolSize.descriptorCount > 0)
		descriptorPoolSizes.push_back(storageBufferPoolSize);
	if (inputAttachmentPoolSize.descriptorCount > 0)
		descriptorPoolSizes.push_back(inputAttachmentPoolSize);
	if (accelStructurePoolsSize.descriptorCount > 0)
		descriptorPoolSizes.push_back(accelStructurePoolsSize);
	GARDEN_ASSERT(!descriptorPoolSizes.empty());

	vk::DescriptorPoolCreateInfo descriptorPoolInfo(vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind, 
		1, (uint32)descriptorPoolSizes.size(), descriptorPoolSizes.data());
	auto descriptorPool = vulkanAPI->device.createDescriptorPool(descriptorPoolInfo);
	bindless->descriptorPool = descriptorPool;

	vector<vk::DescriptorBindingFlags> descriptorBindingFlags(pipelineUniforms.size(),
		vk::DescriptorBindingFlagBits::eUpdateAfterBind | vk::DescriptorBindingFlagBits::ePartiallyBound);
	vk::DescriptorSetLayoutBindingFlagsCreateInfo layoutBindingFlagsInfo(
		(uint32)descriptorBindingFlags.size(), descriptorBindingFlags.data());
	vk::DescriptorSetLayoutCreateInfo descriptorSetLayoutInfo(vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool, 
		(uint32)layoutBindings.size(), layoutBindings.data(), &layoutBindingFlagsInfo);
	auto descriptorSetLayout = vulkanAPI->device.createDescriptorSetLayout(descriptorSetLayoutInfo);
	bindless->descriptorSetLayout = descriptorSetLayout;

	#if GARDEN_USE_GAPI_VALIDATIONS
	if (vulkanAPI->features.debugUtils)
	{
		vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eDescriptorPool,
			(uint64)(VkDescriptorPool)descriptorPool, "descriptorPool.bindless");
		vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo);
	}
	#endif

	void* instance = nullptr;
	vk::DescriptorSetAllocateInfo allocateInfo(descriptorPool, 1, &descriptorSetLayout);
	auto allocateResult = vulkanAPI->device.allocateDescriptorSets(&allocateInfo, (vk::DescriptorSet*)&instance);
	vk::detail::resultCheck(allocateResult, "vk::Device::allocateDescriptorSets");

	bindless->pipelineUniforms = std::move(pipelineUniforms);
	return instance;
}

//**********************************************************************************************************************
static void destroyVkDescriptorSet(const Pipeline::Uniforms& pipelineUniforms, 
	void* instance, DescriptorSet::BindlessData* bindless, uint8 setCount, uint8 setIndex)
{
	auto vulkanAPI = VulkanAPI::getInstance();
	if (vulkanAPI->forceResourceDestroy)
	{
		if (bindless)
		{
			vulkanAPI->device.destroyDescriptorSetLayout((VkDescriptorSetLayout)bindless->descriptorSetLayout);
			vulkanAPI->device.destroyDescriptorPool((VkDescriptorPool)bindless->descriptorPool);
			delete bindless;
		}
		else
		{
			vk::Result result;
			if (setCount > 0)
			{
				result = vulkanAPI->device.freeDescriptorSets(
					vulkanAPI->descriptorPool, setCount, (vk::DescriptorSet*)instance);
				free(instance);
			}
			else
			{
				result = vulkanAPI->device.freeDescriptorSets(
					vulkanAPI->descriptorPool, 1, (vk::DescriptorSet*)&instance);
			}
			vk::detail::resultCheck(result, "vk::Device::freeDescriptorSets");
		}
	}
	else
	{
		vulkanAPI->destroyResource(GraphicsAPI::DestroyResourceType::DescriptorSet,
			instance, bindless, setCount > 1 ? setCount : 0);
	}

	#if GARDEN_DEBUG || GARDEN_EDITOR
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
			default: GARDEN_UNREACHABLE();
		}
	}
	#endif
}

static vk::ImageLayout getVkDsImageLayout(VulkanAPI* vulkanAPI, View<ImageView> imageView, 
	OptView<Framebuffer> framebufferView, bool isImageType)
{
	if (isImageType || vulkanAPI->features.unifiedLayouts)
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
static vk::WriteDescriptorSet createVkWriteDescriptorSet(
	VulkanAPI* vulkanAPI, void* instance, const DescriptorSet::Samplers& samplers, 
	string_view uniformName, PipelineUniform pipelineUniform, OptView<Framebuffer> framebufferView, 
	vector<Image::LayoutState>& setBarriers, const ID<Resource>* resourceArray, 
	uint32 elementCount, uint32 elementOffset, uint8 setIndex)
{
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
		if (pipelineUniform.isMutable)
		{
			auto sampler = samplers.at(uniformName);
			auto samplerView = vulkanAPI->samplerPool.get(sampler);
			imageInfo.sampler = (VkSampler)ResourceExt::getInstance(**samplerView);
		}
		else
		{
			GARDEN_ASSERT_MSG(samplers.find(uniformName) == samplers.end(), 
				"Shader uniform [" + string(uniformName) + "] is not marked as 'mutable'");
		}

		for (uint32 i = elementOffset; i < count; i++)
		{
			auto resource = resourceArray[i];
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
			else { imageInfo.imageView = nullptr; imageInfo.imageLayout = {}; }
			vulkanAPI->descriptorImageInfos.push_back(imageInfo);
		}

		if (hasAnyResource)
			writeDescriptorSet.pImageInfo = &vulkanAPI->descriptorImageInfos.back();
	}
	else if (pipelineUniform.isBufferType)
	{
		vk::DescriptorBufferInfo bufferInfo({}, 0, 0);
		for (uint32 i = elementOffset; i < count; i++)
		{
			auto resource = resourceArray[i];
			if (resource)
			{
				auto buffer = vulkanAPI->bufferPool.get(ID<Buffer>(resource));
				bufferInfo.buffer = (VkBuffer)ResourceExt::getInstance(**buffer);
				bufferInfo.range = buffer->getBinarySize();
				newResourceState.layout = DescriptorSet::bufferBarrierLayout;
				newResourceState.view = ID<ImageView>(resource);
				setBarriers.push_back(newResourceState);
				hasAnyResource = true;
			}
			else { bufferInfo.buffer = nullptr; bufferInfo.range = 0; }
			vulkanAPI->descriptorBufferInfos.push_back(bufferInfo);
		}

		if (hasAnyResource)
			writeDescriptorSet.pBufferInfo = &vulkanAPI->descriptorBufferInfos.back();
	}
	else if (pipelineUniform.type == GslUniformType::AccelerationStructure)
	{
		for (uint32 i = elementOffset; i < count; i++)
		{
			auto resource = resourceArray[i];
			vk::AccelerationStructureKHR accelerationStructure;
			if (resource)
			{
				auto tlas = vulkanAPI->tlasPool.get(ID<Tlas>(resource));
				accelerationStructure = (VkAccelerationStructureKHR)ResourceExt::getInstance(**tlas);
				newResourceState.layout = DescriptorSet::asBarrierLayout;
				newResourceState.view = ID<ImageView>(resource);
				setBarriers.push_back(newResourceState);
				hasAnyResource = true;
			}
			else accelerationStructure = nullptr;
			vulkanAPI->asDescriptorInfos.push_back(accelerationStructure);
		}

		if (hasAnyResource)
		{
			vk::WriteDescriptorSetAccelerationStructureKHR asWriteDescriptorSet(
				elementCount, &vulkanAPI->asDescriptorInfos.back());
			vulkanAPI->asWriteDescriptorSets.push_back(asWriteDescriptorSet);
			writeDescriptorSet.pNext = &vulkanAPI->asWriteDescriptorSets.back();
		}
	}
	else throw GardenError("Invalid pipeline uniform.");

	return writeDescriptorSet;
}

//**********************************************************************************************************************
static void writeVkDescriptorSets(const DescriptorSet::Samplers& samplers, 
	const DescriptorSet::Uniforms& dsUniforms, const Pipeline::Uniforms& pipelineUniforms, 
	vk::DescriptorSetLayout descriptorSetLayout, OptView<Framebuffer> framebufferView, 
	DescriptorSet::Barriers& barriers, void*& instance, uint8 setCount, uint8 setIndex)
{
	auto vulkanAPI = VulkanAPI::getInstance();
	auto instances = setCount > 1 ? (vk::DescriptorSet*)instance : (vk::DescriptorSet*)&instance;
	uint32 imageInfoCount = 0, bufferInfoCount = 0, asInfoCount = 0, tlasCount = 0;

	for	(const auto& uniformPair : pipelineUniforms)
	{
		if (uniformPair.second.descriptorSetIndex != setIndex)
			continue;

		const auto& dsUniform = dsUniforms.at(uniformPair.first);
		auto resourceSetData = dsUniform.resourceSets.data();
		auto pipelineUniform = uniformPair.second;

		if (pipelineUniform.isSamplerType | pipelineUniform.isImageType)
		{
			for (uint32 i = 0; i < setCount; i++)
				imageInfoCount += (uint32)resourceSetData[i].size();
		}
		else if (pipelineUniform.isBufferType)
		{
			for (uint32 i = 0; i < setCount; i++)
				bufferInfoCount += (uint32)resourceSetData[i].size();
		}
		else if (pipelineUniform.type == GslUniformType::AccelerationStructure)
		{
			for (uint32 i = 0; i < setCount; i++)
				asInfoCount += (uint32)resourceSetData[i].size();
			tlasCount += setCount;
		}
		else throw GardenError("Invalid pipeline uniform. (name: " + uniformPair.first + ")");
	}

	if (vulkanAPI->descriptorImageInfos.size() < imageInfoCount)
		vulkanAPI->descriptorImageInfos.reserve(imageInfoCount);
	if (vulkanAPI->descriptorBufferInfos.size() < bufferInfoCount)
		vulkanAPI->descriptorBufferInfos.reserve(bufferInfoCount);
	if (vulkanAPI->asDescriptorInfos.size() < asInfoCount)
		vulkanAPI->asDescriptorInfos.reserve(asInfoCount);
	if (vulkanAPI->asWriteDescriptorSets.size() < tlasCount)
		vulkanAPI->asWriteDescriptorSets.reserve(tlasCount);

	for (auto& setBarriers : barriers)
		setBarriers.clear();
	imageInfoCount = 0, bufferInfoCount = 0, asInfoCount = 0, tlasCount = 0;

	for	(const auto& uniformPair : pipelineUniforms)
	{
		if (uniformPair.second.descriptorSetIndex != setIndex)
			continue;

		const auto& uniformName = uniformPair.first;
		const auto& resourceSets = dsUniforms.at(uniformName).resourceSets;
		auto pipelineUniform = uniformPair.second;

		for (uint32 set = 0; set < setCount; set++)
		{
			const auto& resourceArray = resourceSets[set];
			auto elementCount = resourceArray.size(); // TODO: use only used bindless DS element count.
			auto writeDescriptorSet = createVkWriteDescriptorSet(vulkanAPI, instances[set], samplers, uniformName, 
				pipelineUniform, framebufferView, barriers[set], resourceArray.data(), elementCount, 0, setIndex);
			vulkanAPI->writeDescriptorSets.push_back(writeDescriptorSet);
		}
	}

	if (!vulkanAPI->writeDescriptorSets.empty())
	{
		vulkanAPI->device.updateDescriptorSets(vulkanAPI->writeDescriptorSets, {});

		vulkanAPI->writeDescriptorSets.clear();
		vulkanAPI->descriptorImageInfos.clear();
		vulkanAPI->descriptorBufferInfos.clear();
		vulkanAPI->asDescriptorInfos.clear();
	}
}

//**********************************************************************************************************************
static void updateVkDescriptorSet(const DescriptorSet::Samplers& samplers, 
	const DescriptorSet::Uniforms& oldUniforms, const DescriptorSet::Uniforms& newUniforms, 
	const Pipeline::Uniforms& pipelineUniforms, vk::DescriptorSetLayout descriptorSetLayout, 
	OptView<Framebuffer> framebufferView, DescriptorSet::Barriers& barriers, void*& instance, uint8 setIndex)
{
	auto vulkanAPI = VulkanAPI::getInstance();
	auto newSetCount = (uint32)newUniforms.begin()->second.resourceSets.size();
	auto oldSetCount = !oldUniforms.empty() ? (uint32)oldUniforms.begin()->second.resourceSets.size() : newSetCount;
	// Note: Checks if descriptor set is just now created. 

	if (newSetCount != oldSetCount)
	{
		vk::DescriptorSetAllocateInfo allocateInfo(vulkanAPI->descriptorPool, newSetCount);
		if (oldSetCount > 1)
		{
			auto result = vulkanAPI->device.freeDescriptorSets(
				vulkanAPI->descriptorPool, oldSetCount, (vk::DescriptorSet*)instance);
			vk::detail::resultCheck(result, "vk::Device::freeDescriptorSets");
			if (newSetCount == 1) free(instance);
		}
		else
		{
			auto result = vulkanAPI->device.freeDescriptorSets(
				vulkanAPI->descriptorPool, 1, (vk::DescriptorSet*)&instance);
			vk::detail::resultCheck(result, "vk::Device::freeDescriptorSets");
		}
		if (newSetCount > 1)
		{
			if (oldSetCount == 1) instance = malloc<vk::DescriptorSet>(newSetCount);
			vulkanAPI->descriptorSetLayouts.assign(newSetCount, descriptorSetLayout);
			allocateInfo.pSetLayouts = vulkanAPI->descriptorSetLayouts.data();
			auto allocateResult = vulkanAPI->device.allocateDescriptorSets(
				&allocateInfo, (vk::DescriptorSet*)instance);
			vk::detail::resultCheck(allocateResult, "vk::Device::allocateDescriptorSets");
		}
		else
		{
			allocateInfo.pSetLayouts = &descriptorSetLayout;
			auto allocateResult = vulkanAPI->device.allocateDescriptorSets(
				&allocateInfo, (vk::DescriptorSet*)&instance);
			vk::detail::resultCheck(allocateResult, "vk::Device::allocateDescriptorSets");
		}
	}

	writeVkDescriptorSets(samplers, newUniforms, pipelineUniforms, descriptorSetLayout, 
		framebufferView, barriers, instance, newSetCount, setIndex);
}

//**********************************************************************************************************************
DescriptorSet::DescriptorSet(ID<Pipeline> pipeline, PipelineType pipelineType, Uniforms&& uniforms, 
	Samplers&& samplers, uint8 index) : pipeline(pipeline), pipelineType(pipelineType), index(index)
{
	GARDEN_ASSERT(pipeline);
	GARDEN_ASSERT(!uniforms.empty());
	barriers.resize(uniforms.begin()->second.resourceSets.size());

	auto graphicsBackend = GraphicsAPI::getInstance()->getBackendType();
	if (graphicsBackend == GraphicsBackend::VulkanAPI)
		this->instance = createVkDescriptorSet(pipeline, pipelineType, uniforms, setCount, index);
	else GARDEN_UNREACHABLE();

	update(std::move(uniforms), std::move(samplers));
}
DescriptorSet::DescriptorSet(PipelineUniforms&& pipelineUniforms, Uniforms&& dsUniforms, 
	Samplers&& samplers, uint32 maxBindlessCount, uint8 index) : index(index)
{
	GARDEN_ASSERT(pipeline);
	GARDEN_ASSERT(!uniforms.empty());

	barriers.resize(uniforms.begin()->second.resourceSets.size());
	bindless = new BindlessData();

	auto graphicsBackend = GraphicsAPI::getInstance()->getBackendType();
	if (graphicsBackend == GraphicsBackend::VulkanAPI)
		this->instance = createVkDescriptorSet(std::move(pipelineUniforms), bindless, maxBindlessCount, index);
	else GARDEN_UNREACHABLE();

	update(std::move(uniforms), std::move(samplers));
}

bool DescriptorSet::destroy()
{
	if (!instance || isLocked())
		return false;

	auto graphicsBackend = GraphicsAPI::getInstance()->getBackendType();
	if (graphicsBackend == GraphicsBackend::VulkanAPI)
		destroyVkDescriptorSet(getPipelineUniforms(), instance, bindless, setCount, index);
	else GARDEN_UNREACHABLE();

	return true;
}

const DescriptorSet::PipelineUniforms& DescriptorSet::getPipelineUniforms() const
{
	if (bindless)
		return bindless->pipelineUniforms;
	auto pipelineView = GraphicsAPI::getInstance()->getPipelineView(pipeline, pipelineType);
	return pipelineView->getUniforms();
}

#if GARDEN_DEBUG
static void validatePipelineUniform(PipelineUniform pipelineUniform, 
	ID<Resource> resource, string_view dsDebugName, string_view uniformName)
{
	auto graphicsAPI = GraphicsAPI::getInstance();
	if (pipelineUniform.isSamplerType | pipelineUniform.isImageType)
	{
		auto imageView = graphicsAPI->imageViewPool.get(ID<ImageView>(resource));
		GARDEN_ASSERT_MSG(imageView->getType() == toImageType(pipelineUniform.type), "Different image "
			"[" + imageView->getDebugName() + "] and pipeline uniform [" + string(uniformName) + "] type");

		auto image = graphicsAPI->imagePool.get(imageView->getImage());
		if (pipelineUniform.isSamplerType)
		{
			GARDEN_ASSERT_MSG(hasAnyFlag(image->getUsage(), Image::Usage::Sampled), "Missing descriptor set "
				"[" + string(dsDebugName) + "] pipeline uniform [" + string(uniformName) + "] flag");
		}
		else
		{
			GARDEN_ASSERT_MSG(hasAnyFlag(image->getUsage(), Image::Usage::Storage), "Missing descriptor set "
				"[" + string(dsDebugName) + "] pipeline uniform [" + string(uniformName) + "] flag");
		}
	}
	else if (pipelineUniform.isBufferType)
	{
		auto buffer = graphicsAPI->bufferPool.get(ID<Buffer>(resource));
		if (pipelineUniform.type == GslUniformType::UniformBuffer)
		{
			GARDEN_ASSERT_MSG(hasAnyFlag(buffer->getUsage(), Buffer::Usage::Uniform), "Missing descriptor set "
				"[" + string(dsDebugName) + "] pipeline uniform [" + string(uniformName) + "] flag");
		}
		else
		{
			GARDEN_ASSERT_MSG(hasAnyFlag(buffer->getUsage(), Buffer::Usage::Storage), "Missing descriptor set "
				"[" + string(dsDebugName) + "] pipeline uniform [" + string(uniformName) + "] flag");
		}
	}
	else if (pipelineUniform.type == GslUniformType::AccelerationStructure)
	{
		auto tlasView = graphicsAPI->tlasPool.get(ID<Tlas>(resource));
		auto buffer = graphicsAPI->bufferPool.get(tlasView->getStorageBuffer());
		GARDEN_ASSERT_MSG(hasAnyFlag(buffer->getUsage(), Buffer::Usage::StorageAS), "Missing descriptor set "
			"[" + string(dsDebugName) + "] pipeline uniform [" + string(uniformName) + "] flag");
	}
	else throw GardenError("Invalid pipeline uniform. (name: " + string(uniformName) + ")");
}
#endif

//**********************************************************************************************************************
void DescriptorSet::update(Uniforms&& uniforms, Samplers&& samplers)
{
	GARDEN_ASSERT_MSG(this->uniforms.size() == 0 || 
		uniforms.size() == this->uniforms.size(), "Assert " + debugName);
	GARDEN_ASSERT_MSG(!bindless, "Assert " + debugName);

	auto graphicsAPI = GraphicsAPI::getInstance();
	auto pipelineView = graphicsAPI->getPipelineView(pipeline, pipelineType);
	const auto& pipelineUniforms = pipelineView->getUniforms();

	OptView<Framebuffer> framebufferView = {};
	if (pipelineType == PipelineType::Graphics)
	{
		framebufferView = OptView<Framebuffer>(graphicsAPI->framebufferPool.get(
			View<GraphicsPipeline>(pipelineView)->getFramebuffer()));
	}

	#if GARDEN_DEBUG || GARDEN_EDITOR
	for (const auto& pair : uniforms)
	{
		GARDEN_ASSERT_MSG(setCount == pair.second.resourceSets.size(), 
			"Different descriptor set [" + debugName + "] count and resource sets array size");
		GARDEN_ASSERT_MSG(!pair.first.empty(), "Descriptor set [" + debugName + "] uniform name is empty");
	}

	absl::btree_map<string_view, PipelineUniform> hashUniforms;
	auto hashState = Hash128::getState();
	Hash128::resetState(hashState);

	for	(const auto& pair : pipelineUniforms)
	{
		if (pair.second.descriptorSetIndex != index)
			continue;

		const auto& uniformName = pair.first;
		if (uniforms.find(uniformName) == uniforms.end())
			throw GardenError("Missing required pipeline uniform. (" + uniformName + ")");

		auto pipelineUniform = pair.second;
		const auto& dsUniform = uniforms.at(uniformName);

		for (uint32 set = 0; set < setCount; set++)
		{
			const auto& resourceArray = dsUniform.resourceSets[set];
			if (pipelineUniform.arraySize > 0)
			{
				GARDEN_ASSERT_MSG(resourceArray.size() == pipelineUniform.arraySize, "Different descriptor set [" +
					debugName + "] and pipeline [" + pipelineView->getDebugName() + "] array uniform size");
			}
			else
			{
				throw GardenError("You should use bindless pool for unsized arrays. ("
					"pipeline: " + pipelineView->getDebugName() + ", uniform: " + uniformName + ")");
			}

			for (auto resource : resourceArray)
			{
				if (resource)
					validatePipelineUniform(pipelineUniform, resource, debugName, uniformName);
			}
		}
		hashUniforms.emplace(uniformName, pipelineUniform);
	}
	for	(const auto& pair : hashUniforms)
		Hash128::updateState(hashState, &pair.second, sizeof(PipelineUniform));
	layoutHash = Hash128::digestState(hashState);
	#endif

	auto graphicsBackend = graphicsAPI->getBackendType();
	if (graphicsBackend == GraphicsBackend::VulkanAPI)
	{
		vk::DescriptorSetLayout descriptorSetLayout = (VkDescriptorSetLayout)
			PipelineExt::getDescriptorSetLayouts(**pipelineView)[index];
		updateVkDescriptorSet(samplers, this->uniforms, uniforms, pipelineUniforms, 
			descriptorSetLayout, framebufferView, barriers, instance, index);
	}
	else GARDEN_UNREACHABLE();

	this->uniforms = std::move(uniforms);
	this->samplers = std::move(samplers);
}

//**********************************************************************************************************************
void DescriptorSet::updateUniform(string_view name, const UniformResource& uniform, uint32 elementIndex, uint8 setIndex)
{
	GARDEN_ASSERT_MSG(!name.empty(), "Assert " + debugName);
	GARDEN_ASSERT_MSG(uniform.resource, "Assert " + debugName);

	const auto& pipelineUniforms = getPipelineUniforms();
	auto uniformPair = pipelineUniforms.find(name);
	if (uniformPair == pipelineUniforms.end())
		throw GardenError("Missing required pipeline uniform. (" + string(name) + ")");
	auto dsUniform = this->uniforms.find(name);
	if (dsUniform == this->uniforms.end())
		throw GardenError("Missing required descriptor set uniform. (" + string(name) + ")");

	#if GARDEN_DEBUG
	validatePipelineUniform(uniformPair->second, uniform.resource, debugName, name);
	#endif

	dsUniform->second.resourceSets[setIndex][elementIndex] = uniform.resource;
}
void DescriptorSet::updateResources(string_view name, uint32 elementCount, uint32 elementOffset, uint8 setIndex)
{
	GARDEN_ASSERT_MSG(!name.empty(), "Assert " + debugName);
	GARDEN_ASSERT_MSG(elementCount > 0, "Assert " + debugName);

	const auto& pipelineUniforms = getPipelineUniforms();
	auto pipelineUniform = pipelineUniforms.find(name);
	if (pipelineUniform == pipelineUniforms.end())
		throw GardenError("Missing required pipeline uniform. (" + string(name) + ")");
	auto dsUniform = this->uniforms.find(name);
	if (dsUniform == this->uniforms.end())
		throw GardenError("Missing required descriptor set uniform. (" + string(name) + ")");

	const auto& resourceSets = dsUniform->second.resourceSets;
	GARDEN_ASSERT_MSG(setIndex < resourceSets.size(), "Assert " + debugName);
	const auto& resourceArray = resourceSets[setIndex];
	GARDEN_ASSERT_MSG(elementCount + elementOffset < resourceArray.size(), "Assert " + debugName);

	auto graphicsAPI = GraphicsAPI::getInstance();
	OptView<Framebuffer> framebufferView = {};

	if (pipeline && pipelineType == PipelineType::Graphics)
	{
		auto pipelineView = graphicsAPI->getPipelineView(pipeline, pipelineType);
		framebufferView = OptView<Framebuffer>(graphicsAPI->framebufferPool.get(
			View<GraphicsPipeline>(pipelineView)->getFramebuffer()));
	}

	auto graphicsBackend = graphicsAPI->getBackendType();
	if (graphicsBackend == GraphicsBackend::VulkanAPI)
	{
		createVkWriteDescriptorSet(VulkanAPI::getInstance(), instance, samplers, name, pipelineUniform->second, 
			framebufferView, barriers[setIndex], resourceArray.data(), elementCount, elementOffset, setIndex);
	}
	else GARDEN_UNREACHABLE();
}

#if GARDEN_DEBUG || GARDEN_EDITOR
//**********************************************************************************************************************
void DescriptorSet::setDebugName(const string& name)
{
	Resource::setDebugName(name);

	auto graphicsBackend = GraphicsAPI::getInstance()->getBackendType();
	if (graphicsBackend == GraphicsBackend::VulkanAPI)
	{
		#if GARDEN_USE_GAPI_VALIDATIONS
		auto vulkanAPI = VulkanAPI::getInstance();
		if (!vulkanAPI->features.debugUtils)
			return;

		vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eDescriptorSet, 0);
		if (setCount > 1)
		{
			auto instances = (vk::DescriptorSet*)instance;
			for (uint32 set = 0; set < setCount; set++)
			{
				nameInfo.objectHandle = (uint64)(VkDescriptorSet)instances[set];
				auto itemName = name + to_string(set);
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
	else GARDEN_UNREACHABLE();
}
#endif