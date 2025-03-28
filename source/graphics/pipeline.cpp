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

#include <fstream>

using namespace garden;
using namespace garden::graphics;

//**********************************************************************************************************************
static vk::Filter toVkFilter(SamplerFilter filterType) noexcept
{
	switch (filterType)
	{
	case SamplerFilter::Nearest: return vk::Filter::eNearest;
	case SamplerFilter::Linear: return vk::Filter::eLinear;
	default: abort();
	}
}
static vk::SamplerMipmapMode toVkSamplerMipmapMode(SamplerFilter filterType) noexcept
{
	switch (filterType)
	{
	case SamplerFilter::Nearest: return vk::SamplerMipmapMode::eNearest;
	case SamplerFilter::Linear: return vk::SamplerMipmapMode::eLinear;
	default: abort();
	}
}
static vk::SamplerAddressMode toVkSamplerAddressMode(Pipeline::SamplerWrap samplerWrap) noexcept
{
	switch (samplerWrap)
	{
	case Pipeline::SamplerWrap::Repeat: return vk::SamplerAddressMode::eRepeat;
	case Pipeline::SamplerWrap::MirroredRepeat: return vk::SamplerAddressMode::eMirroredRepeat;
	case Pipeline::SamplerWrap::ClampToEdge: return vk::SamplerAddressMode::eClampToEdge;
	case Pipeline::SamplerWrap::ClampToBorder: return vk::SamplerAddressMode::eClampToBorder;
	case Pipeline::SamplerWrap::MirrorClampToEdge: return vk::SamplerAddressMode::eMirrorClampToEdge;
	default: abort();
	}
}
static vk::BorderColor toVkBorderColor(Pipeline::BorderColor borderColor) noexcept
{
	switch (borderColor)
	{
		case Pipeline::BorderColor::FloatTransparentBlack: return vk::BorderColor::eFloatTransparentBlack;
		case Pipeline::BorderColor::IntTransparentBlack: return vk::BorderColor::eIntTransparentBlack;
		case Pipeline::BorderColor::FloatOpaqueBlack: return vk::BorderColor::eFloatOpaqueBlack;
		case Pipeline::BorderColor::IntOpaqueBlack: return vk::BorderColor::eIntOpaqueBlack;
		case Pipeline::BorderColor::FloatOpaqueWhite: return vk::BorderColor::eFloatOpaqueWhite;
		case Pipeline::BorderColor::IntOpaqueWhite: return vk::BorderColor::eIntOpaqueWhite;
		default: abort();
	}
}

//**********************************************************************************************************************
static vector<void*> createVkPipelineSamplers(const map<string, Pipeline::SamplerState>& samplerStates,
	map<string, vk::Sampler>& immutableSamplers, const fs::path& pipelinePath,
	const map<string, Pipeline::SamplerState>& samplerStateOverrides)
{
	auto vulkanAPI = VulkanAPI::get();
	vector<void*> samplers(samplerStates.size());
	uint32 i = 0;

	for (auto it = samplerStates.begin(); it != samplerStates.end(); it++, i++)
	{
		auto samplerStateSearch = samplerStateOverrides.find(it->first);
		auto state = samplerStateSearch == samplerStateOverrides.end() ?
			it->second : samplerStateSearch->second;

		vk::SamplerCreateInfo samplerInfo({}, toVkFilter(state.magFilter), toVkFilter(state.minFilter),
			toVkSamplerMipmapMode(state.mipmapFilter), toVkSamplerAddressMode(state.wrapX),
			toVkSamplerAddressMode(state.wrapY), toVkSamplerAddressMode(state.wrapZ),
			state.mipLodBias, state.anisoFiltering, state.maxAnisotropy,
			state.comparison, toVkCompareOp(state.compareOperation), state.minLod,
			state.maxLod == INFINITY ? VK_LOD_CLAMP_NONE : state.maxLod,
			toVkBorderColor(state.borderColor), state.unnormCoords);
		auto sampler = vulkanAPI->device.createSampler(samplerInfo);
		samplers[i] = (VkSampler)sampler;
		immutableSamplers.emplace(it->first, sampler);

		#if GARDEN_DEBUG
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
	const map<string, Pipeline::Uniform>& uniforms, const map<string, vk::Sampler>& immutableSamplers,
	const fs::path& pipelinePath, uint32 maxBindlessCount, bool& bindless)
{
	bindless = false;

	auto vulkanAPI = VulkanAPI::get();
	vector<vk::DescriptorSetLayoutBinding> descriptorSetBindings;
	vector<vk::DescriptorBindingFlags> descriptorBindingFlags;
	vector<vector<vk::Sampler>> samplerArrays; 
	
	for (uint8 i = 0; i < (uint8)descriptorSetLayouts.size(); i++)
	{	
		uint32 bindingIndex = 0; bool isBindless = false;

		vector<vk::DescriptorPoolSize> descriptorPoolSizes =
		{
			vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 0),
			vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, 0),
			vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 0),
			vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 0),
		};

		if (descriptorSetBindings.size() < uniforms.size())
		{
			descriptorSetBindings.resize(uniforms.size());
			descriptorBindingFlags.resize(uniforms.size());
		}

		for	(auto pair : uniforms)
		{
			auto uniform = pair.second;
			if (uniform.descriptorSetIndex != i)
				continue;

			auto& descriptorSetBinding = descriptorSetBindings[bindingIndex];
			descriptorSetBinding.binding = (uint32)uniform.bindingIndex;
			descriptorSetBinding.descriptorType = toVkDescriptorType(uniform.type);
			descriptorSetBinding.stageFlags = toVkShaderStages(uniform.shaderStages);

			if (uniform.arraySize > 0)
			{
				if (isSamplerType(uniform.type))
					descriptorSetBinding.pImmutableSamplers = &immutableSamplers.at(pair.first);
				descriptorSetBinding.descriptorCount = uniform.arraySize;
			}
			else
			{
				GARDEN_ASSERT(maxBindlessCount > 0);
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
				default: abort();
				}

				if (isSamplerType(uniform.type))
				{
					vector<vk::Sampler> samplers(maxBindlessCount, *&immutableSamplers.at(pair.first));
					samplerArrays.push_back(std::move(samplers));
					descriptorSetBinding.pImmutableSamplers = samplerArrays[samplerArrays.size() - 1].data();
				}

				descriptorBindingFlags[bindingIndex] =
					vk::DescriptorBindingFlagBits::eUpdateAfterBind | vk::DescriptorBindingFlagBits::ePartiallyBound;
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
			for (uint32 j = 0; j < (uint32)descriptorPoolSizes.size(); j++)
			{
				if (descriptorPoolSizes[j].descriptorCount == 0)
				{
					descriptorPoolSizes.erase(descriptorPoolSizes.begin() + j);

					if (j > 0)
						j--;
				}
				else
				{
					maxSetCount += descriptorPoolSizes[j].descriptorCount;
				}
			}

			vk::DescriptorPoolCreateInfo descriptorPoolInfo(vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind, 
				maxSetCount, (uint32)descriptorPoolSizes.size(), descriptorPoolSizes.data());
			descriptorPools[i] = vulkanAPI->device.createDescriptorPool(descriptorPoolInfo);

			#if GARDEN_DEBUG
			if (vulkanAPI->hasDebugUtils)
			{
				auto name = "descriptorPool." + pipelinePath.generic_string() + to_string(i);
				vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eDescriptorPool,
					(uint64)(VkSampler)descriptorPools[i], name.c_str());
				vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo);
			}
			#endif
		}

		descriptorSetLayouts[i] = vulkanAPI->device.createDescriptorSetLayout(descriptorSetLayoutInfo);

		samplerArrays.clear();
		bindless = isBindless;

		#if GARDEN_DEBUG
		if (vulkanAPI->hasDebugUtils)
		{
			auto name = "descriptorSetLayout." + pipelinePath.generic_string() + to_string(i);
			vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eDescriptorSetLayout,
				(uint64)descriptorSetLayouts[i], name.c_str());
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

	vk::PipelineLayoutCreateInfo pipelineLayoutInfo({}, 0, nullptr,
		(uint32)pushConstantRanges.size(), pushConstantRanges.data());

	if (!descriptorSetLayouts.empty())
	{
		pipelineLayoutInfo.setLayoutCount = (uint32)descriptorSetLayouts.size();
		pipelineLayoutInfo.pSetLayouts = (const vk::DescriptorSetLayout*)descriptorSetLayouts.data();
	}

	auto vulkanAPI = VulkanAPI::get();
	auto layout = vulkanAPI->device.createPipelineLayout(pipelineLayoutInfo);

	#if GARDEN_DEBUG
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
static vector<void*> createVkShaders(const vector<vector<uint8>>& code, const fs::path& pipelinePath)
{
	auto vulkanAPI = VulkanAPI::get();
	vector<void*> shaders(code.size());

	for (uint8 i = 0; i < (uint8)code.size(); i++)
	{
		const auto& shaderCode = code[i];
		vk::ShaderModuleCreateInfo shaderInfo({},
	
		(uint32)shaderCode.size(), (const uint32*)shaderCode.data());
		shaders[i] = (VkShaderModule)vulkanAPI->device.createShaderModule(shaderInfo);

		#if GARDEN_DEBUG
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
	if (pushConstantsSize > 0)
	{
		auto threadCount = asyncRecording ? graphicsAPI->threadCount : 1;
		pushConstantsBuffer.resize(pushConstantsSize * threadCount);
	}

	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		if (createData.maxBindlessCount > 0 && !VulkanAPI::get()->hasDescriptorIndexing)
		{
			throw GardenError("Bindless descriptors are not supported on this GPU. ("
				"pipeline: )" + createData.shaderPath.generic_string() + ")");
		}

		this->pushConstantsMask = (uint32)toVkShaderStages(createData.pushConstantsStages);

		map<string, vk::Sampler> immutableSamplers;
		this->samplers = createVkPipelineSamplers(createData.samplerStates,
			immutableSamplers, createData.shaderPath, createData.samplerStateOverrides);

		createVkDescriptorSetLayouts(descriptorSetLayouts, descriptorPools, uniforms,
			immutableSamplers, createData.shaderPath, createData.maxBindlessCount, bindless);
		this->pipelineLayout = createVkPipelineLayout(pushConstantsSize,
			createData.pushConstantsStages, descriptorSetLayouts, createData.shaderPath);
	}
	else abort();
}

bool Pipeline::destroy()
{
	if (!instance || readyLock > 0)
		return false;

	#if GARDEN_DEBUG
	auto graphicsAPI = GraphicsAPI::get();
	if (!graphicsAPI->forceResourceDestroy)
	{
		auto pipelineInstance = graphicsAPI->getPipeline(type, this);
		for (auto& descriptorSet : graphicsAPI->descriptorSetPool)
		{
			if (!ResourceExt::getInstance(descriptorSet) || descriptorSet.getPipelineType() != type ||
				descriptorSet.getPipeline() != pipelineInstance)
			{
				continue;
			}

			throw GardenError("Descriptor set is still using destroyed pipeline. (pipeline: " +
				debugName + ", descriptorSet: " + descriptorSet.getDebugName() + ")");
		}
	}
	#endif

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		destroyVkPipeline(instance, pipelineLayout, 
			samplers, descriptorSetLayouts, descriptorPools, variantCount);
	}
	else abort();

	instance = nullptr;
	return true;
}

vector<void*> Pipeline::createShaders(const vector<vector<uint8>>& code, const fs::path& pipelinePath)
{
	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		return createVkShaders(code, pipelinePath);
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
void Pipeline::fillVkSpecConsts(const fs::path& path, void* specInfo, const map<string, SpecConst>& specConsts, 
	const map<string, SpecConstValue>& specConstValues, ShaderStage shaderStage, uint8 variantCount)
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
				"specConst: " + pair.first + ","
				"pipelinePath: " + path.generic_string() + ")");
		}
		#endif

		const auto& value = specConstValues.at(pair.first);
		GARDEN_ASSERT(value.constBase.type == pair.second.dataType);
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
	uint32 variantIndexValue = variantIndex;
	memcpy((void*)info->pData, &variantIndexValue, sizeof(uint32));
}

//**********************************************************************************************************************
void Pipeline::updateDescriptorsLock(const DescriptorSet::Range* descriptorSetRange, uint8 rangeCount)
{
	auto graphicsAPI = GraphicsAPI::get();
	for (uint8 i = 0; i < rangeCount; i++)
	{
		auto descriptorSet = descriptorSetRange[i].set;
		auto dsView = graphicsAPI->descriptorSetPool.get(descriptorSet);

		if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
		{
			ResourceExt::getReadyLock(**dsView)++;
			graphicsAPI->currentCommandBuffer->addLockResource(descriptorSet);
		}

		auto dsPipelineView = graphicsAPI->getPipelineView(
			dsView->getPipelineType(), dsView->getPipeline());
		const auto& pipelineUniforms = dsPipelineView->getUniforms();
		const auto& dsUniforms = dsView->getUniforms();

		for (const auto& dsUniform : dsUniforms)
		{
			const auto& pipelineUniform = pipelineUniforms.at(dsUniform.first);
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

						if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
						{
							ResourceExt::getReadyLock(**imageViewView)++;
							ResourceExt::getReadyLock(**imageView)++;
							graphicsAPI->currentCommandBuffer->addLockResource(ID<ImageView>(resource));
							graphicsAPI->currentCommandBuffer->addLockResource(imageViewView->getImage());
						}
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
							continue; // TODO: maybe separate into 2 paths: bindless/nonbindless?

						auto bufferView = graphicsAPI->bufferPool.get(ID<Buffer>(resource));
						if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
						{
							ResourceExt::getReadyLock(**bufferView)++;
							graphicsAPI->currentCommandBuffer->addLockResource(ID<Buffer>(resource));
						}
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
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(variant < variantCount);
	GARDEN_ASSERT(!GraphicsAPI::get()->isCurrentRenderPassAsync);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);

	auto graphicsAPI = GraphicsAPI::get();
	auto pipeline = graphicsAPI->getPipeline(type, this);

	if (type == PipelineType::Graphics)
	{
		if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
		{
			readyLock++;
			graphicsAPI->currentCommandBuffer->addLockResource(ID<GraphicsPipeline>(pipeline));
		}
	}
	else if (type == PipelineType::Compute)
	{
		if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
		{
			readyLock++;
			graphicsAPI->currentCommandBuffer->addLockResource(ID<ComputePipeline>(pipeline));
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
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(asyncRecording);
	GARDEN_ASSERT(variant < variantCount);
	GARDEN_ASSERT(threadIndex < GraphicsAPI::get()->threadCount);
	GARDEN_ASSERT(GraphicsAPI::get()->isCurrentRenderPassAsync);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);

	auto graphicsAPI = GraphicsAPI::get();
	auto pipeline = graphicsAPI->getPipeline(type, this);

	if (type == PipelineType::Graphics)
	{
		graphicsAPI->currentCommandBuffer->commandMutex.lock();
		if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
		{
			readyLock++;
			graphicsAPI->currentCommandBuffer->addLockResource(ID<GraphicsPipeline>(pipeline));
		}
		graphicsAPI->currentCommandBuffer->commandMutex.unlock();
	}
	else if (type == PipelineType::Compute)
	{
		graphicsAPI->currentCommandBuffer->commandMutex.lock();
		if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
		{
			readyLock++;
			graphicsAPI->currentCommandBuffer->addLockResource(ID<ComputePipeline>(pipeline));
		}
		graphicsAPI->currentCommandBuffer->commandMutex.unlock();
	}
	else abort();

	auto autoThreadCount = graphicsAPI->calcAutoThreadCount(threadIndex);
	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		auto bindPoint = toVkPipelineBindPoint(type);

		while (threadIndex < autoThreadCount)
		{
			if (pipeline != graphicsAPI->currentPipelines[threadIndex] ||
				type != graphicsAPI->currentPipelineTypes[threadIndex])
			{
				vk::Pipeline pipeline = variantCount > 1 ? ((VkPipeline*)instance)[variant] : (VkPipeline)instance;
				vulkanAPI->secondaryCommandBuffers[threadIndex].bindPipeline(bindPoint, pipeline);
			}
			threadIndex++;
		}
	}
	else abort();
}

//**********************************************************************************************************************
void Pipeline::bindDescriptorSets(const DescriptorSet::Range* descriptorSetRange, uint8 rangeCount)
{
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(descriptorSetRange);
	GARDEN_ASSERT(rangeCount > 0);
	GARDEN_ASSERT(!GraphicsAPI::get()->isCurrentRenderPassAsync);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);
	auto graphicsAPI = GraphicsAPI::get();

	#if GARDEN_DEBUG
	for (uint8 i = 0; i < rangeCount; i++)
	{
		auto descriptor = descriptorSetRange[i];
		GARDEN_ASSERT(descriptor.set);
		auto descriptorSetView = graphicsAPI->descriptorSetPool.get(descriptor.set);
		GARDEN_ASSERT(descriptor.offset + descriptor.count <= descriptorSetView->getSetCount());
		auto pipeline = graphicsAPI->getPipeline(descriptorSetView->getPipelineType(), this);
		GARDEN_ASSERT(pipeline == descriptorSetView->getPipeline());
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
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(asyncRecording);
	GARDEN_ASSERT(descriptorSetRange);
	GARDEN_ASSERT(rangeCount > 0);
	GARDEN_ASSERT(threadIndex < GraphicsAPI::get()->threadCount);
	GARDEN_ASSERT(GraphicsAPI::get()->isCurrentRenderPassAsync);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);
	auto graphicsAPI = GraphicsAPI::get();

	#if GARDEN_DEBUG
	for (uint8 i = 0; i < rangeCount; i++)
	{
		auto descriptor = descriptorSetRange[i];
		GARDEN_ASSERT(descriptor.set);
		auto descriptorSetView = graphicsAPI->descriptorSetPool.get(descriptor.set);
		GARDEN_ASSERT(descriptor.offset + descriptor.count <= descriptorSetView->getSetCount());
		auto pipeline = graphicsAPI->getPipeline(descriptorSetView->getPipelineType(), this);
		GARDEN_ASSERT(pipeline == descriptorSetView->getPipeline());
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
void Pipeline::pushConstants()
{
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(pushConstantsSize > 0);
	GARDEN_ASSERT(!GraphicsAPI::get()->isCurrentRenderPassAsync);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);

	PushConstantsCommand command;
	command.dataSize = pushConstantsSize;
	command.shaderStages = pushConstantsMask;
	command.pipelineLayout = pipelineLayout;
	command.data = pushConstantsBuffer.data();
	GraphicsAPI::get()->currentCommandBuffer->addCommand(command);
}
void Pipeline::pushConstantsAsync(int32 threadIndex)
{
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(asyncRecording);
	GARDEN_ASSERT(pushConstantsSize > 0);
	GARDEN_ASSERT(threadIndex >= 0);
	GARDEN_ASSERT(threadIndex < GraphicsAPI::get()->threadCount);
	GARDEN_ASSERT(GraphicsAPI::get()->isCurrentRenderPassAsync);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		VulkanAPI::get()->secondaryCommandBuffers[threadIndex].pushConstants(
			vk::PipelineLayout((VkPipelineLayout)pipelineLayout),
			(vk::ShaderStageFlags)pushConstantsMask, 0, pushConstantsSize,
			(const uint8*)pushConstantsBuffer.data() + pushConstantsSize * threadIndex);
	}
	else abort();
}