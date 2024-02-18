//--------------------------------------------------------------------------------------------------
// Copyright 2022-2023 Nikita Fediuchin. All rights reserved.
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
//--------------------------------------------------------------------------------------------------

#include "garden/graphics/descriptor-set.hpp"
#include "garden/graphics/vulkan.hpp"
#include "garden/graphics/pipeline/graphics.hpp"

using namespace std;
using namespace garden::graphics;

static vector<vk::DescriptorSetLayout> descriptorSetLayouts;

DescriptorSet::DescriptorSet(ID<Pipeline> pipeline, PipelineType pipelineType,
	map<string, Uniform>&& uniforms, uint8 index)
{
	this->pipeline = pipeline;
	this->pipelineType = pipelineType;
	this->index = index;	

	vk::DescriptorSetLayout descriptorSetLayout;
	vk::DescriptorPool descriptorPool;

	if (pipelineType == PipelineType::Graphics)
	{
		auto pipelineView = GraphicsAPI::graphicsPipelinePool.get(
			ID<GraphicsPipeline>(pipeline));
		descriptorSetLayout = (VkDescriptorSetLayout)
			pipelineView->descriptorSetLayouts[index];
		descriptorPool = (VkDescriptorPool)pipelineView->descriptorPools[index];
	}
	else if (pipelineType == PipelineType::Compute)
	{
		auto pipelineView = GraphicsAPI::computePipelinePool.get(
			ID<ComputePipeline>(pipeline));
		descriptorSetLayout = (VkDescriptorSetLayout)
			pipelineView->descriptorSetLayouts[index];
		descriptorPool = (VkDescriptorPool)pipelineView->descriptorPools[index];
	}
	else abort();

	GARDEN_ASSERT(descriptorSetLayout);
	auto setCount = (uint32)uniforms.begin()->second.resourceSets.size();
	vk::DescriptorSetAllocateInfo allocateInfo(descriptorPool ?
		descriptorPool : Vulkan::descriptorPool, setCount);
	
	if (setCount > 1)
	{
		auto descriptorSets = malloc<vk::DescriptorSet>(setCount);
		this->instance = descriptorSets;
		descriptorSetLayouts.assign(setCount, descriptorSetLayout);
		allocateInfo.pSetLayouts = descriptorSetLayouts.data();
		auto allocateResult = Vulkan::device.allocateDescriptorSets(&allocateInfo, descriptorSets);
		vk::resultCheck(allocateResult, "vk::Device::allocateDescriptorSets");
	}
	else
	{
		allocateInfo.pSetLayouts = &descriptorSetLayout;
		auto allocateResult = Vulkan::device.allocateDescriptorSets(
			&allocateInfo, (vk::DescriptorSet*)&this->instance);
		vk::resultCheck(allocateResult, "vk::Device::allocateDescriptorSets");
	}
	
	recreate(std::move(uniforms));
}

//--------------------------------------------------------------------------------------------------
bool DescriptorSet::destroy()
{
	if (!instance || readyLock > 0)
		return false;

	if (GraphicsAPI::isRunning)
	{
		bool isBindless;
		if (pipelineType == PipelineType::Graphics)
		{
			auto pipelineView = GraphicsAPI::graphicsPipelinePool.get(
				ID<GraphicsPipeline>(this->pipeline));
			isBindless = pipelineView->descriptorPools[index];
		}
		else if (pipelineType == PipelineType::Compute)
		{
			auto pipelineView = GraphicsAPI::computePipelinePool.get(
				ID<ComputePipeline>(this->pipeline));
			isBindless = pipelineView->descriptorPools[index];
		}
		else abort();
		
		if (!isBindless)
		{
			GraphicsAPI::destroyResource(
				GraphicsAPI::DestroyResourceType::DescriptorSet,
				instance, nullptr, uniforms.begin()->second.resourceSets.size() - 1);
		}
		else
		{
			if (uniforms.begin()->second.resourceSets.size() > 1)
				free(instance);
		}
	}
	else
	{
		if (uniforms.begin()->second.resourceSets.size() > 1)
			free(instance);
	}

	instance = nullptr;
	return true;
}

//--------------------------------------------------------------------------------------------------
static vector<vk::WriteDescriptorSet> writeDescriptorSets;
static vector<vk::DescriptorImageInfo> descriptorImageInfos;
static vector<vk::DescriptorBufferInfo> descriptorBufferInfos;

void DescriptorSet::recreate(map<string, Uniform>&& uniforms)
{
	GARDEN_ASSERT(this->uniforms.size() == 0 ||
		uniforms.size() == this->uniforms.size());

	vk::DescriptorPool descriptorPool; uint32 maxBindlessCount = 0;
	const map<string, Pipeline::Uniform>* pipelineUniforms = nullptr;

	if (pipelineType == PipelineType::Graphics)
	{
		auto pipelineView = GraphicsAPI::graphicsPipelinePool.get(
			ID<GraphicsPipeline>(this->pipeline));
		descriptorPool = (VkDescriptorPool)pipelineView->descriptorPools[index];
		maxBindlessCount = pipelineView->maxBindlessCount;
		pipelineUniforms = &pipelineView->uniforms;
	}
	else if (pipelineType == PipelineType::Compute)
	{
		auto pipelineView = GraphicsAPI::computePipelinePool.get(
			ID<ComputePipeline>(this->pipeline));
		descriptorPool = (VkDescriptorPool)pipelineView->descriptorPools[index];
		maxBindlessCount = pipelineView->maxBindlessCount;
		pipelineUniforms = &pipelineView->uniforms;
	}
	else abort();

	#if GARDEN_DEBUG
	auto& firstUniform = uniforms.begin()->second;
	auto setCount = firstUniform.resourceSets.size();

	for (auto& pair : uniforms)
	{
		GARDEN_ASSERT(!pair.first.empty());
		GARDEN_ASSERT(setCount == pair.second.resourceSets.size());
		
		if (!descriptorPool)
		{
			for (auto& resourceArray : pair.second.resourceSets)
			{
				for (auto resource : resourceArray)
					GARDEN_ASSERT(resource);
			}
		}
	}
	#endif

	// Note: Checks if descriptor set is just now created. 
	auto oldSetCount = this->uniforms.size() > 0 ?
		(uint32)this->uniforms.begin()->second.resourceSets.size() : (uint32)0;
	auto newSetCount = (uint32)uniforms.begin()->second.resourceSets.size();

	if (oldSetCount != 0 && newSetCount != oldSetCount)
	{
		vk::DescriptorSetLayout descriptorSetLayout;
		if (pipelineType == PipelineType::Graphics)
		{
			auto pipelineView = GraphicsAPI::graphicsPipelinePool.get(
				ID<GraphicsPipeline>(pipeline));
			descriptorSetLayout = (VkDescriptorSetLayout)
				pipelineView->descriptorSetLayouts[index];
		}
		else if (pipelineType == PipelineType::Compute)
		{
			auto pipelineView = GraphicsAPI::computePipelinePool.get(
				ID<ComputePipeline>(pipeline));
			descriptorSetLayout = (VkDescriptorSetLayout)
				pipelineView->descriptorSetLayouts[index];
		}
		else abort();

		vk::DescriptorSetAllocateInfo allocateInfo(descriptorPool ?
			descriptorPool : Vulkan::descriptorPool, newSetCount);

		// TODO: We can only allocate or free required array part.
		// 		 But I'm not sure if it faster or better.
		if (oldSetCount > 1)
		{
			Vulkan::device.freeDescriptorSets(descriptorPool ?
				descriptorPool : Vulkan::descriptorPool,
				oldSetCount, (vk::DescriptorSet*)this->instance);
			this->instance = realloc<vk::DescriptorSet>(
				(vk::DescriptorSet*)this->instance, newSetCount);

			descriptorSetLayouts.assign(newSetCount, descriptorSetLayout);
			allocateInfo.pSetLayouts = descriptorSetLayouts.data();
			auto allocateResult = Vulkan::device.allocateDescriptorSets(
				&allocateInfo, (vk::DescriptorSet*)this->instance);
			vk::resultCheck(allocateResult, "vk::Device::allocateDescriptorSets");
		}
		else
		{
			Vulkan::device.freeDescriptorSets(descriptorPool ?
				descriptorPool : Vulkan::descriptorPool,
				1, (vk::DescriptorSet*)&this->instance);

			allocateInfo.pSetLayouts = &descriptorSetLayout;
			auto allocateResult = Vulkan::device.allocateDescriptorSets(
				&allocateInfo, (vk::DescriptorSet*)&this->instance);
			vk::resultCheck(allocateResult, "vk::Device::allocateDescriptorSets");
		}
	}

	vk::WriteDescriptorSet writeDescriptorSet;
	auto uniformCount = pipelineUniforms->size();
	auto bufferSize = uniformCount * newSetCount;
	auto instances = newSetCount > 1 ?
		(vk::DescriptorSet*)this->instance : (vk::DescriptorSet*)&this->instance;
	writeDescriptorSets.reserve(bufferSize);
	descriptorImageInfos.reserve(bufferSize);
	descriptorBufferInfos.reserve(bufferSize);

	for	(auto& pair : *pipelineUniforms)
	{
		if (pair.second.descriptorSetIndex != index)
			continue;

		#if GARDEN_DEBUG
		if (uniforms.find(pair.first) == uniforms.end())
			throw runtime_error("Missing required pipeline uniform. (" + pair.first + ")");
		#endif

		auto& pipelineUniform = pair.second;
		auto& dsUniform = uniforms.at(pair.first);
		auto uniformType = pipelineUniform.type;
		writeDescriptorSet.dstBinding = (uint32)pipelineUniform.bindingIndex;
		writeDescriptorSet.descriptorType = toVkDescriptorType(uniformType);

		if (isSamplerType(uniformType) || isImageType(uniformType) ||
			uniformType == GslUniformType::SubpassInput)
		{
			GARDEN_ASSERT(dsUniform.type == typeid(ImageView));
			writeDescriptorSet.pBufferInfo = nullptr;

			vk::DescriptorImageInfo imageInfo(VK_NULL_HANDLE, VK_NULL_HANDLE,
				isImageType(uniformType) ? vk::ImageLayout::eGeneral :
				vk::ImageLayout::eShaderReadOnlyOptimal);

			for (uint32 i = 0; i < newSetCount; i++)
			{
				uint32 resourceCount = 0;
				auto& resourceArray = dsUniform.resourceSets[i];

				#if GARDEN_DEBUG
				if (pipelineUniform.arraySize > 0)
				{
					GARDEN_ASSERT(resourceArray.size() == pipelineUniform.arraySize);
				}
				else
				{
					GARDEN_ASSERT(resourceArray.size() == maxBindlessCount);
				}
				#endif

				for (uint32 j = 0; j < (uint32)resourceArray.size(); j++)
				{
					if (!resourceArray[j])
						continue;

					auto imageView = GraphicsAPI::imageViewPool.get(ID<ImageView>(resourceArray[j]));
			
					#if GARDEN_DEBUG
					auto image = GraphicsAPI::imagePool.get(imageView->image);
					if (isSamplerType(uniformType))
					{
						GARDEN_ASSERT(hasAnyFlag(image->getBind(), Image::Bind::Sampled));
						GARDEN_ASSERT(toImageType(uniformType) == imageView->type);
					}
					else if (isImageType(uniformType))
					{
						GARDEN_ASSERT(hasAnyFlag(image->getBind(), Image::Bind::Storage));
						GARDEN_ASSERT(toImageType(uniformType) == imageView->type);
					}
					else
					{
						GARDEN_ASSERT(hasAnyFlag(image->getBind(),
							Image::Bind::InputAttachment));
					}
					#endif

					imageInfo.imageView = (VkImageView)imageView->instance;
					descriptorImageInfos.push_back(imageInfo);
					resourceCount++;
				}
				
				if (resourceCount == 0)
					continue;

				writeDescriptorSet.dstSet = instances[i];
				writeDescriptorSet.descriptorCount = (uint32)resourceArray.size();
				writeDescriptorSet.pImageInfo = &descriptorImageInfos[(uint32)(
					descriptorImageInfos.size() - resourceCount)];
				writeDescriptorSets.push_back(writeDescriptorSet);
			}
		}
		else if (uniformType == GslUniformType::UniformBuffer ||
			uniformType == GslUniformType::StorageBuffer)
		{
			GARDEN_ASSERT(dsUniform.type == typeid(Buffer));
			writeDescriptorSet.pImageInfo = nullptr;

			for (uint32 i = 0; i < newSetCount; i++)
			{
				uint32 resourceCount = 0;
				auto& resourceArray = dsUniform.resourceSets[i];

				#if GARDEN_DEBUG
				if (pipelineUniform.arraySize > 0)
				{
					GARDEN_ASSERT(resourceArray.size() == pipelineUniform.arraySize);
				}
				else
				{
					GARDEN_ASSERT(resourceArray.size() == maxBindlessCount);
				}
				#endif

				for (uint32 j = 0; j < (uint32)resourceArray.size(); j++)
				{
					if (!resourceArray[j])
						continue;

					auto buffer = GraphicsAPI::bufferPool.get(ID<Buffer>(resourceArray[j]));

					#if GARDEN_DEBUG
					if (uniformType == GslUniformType::UniformBuffer)
					{
						GARDEN_ASSERT(hasAnyFlag(buffer->getBind(), Buffer::Bind::Uniform));
					}
					else
					{
						GARDEN_ASSERT(hasAnyFlag(buffer->getBind(), Buffer::Bind::Storage));
					}
					#endif

					// TODO: support part of the buffer mapping?
					descriptorBufferInfos.push_back(vk::DescriptorBufferInfo(
						(VkBuffer)buffer->instance, 0, buffer->binarySize));
					resourceCount++;
				}
				
				if (resourceCount == 0)
					continue;

				writeDescriptorSet.dstSet = instances[i];
				writeDescriptorSet.descriptorCount = (uint32)resourceArray.size();
				writeDescriptorSet.pBufferInfo = &descriptorBufferInfos[(uint32)(
					descriptorBufferInfos.size() - resourceArray.size())];
				writeDescriptorSets.push_back(writeDescriptorSet);
			}
		}
		else abort();
	}

	if (!writeDescriptorSets.empty())
	{
		Vulkan::device.updateDescriptorSets(writeDescriptorSets, {});

		writeDescriptorSets.clear();
		descriptorImageInfos.clear();
		descriptorBufferInfos.clear();
	}

	this->uniforms = std::move(uniforms);
}

//--------------------------------------------------------------------------------------------------
void DescriptorSet::updateUniform(const string& name,
	const Uniform& uniform, uint32 elementOffset)
{
	GARDEN_ASSERT(!name.empty());
	GARDEN_ASSERT(uniform.resourceSets.size() == 1);

	uint32 maxBindlessCount = 0;
	const map<string, Pipeline::Uniform>* pipelineUniforms = nullptr;

	if (pipelineType == PipelineType::Graphics)
	{
		auto pipelineView = GraphicsAPI::graphicsPipelinePool.get(
			ID<GraphicsPipeline>(this->pipeline));
		GARDEN_ASSERT(pipelineView->bindless);
		maxBindlessCount = pipelineView->maxBindlessCount;
		pipelineUniforms = &pipelineView->uniforms;
	}
	else if (pipelineType == PipelineType::Compute)
	{
		auto pipelineView = GraphicsAPI::computePipelinePool.get(
			ID<ComputePipeline>(this->pipeline));
		GARDEN_ASSERT(pipelineView->bindless);
		maxBindlessCount = pipelineView->maxBindlessCount;
		pipelineUniforms = &pipelineView->uniforms;
	}
	else abort();

	GARDEN_ASSERT(uniform.resourceSets[0].size() +
		elementOffset <= maxBindlessCount);

	#if GARDEN_DEBUG
	if (pipelineUniforms->find(name) == pipelineUniforms->end())
		throw runtime_error("Missing required pipeline uniform. (" + name + ")");
	#endif

	auto& pipelineUniform = pipelineUniforms->at(name);
	auto uniformType = pipelineUniform.type;

	vk::WriteDescriptorSet writeDescriptorSet(
		(VkDescriptorSet)this->instance, pipelineUniform.bindingIndex, elementOffset, 
		(uint32)uniform.resourceSets[0].size(), toVkDescriptorType(uniformType));

	if (isSamplerType(uniformType) || isImageType(uniformType))
	{
		GARDEN_ASSERT(uniform.type == typeid(ImageView));
		writeDescriptorSet.pBufferInfo = nullptr;

		vk::DescriptorImageInfo imageInfo(
			VK_NULL_HANDLE, VK_NULL_HANDLE, isImageType(uniformType) ?
			vk::ImageLayout::eGeneral : vk::ImageLayout::eShaderReadOnlyOptimal);

		auto& resourceArray = uniform.resourceSets[0];
		for (uint32 i = 0; i < (uint32)resourceArray.size(); i++)
		{
			if (!resourceArray[i])
				continue;

			auto imageView = GraphicsAPI::imageViewPool.get(ID<ImageView>(resourceArray[i]));
			
			#if GARDEN_DEBUG
			auto image = GraphicsAPI::imagePool.get(imageView->image);
			if (isSamplerType(uniformType))
			{
				GARDEN_ASSERT(hasAnyFlag(image->getBind(), Image::Bind::Sampled));
			}
			else
			{
				GARDEN_ASSERT(hasAnyFlag(image->getBind(), Image::Bind::Storage));
			}
			GARDEN_ASSERT(toImageType(uniformType) == imageView->type);
			#endif

			imageInfo.imageView = (VkImageView)imageView->instance;
			descriptorImageInfos.push_back(imageInfo);
		}

		writeDescriptorSet.pImageInfo = &descriptorImageInfos[(uint32)(
			descriptorImageInfos.size() - resourceArray.size())];
	}
	else if (uniformType == GslUniformType::UniformBuffer ||
		uniformType == GslUniformType::StorageBuffer)
	{
		GARDEN_ASSERT(uniform.type == typeid(Buffer));
		writeDescriptorSet.pImageInfo = nullptr;

		auto& resourceArray = uniform.resourceSets[0];
		for (uint32 i = 0; i < (uint32)resourceArray.size(); i++)
		{
			if (!resourceArray[i])
				continue;

			auto buffer = GraphicsAPI::bufferPool.get(ID<Buffer>(resourceArray[0]));

			#if GARDEN_DEBUG
			if (uniformType == GslUniformType::UniformBuffer)
			{
				GARDEN_ASSERT(hasAnyFlag(buffer->getBind(), Buffer::Bind::Uniform));
			}
			else
			{
				GARDEN_ASSERT(hasAnyFlag(buffer->getBind(), Buffer::Bind::Storage));
			}
			#endif

			descriptorBufferInfos.push_back(vk::DescriptorBufferInfo(
				(VkBuffer)buffer->instance, 0, buffer->binarySize));
		}

		writeDescriptorSet.pBufferInfo = &descriptorBufferInfos[(uint32)(
			descriptorBufferInfos.size() - resourceArray.size())];
	}
	else abort();

	if (!descriptorImageInfos.empty() || !descriptorBufferInfos.empty())
	{
		Vulkan::device.updateDescriptorSets(1, &writeDescriptorSet, 0, nullptr);

		descriptorImageInfos.clear();
		descriptorBufferInfos.clear();
	}

	memcpy(this->uniforms.at(name).resourceSets[0].data() +
		elementOffset, uniform.resourceSets[0].data(),
		uniform.resourceSets[0].size() * sizeof(ID<Resource>));
}

#if GARDEN_DEBUG
//--------------------------------------------------------------------------------------------------
void DescriptorSet::setDebugName(const string& name)
{
	Resource::setDebugName(name);

	if (!Vulkan::hasDebugUtils)
		return;

	vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eDescriptorSet, 0);
	auto setCount = (uint32)uniforms.begin()->second.resourceSets.size();

	if (setCount > 1)
	{
		auto instances = (void**)instance;
		for	(uint32 i = 0; i < setCount; i++)
		{
			nameInfo.objectHandle = (uint64)instances[i];
			auto itemName = name + to_string(i);
			nameInfo.pObjectName = itemName.c_str();
			Vulkan::device.setDebugUtilsObjectNameEXT(nameInfo, Vulkan::dynamicLoader);
		}
	}
	else
	{
		nameInfo.objectHandle = (uint64)instance;
		nameInfo.pObjectName = name.c_str();
		Vulkan::device.setDebugUtilsObjectNameEXT(nameInfo, Vulkan::dynamicLoader);
	}
}
#endif

// TODO: track and log total used sampler/buffer/etc in the application to adjust pool size.
