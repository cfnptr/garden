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

#include "garden/graphics/pipeline.hpp"
#include "garden/graphics/vulkan.hpp"
#include "garden/xxhash.hpp"
#include "mpio/directory.hpp"

#include <fstream>

using namespace std;
using namespace mpio;
using namespace garden;
using namespace garden::graphics;

//--------------------------------------------------------------------------------------------------
static vk::Filter toVkFilter(SamplerFilter filterType)
{
	switch (filterType)
	{
	case SamplerFilter::Nearest: return vk::Filter::eNearest;
	case SamplerFilter::Linear: return vk::Filter::eLinear;
	default: abort();
	}
}
static vk::SamplerMipmapMode toVkSamplerMipmamMode(SamplerFilter filterType)
{
	switch (filterType)
	{
	case SamplerFilter::Nearest: return vk::SamplerMipmapMode::eNearest;
	case SamplerFilter::Linear: return vk::SamplerMipmapMode::eLinear;
	default: abort();
	}
}
static vk::SamplerAddressMode toVkSamplerAddressMode(Pipeline::SamplerWrap samplerWrap)
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
static vk::BorderColor toVkBorderColor(Pipeline::BorderColor borderColor)
{
	switch (borderColor)
	{
		case Pipeline::BorderColor::FloatTransparentBlack:
			return vk::BorderColor::eFloatTransparentBlack;
		case Pipeline::BorderColor::IntTransparentBlack:
			return vk::BorderColor::eIntTransparentBlack;
		case Pipeline::BorderColor::FloatOpaqueBlack:
			return vk::BorderColor::eFloatOpaqueBlack;
		case Pipeline::BorderColor::IntOpaqueBlack:
			return vk::BorderColor::eIntOpaqueBlack;
		case Pipeline::BorderColor::FloatOpaqueWhite:
			return vk::BorderColor::eFloatOpaqueWhite;
		case Pipeline::BorderColor::IntOpaqueWhite:
			return vk::BorderColor::eIntOpaqueWhite;
		default: abort();
	}
}

//--------------------------------------------------------------------------------------------------
namespace
{
	struct PipelineCacheHeader final
	{
		char							magic[4];
		uint32							engineVersion;
		uint32							appVersion;
		uint32							dataSize;
		Hash128							dataHash;
		uint32	 						driverVersion;
    	uint32	 						driverABI;
		VkPipelineCacheHeaderVersionOne	cache;
	};
}

static vk::PipelineCache createPipelineCache(
	bool& isCacheLoaded, const fs::path& pipelinePath)
{
	auto hashState = XXH3_createState();
	auto pathString = pipelinePath.generic_string();
	if (XXH3_128bits_reset(hashState) == XXH_ERROR) abort();
	if (XXH3_128bits_update(hashState, pathString.c_str(),
		pathString.length()) == XXH_ERROR) abort();
	auto hashResult = XXH3_128bits_digest(hashState);
	auto pipelineHash = Hash128(hashResult.low64, hashResult.high64);

	const auto cacheHeaderSize = sizeof(PipelineCacheHeader) -
		sizeof(VkPipelineCacheHeaderVersionOne);
	auto appDataPath = Directory::getAppDataPath(GARDEN_APP_NAME_LOWERCASE_STRING);
	auto path = appDataPath / "caches/shaders" / pipelineHash.toString();
	ifstream inputStream(path, ios::in | ios::binary | ios::ate);
	vector<uint8> fileData;

	if (inputStream.is_open())
	{
		auto fileSize = (psize)inputStream.tellg();
		if (fileSize > sizeof(PipelineCacheHeader))
		{
			fileData.resize(fileSize);
			inputStream.seekg(0, ios::beg);

			if (inputStream.read((char*)fileData.data(), fileSize))
			{
				if (XXH3_128bits_reset(hashState) == XXH_ERROR) abort();
				if (XXH3_128bits_update(hashState, fileData.data() + cacheHeaderSize,
					(psize)fileSize - cacheHeaderSize) == XXH_ERROR) abort();
				hashResult = XXH3_128bits_digest(hashState);

				PipelineCacheHeader targetHeader;
				memcpy(targetHeader.magic, "GSLC", 4);
				targetHeader.engineVersion = VK_MAKE_API_VERSION(0, GARDEN_VERSION_MAJOR,
					GARDEN_VERSION_MINOR, GARDEN_VERSION_PATCH);
				targetHeader.appVersion = VK_MAKE_API_VERSION(0, GARDEN_APP_VERSION_MAJOR,
					GARDEN_APP_VERSION_MINOR, GARDEN_APP_VERSION_PATCH);
				targetHeader.dataSize = (uint32)(fileSize - cacheHeaderSize);
				targetHeader.dataHash = Hash128(hashResult.low64, hashResult.high64);
				targetHeader.driverVersion =
					Vulkan::deviceProperties.properties.driverVersion;
				targetHeader.driverABI = sizeof(void*);
				targetHeader.cache.headerSize = sizeof(VkPipelineCacheHeaderVersionOne);
				targetHeader.cache.headerVersion = VK_PIPELINE_CACHE_HEADER_VERSION_ONE;
				targetHeader.cache.vendorID = Vulkan::deviceProperties.properties.vendorID;
				targetHeader.cache.deviceID = Vulkan::deviceProperties.properties.deviceID;
				memcpy(targetHeader.cache.pipelineCacheUUID,
					Vulkan::deviceProperties.properties.pipelineCacheUUID, VK_UUID_SIZE);

				isCacheLoaded = memcmp(fileData.data(),
					&targetHeader, sizeof(PipelineCacheHeader)) == 0;
			}
		}
	}

	XXH3_freeState(hashState);

	vk::PipelineCacheCreateInfo cacheInfo;
	if (isCacheLoaded)
	{
		cacheInfo.initialDataSize = (uint32)(fileData.size() - cacheHeaderSize);
		cacheInfo.pInitialData = fileData.data() + cacheHeaderSize;
	}

	auto cache = Vulkan::device.createPipelineCache(cacheInfo);

	#if GARDEN_DEBUG
	if (Vulkan::hasDebugUtils)
	{
		auto name = "pipelineCache." + pipelinePath.generic_string();
		vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::ePipelineCache,
			(uint64)(VkPipelineCache)cache, name.c_str());
		Vulkan::device.setDebugUtilsObjectNameEXT(nameInfo, Vulkan::dynamicLoader);
	}
	#endif

	return cache;
}

//--------------------------------------------------------------------------------------------------
static vector<void*> createPipelineSamplers(
	const map<string, Pipeline::SamplerState>& samplerStates, 
	map<string, void*>& immutableSamplers, const fs::path& pipelinePath)
{
	vector<void*> samplers(samplerStates.size());
	uint32 i = 0;

	for (auto it = samplerStates.begin(); it != samplerStates.end(); it++, i++)
	{
		auto state = it->second;
		vk::SamplerCreateInfo samplerInfo({},
			toVkFilter(state.magFilter), toVkFilter(state.minFilter),
			toVkSamplerMipmamMode(state.mipmapFilter), toVkSamplerAddressMode(state.wrapX),
			toVkSamplerAddressMode(state.wrapY), toVkSamplerAddressMode(state.wrapZ),
			state.mipLodBias, state.anisoFiltering, state.maxAnisotropy,
			state.comparing, toVkCompareOp(state.compareOperation), state.minLod,
			state.maxLod == INFINITY ? VK_LOD_CLAMP_NONE : state.maxLod,
			toVkBorderColor(state.borderColor), state.unnormCoords);
		auto sampler = Vulkan::device.createSampler(samplerInfo);
		samplers[i] = (VkSampler)sampler;
		immutableSamplers.emplace(it->first, (VkSampler)sampler);

		#if GARDEN_DEBUG
		if (Vulkan::hasDebugUtils)
		{
			auto name = "sampler." + pipelinePath.generic_string() + "." + it->first;
			vk::DebugUtilsObjectNameInfoEXT nameInfo(
				vk::ObjectType::eSampler, (uint64)(VkSampler)sampler, name.c_str());
			Vulkan::device.setDebugUtilsObjectNameEXT(nameInfo, Vulkan::dynamicLoader);
		}
		#endif
	}
	
	return samplers;
}

//--------------------------------------------------------------------------------------------------
static void createDescriptorSetLayouts(vector<void*>& descriptorSetLayouts,
	vector<void*>& descriptorPools, const map<string, Pipeline::Uniform>& uniforms,
	const map<string, void*>& immutableSamplers, const fs::path& pipelinePath,
	uint32 maxBindlessCount, bool& bindless)
{
	bindless = false;

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
			if (uniform.descriptorSetIndex != i) continue;

			auto& descriptorSetBinding = descriptorSetBindings[bindingIndex];
			descriptorSetBinding.binding = (uint32)uniform.bindingIndex;
			descriptorSetBinding.descriptorType = toVkDescriptorType(uniform.type);
			descriptorSetBinding.stageFlags = toVkShaderStages(uniform.shaderStages);

			if (uniform.arraySize > 0)
			{
				if (isSamplerType(uniform.type))
				{
					descriptorSetBinding.pImmutableSamplers =
						(vk::Sampler*)&immutableSamplers.at(pair.first);
				}

				descriptorSetBinding.descriptorCount = uniform.arraySize;
			}
			else
			{
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
					vector<vk::Sampler> samplers(maxBindlessCount,
						*(vk::Sampler*)&immutableSamplers.at(pair.first));
					samplerArrays.push_back(std::move(samplers));
					descriptorSetBinding.pImmutableSamplers =
						samplerArrays[samplerArrays.size() - 1].data();
				}

				descriptorBindingFlags[bindingIndex] =
					vk::DescriptorBindingFlagBits::eUpdateAfterBind |
					vk::DescriptorBindingFlagBits::ePartiallyBound;
				descriptorSetBinding.descriptorCount = maxBindlessCount;
				isBindless = true;
			}

			bindingIndex++;
		}

		vk::DescriptorSetLayoutCreateInfo descriptorSetLayoutInfo({},
			bindingIndex, descriptorSetBindings.data());
		vk::DescriptorSetLayoutBindingFlagsCreateInfo descriptorSetFlagsInfo;

		if (isBindless)
		{
			GARDEN_ASSERT(maxBindlessCount > 0);
			descriptorSetFlagsInfo.bindingCount = bindingIndex;
			descriptorSetFlagsInfo.pBindingFlags = descriptorBindingFlags.data();
			descriptorSetLayoutInfo.flags =
				vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
			descriptorSetLayoutInfo.pNext = &descriptorSetFlagsInfo;
	
			uint32 maxSetCount = 0;
			for (uint32 j = 0; j < (uint32)descriptorPoolSizes.size(); j++)
			{
				if (descriptorPoolSizes[j].descriptorCount == 0)
				{
					descriptorPoolSizes.erase(descriptorPoolSizes.begin() + j);
					if (j > 0) j--;
				}
				else maxSetCount += descriptorPoolSizes[j].descriptorCount;
			}

			vk::DescriptorPoolCreateInfo descriptorPoolInfo(
				vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind, maxSetCount,
				(uint32)descriptorPoolSizes.size(), descriptorPoolSizes.data());
			descriptorPools[i] = Vulkan::device.createDescriptorPool(descriptorPoolInfo);

			#if GARDEN_DEBUG
			if (Vulkan::hasDebugUtils)
			{
				auto name = "descriptorPool." + pipelinePath.generic_string() + to_string(i);
				vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eDescriptorPool,
					(uint64)(VkSampler)descriptorPools[i], name.c_str());
				Vulkan::device.setDebugUtilsObjectNameEXT(nameInfo, Vulkan::dynamicLoader);
			}
			#endif
		}

		descriptorSetLayouts[i] = Vulkan::device.createDescriptorSetLayout(
			descriptorSetLayoutInfo);

		samplerArrays.clear();
		bindless = isBindless;

		#if GARDEN_DEBUG
		if (Vulkan::hasDebugUtils)
		{
			auto name = "descriptorSetLayout." +
				pipelinePath.generic_string() + to_string(i);
			vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eDescriptorSetLayout,
				(uint64)descriptorSetLayouts[i], name.c_str());
			Vulkan::device.setDebugUtilsObjectNameEXT(nameInfo, Vulkan::dynamicLoader);
		}
		#endif
	}
}

//--------------------------------------------------------------------------------------------------
static vk::PipelineLayout createPipelineLayout(
	uint16 pushConstantsSize, ShaderStage pushConstantsStages,
	const vector<void*>& descriptorSetLayouts, const fs::path& pipelinePath)
{
	vector<vk::PushConstantRange> pushConstantRanges;

	if (hasAnyFlag(pushConstantsStages, ShaderStage::Vertex))
	{
		pushConstantRanges.push_back(vk::PushConstantRange(
			vk::ShaderStageFlagBits::eVertex, 0, pushConstantsSize));
	}
	if (hasAnyFlag(pushConstantsStages, ShaderStage::Fragment))
	{
		pushConstantRanges.push_back(vk::PushConstantRange(
			vk::ShaderStageFlagBits::eFragment, 0, pushConstantsSize));
	}
	if (hasAnyFlag(pushConstantsStages, ShaderStage::Compute))
	{
		pushConstantRanges.push_back(vk::PushConstantRange(
			vk::ShaderStageFlagBits::eCompute, 0, pushConstantsSize));
	}

	vk::PipelineLayoutCreateInfo pipelineLayoutInfo({}, 0, nullptr,
		(uint32)pushConstantRanges.size(), pushConstantRanges.data());

	if (descriptorSetLayouts.size() > 0)
	{
		pipelineLayoutInfo.setLayoutCount = (uint32)descriptorSetLayouts.size();
		pipelineLayoutInfo.pSetLayouts =
			(const vk::DescriptorSetLayout*)descriptorSetLayouts.data();
	}

	auto layout = Vulkan::device.createPipelineLayout(pipelineLayoutInfo);

	#if GARDEN_DEBUG
	if (Vulkan::hasDebugUtils)
	{
		auto name = "pipelineLayout." + pipelinePath.generic_string();
		vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::ePipelineLayout,
			(uint64)(VkPipelineLayout)layout, name.c_str());
		Vulkan::device.setDebugUtilsObjectNameEXT(nameInfo, Vulkan::dynamicLoader);
	}
	#endif

	return layout;
}

//--------------------------------------------------------------------------------------------------
Pipeline::Pipeline(CreateData& createData, bool useAsync)
{
	this->uniforms = std::move(createData.uniforms);
	this->pipelineVersion = createData.pipelineVersion;
	this->pushConstantsMask = (uint32)toVkShaderStages(createData.pushConstantsStages);
	this->pushConstantsSize = createData.pushConstantsSize;
	this->pipelineCache = createPipelineCache(this->cacheLoaded, createData.path);
	this->variantCount = createData.variantCount;

	map<string, void*> immutableSamplers;
	this->samplers = createPipelineSamplers(
		createData.samplerStates, immutableSamplers, createData.path);

	if (createData.descriptorSetCount > 0)
	{
		descriptorSetLayouts.resize(createData.descriptorSetCount);
		descriptorPools.resize(createData.descriptorSetCount);
	}

	createDescriptorSetLayouts(descriptorSetLayouts, descriptorPools, uniforms,
		immutableSamplers, createData.path, createData.maxBindlessCount, bindless);

	this->pipelineLayout = createPipelineLayout(pushConstantsSize,
		createData.pushConstantsStages, descriptorSetLayouts, createData.path);
	
	if (pushConstantsSize > 0)
	{
		auto mult = useAsync ? thread::hardware_concurrency() : 1;
		pushConstantsBuffer.resize(pushConstantsSize * mult);
	}
}

//--------------------------------------------------------------------------------------------------
bool Pipeline::destroy()
{
	if (isBusy()) return false;

	if (!this->cacheLoaded)
	{
		auto cacheData = Vulkan::device.getPipelineCacheData(
			(VkPipelineCache)pipelineCache);
		if (cacheData.size() > sizeof(VkPipelineCacheHeaderVersionOne))
		{
			auto hashState = (XXH3_state_t*)GraphicsAPI::hashState;
			auto pathString = pipelinePath.generic_string();
			if (XXH3_128bits_reset(hashState) == XXH_ERROR) abort();
			if (XXH3_128bits_update(hashState, pathString.c_str(),
				pathString.length()) == XXH_ERROR) abort();
			auto hashResult = XXH3_128bits_digest(hashState);
			auto pipelineHash = Hash128(hashResult.low64, hashResult.high64);

			auto appDataPath = Directory::getAppDataPath(GARDEN_APP_NAME_LOWERCASE_STRING);
			auto directory = appDataPath / "caches/shaders";
			if (!fs::exists(directory)) fs::create_directories(directory);
			auto path = directory / pipelineHash.toString();
			ofstream outputStream(path, ios::out | ios::binary);

			if (outputStream.is_open())
			{
				outputStream.write("GSLC", 4);
				const uint32 engineVersion = VK_MAKE_API_VERSION(0, GARDEN_VERSION_MAJOR,
					GARDEN_VERSION_MINOR, GARDEN_VERSION_PATCH);
				const uint32 appVersion = VK_MAKE_API_VERSION(0, GARDEN_APP_VERSION_MAJOR,
					GARDEN_APP_VERSION_MINOR, GARDEN_APP_VERSION_PATCH);
				outputStream.write((const char*)&engineVersion, sizeof(uint32));
				outputStream.write((const char*)&appVersion, sizeof(uint32));
				auto dataSize = (uint32)cacheData.size();
				outputStream.write((const char*)&dataSize, sizeof(uint32));
				if (XXH3_128bits_reset(hashState) == XXH_ERROR) abort();
				if (XXH3_128bits_update(hashState, cacheData.data(),
					cacheData.size()) == XXH_ERROR) abort();
				auto hashResult = XXH3_128bits_digest(hashState);
				auto hash = Hash128(hashResult.low64, hashResult.high64);
				outputStream.write((const char*)&hash, sizeof(Hash128));
				outputStream.write((const char*)
					&Vulkan::deviceProperties.properties.driverVersion, sizeof(uint32));
				auto driverABI = (uint32)sizeof(void*);
				outputStream.write((const char*)&driverABI, sizeof(uint32));
				outputStream.write((const char*)cacheData.data(), cacheData.size());
			}
		}
	}

	if (GraphicsAPI::isRunning)
	{
		GraphicsAPI::destroyResource(GraphicsAPI::DestroyResourceType::Pipeline,
			instance, pipelineLayout, pipelineCache, variantCount - 1);

		for (auto descriptorSetLayout : descriptorSetLayouts)
		{
			GraphicsAPI::destroyResource(GraphicsAPI::DestroyResourceType::DescriptorSetLayout,
				descriptorSetLayout);
		}
		for (auto descriptorPool : descriptorPools)
		{
			if (!descriptorPool) continue;
			GraphicsAPI::destroyResource(GraphicsAPI::DestroyResourceType::DescriptorPool,
				descriptorPool);
		}

		for (auto sampler : samplers)
			GraphicsAPI::destroyResource(GraphicsAPI::DestroyResourceType::Sampler, sampler);
	}
	else
	{
		if (variantCount > 1)
		{
			for (uint8 i = 0; i < variantCount; i++)
				Vulkan::device.destroyPipeline(((VkPipeline*)instance)[i]);
			free(instance);
		}
		else Vulkan::device.destroyPipeline((VkPipeline)instance);
		
		Vulkan::device.destroyPipelineLayout((VkPipelineLayout)pipelineLayout);
		Vulkan::device.destroyPipelineCache((VkPipelineCache)pipelineCache);

		for (auto descriptorSetLayout : descriptorSetLayouts)
		{
			Vulkan::device.destroyDescriptorSetLayout(vk::DescriptorSetLayout(
				(VkDescriptorSetLayout)descriptorSetLayout));
		}
		for (auto descriptorPool : descriptorPools)
		{
			if (!descriptorPool) continue;
			Vulkan::device.destroyDescriptorPool(vk::DescriptorPool(
				(VkDescriptorPool)descriptorPool));
		}

		for (auto sampler : samplers)
			Vulkan::device.destroySampler((VkSampler)sampler);
	}

	instance = nullptr;
	return true;
}

//--------------------------------------------------------------------------------------------------
vector<void*> Pipeline::createShaders(
	const vector<vector<uint8>>& code, const fs::path& pipelinePath)
{
	vector<void*> shaders(code.size());

	for (uint8 i = 0; i < (uint8)code.size(); i++)
	{
		auto& shaderCode = code[i];
		vk::ShaderModuleCreateInfo shaderInfo({},
			(uint32)shaderCode.size(), (const uint32*)shaderCode.data());
		shaders[i] = (VkShaderModule)Vulkan::device.createShaderModule(shaderInfo);

		#if GARDEN_DEBUG
		if (Vulkan::hasDebugUtils)
		{
			auto _name = "shaderModule." + pipelinePath.generic_string() + to_string(i);
			vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eShaderModule,
				(uint64)shaders[i], _name.c_str());
			Vulkan::device.setDebugUtilsObjectNameEXT(nameInfo, Vulkan::dynamicLoader);
		}
		#endif
	}
	
	return shaders;
}
void Pipeline::destroyShaders(const vector<void*>& shaders)
{
	for (auto shader : shaders)
		Vulkan::device.destroyShaderModule((VkShaderModule)shader);
}

//--------------------------------------------------------------------------------------------------
void Pipeline::updateDescriptorTime(
	const DescriptorData* descriptorData, uint8 descriptorDataSize)
{
	for (uint8 i = 0; i < descriptorDataSize; i++)
	{
		auto dsView = GraphicsAPI::descriptorSetPool.get(descriptorData[i].set);
		if (GraphicsAPI::currentCommandBuffer == &GraphicsAPI::graphicsCommandBuffer)
			dsView->lastGraphicsTime = GraphicsAPI::graphicsCommandBuffer.getBusyTime();
		else if (GraphicsAPI::currentCommandBuffer == &GraphicsAPI::computeCommandBuffer)
			dsView->lastComputeTime = GraphicsAPI::computeCommandBuffer.getBusyTime();
		else dsView->lastFrameTime = GraphicsAPI::frameCommandBuffer.getBusyTime();

		map<string, Pipeline::Uniform>* pipelineUniforms;
		if (dsView->pipelineType == PipelineType::Graphics)
		{
			pipelineUniforms = &GraphicsAPI::graphicsPipelinePool.get(
				ID<GraphicsPipeline>(dsView->pipeline))->uniforms;
		}
		else if (dsView->pipelineType == PipelineType::Compute)
		{
			pipelineUniforms = &GraphicsAPI::computePipelinePool.get(
				ID<ComputePipeline>(dsView->pipeline))->uniforms;
		}
		else abort();

		auto& dsUniforms = dsView->uniforms;
		for (auto& dsUniform : dsUniforms)
		{
			auto& pipelineUniform = pipelineUniforms->at(dsUniform.first);
			auto uniformType = pipelineUniform.type;

			if (isSamplerType(uniformType) || isImageType(uniformType) ||
				uniformType == GslUniformType::SubpassInput)
			{
				for (auto& resourceArray : dsUniform.second.resourceSets)
				{
					for (auto resource : resourceArray)
					{
						if (!resource) continue; // TODO: maybe separate into 2 paths: bindless/nonbindless?
						auto imageViewView = GraphicsAPI::imageViewPool.get(
							ID<ImageView>(resource));
						auto imageView = GraphicsAPI::imagePool.get(imageViewView->image);

						if (GraphicsAPI::currentCommandBuffer ==
							&GraphicsAPI::graphicsCommandBuffer)
						{
							imageViewView->lastGraphicsTime = imageView->lastGraphicsTime =
								GraphicsAPI::graphicsCommandBuffer.getBusyTime();
						}
						else if (GraphicsAPI::currentCommandBuffer ==
							&GraphicsAPI::computeCommandBuffer)
						{
							imageViewView->lastComputeTime = imageView->lastComputeTime =
								GraphicsAPI::computeCommandBuffer.getBusyTime();
						}
						else
						{
							imageViewView->lastFrameTime = imageView->lastFrameTime =
								GraphicsAPI::frameCommandBuffer.getBusyTime();
						}
					}
				}
			}
			else if (uniformType == GslUniformType::UniformBuffer ||
				uniformType == GslUniformType::StorageBuffer)
			{
				for (auto& resourceArray : dsUniform.second.resourceSets)
				{
					for (auto resource : resourceArray)
					{
						if (!resource) continue; // TODO: maybe separate into 2 paths: bindless/nonbindless?
						auto bufferiew = GraphicsAPI::bufferPool.get(ID<Buffer>(resource));
						
						if (GraphicsAPI::currentCommandBuffer ==
							&GraphicsAPI::graphicsCommandBuffer)
						{
							bufferiew->lastGraphicsTime =
								GraphicsAPI::graphicsCommandBuffer.getBusyTime();
						}
						else if (GraphicsAPI::currentCommandBuffer ==
							&GraphicsAPI::computeCommandBuffer)
						{
							bufferiew->lastComputeTime =
								GraphicsAPI::computeCommandBuffer.getBusyTime();
						}
						else
						{
							bufferiew->lastFrameTime =
								GraphicsAPI::frameCommandBuffer.getBusyTime();
						}
					}
				}
			}
			else abort();
		}
	}
}

//--------------------------------------------------------------------------------------------------
void Pipeline::bind(uint8 variant)
{
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(variant < variantCount);
	GARDEN_ASSERT(!Framebuffer::isCurrentRenderPassAsync());
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);

	ID<Pipeline> pipeline;
	if (type == PipelineType::Graphics)
	{
		pipeline = ID<Pipeline>(GraphicsAPI::graphicsPipelinePool.
			getID((GraphicsPipeline*)this));
	}
	else if (type == PipelineType::Compute)
	{
		pipeline = ID<Pipeline>(GraphicsAPI::computePipelinePool.
			getID((ComputePipeline*)this));
	}
	else abort();

	BindPipelineCommand command;
	command.pipelineType = type;
	command.variant = variant;
	command.pipeline = pipeline;
	GraphicsAPI::currentCommandBuffer->addCommand(command);

	if (GraphicsAPI::currentCommandBuffer == &GraphicsAPI::graphicsCommandBuffer)
		lastGraphicsTime = GraphicsAPI::graphicsCommandBuffer.getBusyTime();
	else if (GraphicsAPI::currentCommandBuffer == &GraphicsAPI::computeCommandBuffer)
		lastComputeTime = GraphicsAPI::computeCommandBuffer.getBusyTime();
	else lastFrameTime = GraphicsAPI::frameCommandBuffer.getBusyTime();
}
void Pipeline::bindAsync(uint8 variant, int32 taskIndex)
{
	GARDEN_ASSERT(useAsync);
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(variant < variantCount);
	GARDEN_ASSERT(taskIndex < 0 || taskIndex < thread::hardware_concurrency());
	GARDEN_ASSERT(Framebuffer::isCurrentRenderPassAsync());
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);

	int32 taskCount;
	if (taskIndex < 0)
	{
		taskIndex = 0;
		taskCount = (uint32)Vulkan::secondaryCommandBuffers.size();
	}
	else taskCount = taskIndex + 1;

	ID<Pipeline> pipeline;
	if (type == PipelineType::Graphics)
	{
		pipeline = ID<Pipeline>(GraphicsAPI::graphicsPipelinePool.
			getID((GraphicsPipeline*)this));
	}
	else if (type == PipelineType::Compute)
	{
		pipeline = ID<Pipeline>(GraphicsAPI::computePipelinePool.
			getID((ComputePipeline*)this));
	}
	else abort();

	auto bindPoint = toVkPipelineBindPoint(type);
	while (taskIndex < taskCount)
	{
		if (pipeline != Framebuffer::currentPipelines[taskIndex] ||
			type != Framebuffer::currentPipelineTypes[taskIndex])
		{
			vk::Pipeline pipeline = variantCount > 1 ?
				((VkPipeline*)instance)[variant] : (VkPipeline)instance;
			Vulkan::secondaryCommandBuffers[taskIndex].bindPipeline(bindPoint, pipeline);
		}
		taskIndex++;
	}

	if (GraphicsAPI::currentCommandBuffer == &GraphicsAPI::graphicsCommandBuffer)
		lastGraphicsTime = GraphicsAPI::graphicsCommandBuffer.getBusyTime();
	else if (GraphicsAPI::currentCommandBuffer == &GraphicsAPI::computeCommandBuffer)
		lastComputeTime = GraphicsAPI::computeCommandBuffer.getBusyTime();
	else lastFrameTime = GraphicsAPI::frameCommandBuffer.getBusyTime();
}

//--------------------------------------------------------------------------------------------------
void Pipeline::bindDescriptorSets(
	const DescriptorData* descriptorData, uint8 descriptorDataSize)
{
	GARDEN_ASSERT(descriptorDataSize > 0);
	GARDEN_ASSERT(descriptorData);
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(!Framebuffer::isCurrentRenderPassAsync());
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);
	
	#if GARDEN_DEBUG
	for (uint8 i = 0; i < descriptorDataSize; i++)
	{
		auto descriptor = descriptorData[i];
		GARDEN_ASSERT(descriptor.set);
		auto descriptorSetView = GraphicsAPI::descriptorSetPool.get(descriptor.set);
		GARDEN_ASSERT(descriptor.offset + descriptor.count <=
			descriptorSetView->getSetCount());
		
		auto pipelineType = descriptorSetView->pipelineType;
		if (pipelineType == PipelineType::Graphics)
		{
			GARDEN_ASSERT(ID<GraphicsPipeline>(descriptorSetView->pipeline) ==
				GraphicsAPI::graphicsPipelinePool.getID(((const GraphicsPipeline*)this)));
		}
		else if (pipelineType == PipelineType::Compute)
		{
			GARDEN_ASSERT(ID<ComputePipeline>(descriptorSetView->pipeline) ==
				GraphicsAPI::computePipelinePool.getID(((const ComputePipeline*)this)));
		}
		else abort();
	}
	#endif
	
	BindDescriptorSetsCommand command;
	command.descriptorDataSize = descriptorDataSize;
	command.descriptorData = descriptorData;
	GraphicsAPI::currentCommandBuffer->addCommand(command);

	updateDescriptorTime(descriptorData, descriptorDataSize);
}

//--------------------------------------------------------------------------------------------------
static thread_local vector<vk::DescriptorSet> descriptorSets;

void Pipeline::bindDescriptorSetsAsync(const DescriptorData* descriptorData,
	uint8 descriptorDataSize, int32 taskIndex)
{
	GARDEN_ASSERT(descriptorData);
	GARDEN_ASSERT(descriptorDataSize > 0);
	GARDEN_ASSERT(useAsync);
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(taskIndex < 0 || taskIndex < thread::hardware_concurrency());
	GARDEN_ASSERT(Framebuffer::isCurrentRenderPassAsync());
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);

	for (uint8 i = 0; i < descriptorDataSize; i++)
	{
		auto descriptor = descriptorData[i];
		GARDEN_ASSERT(descriptor.set);
		auto descriptorSetView = GraphicsAPI::descriptorSetPool.get(descriptor.set);
		GARDEN_ASSERT(descriptor.offset + descriptor.count <=
			descriptorSetView->getSetCount());
		
		auto instance = (vk::DescriptorSet*)descriptorSetView->instance;
		if (descriptorSetView->getSetCount() > 1)
		{
			auto count = descriptor.offset + descriptor.count;
			for (uint32 j = descriptor.offset; j < count; j++)
				descriptorSets.push_back(instance[j]);
		}
		else descriptorSets.push_back((VkDescriptorSet)instance);

		#if GARDEN_DEBUG
		auto pipelineType = descriptorSetView->pipelineType;
		if (pipelineType == PipelineType::Graphics)
		{
			GARDEN_ASSERT(ID<GraphicsPipeline>(descriptorSetView->pipeline) ==
				GraphicsAPI::graphicsPipelinePool.getID(((const GraphicsPipeline*)this)));
		}
		else if (pipelineType == PipelineType::Compute)
		{
			GARDEN_ASSERT(ID<ComputePipeline>(descriptorSetView->pipeline) ==
				GraphicsAPI::computePipelinePool.getID(((const ComputePipeline*)this)));
		}
		else abort();
		#endif
	}

	int32 taskCount;
	if (taskIndex < 0)
	{
		taskIndex = 0;
		taskCount = (uint32)Vulkan::secondaryCommandBuffers.size();
	}
	else taskCount = taskIndex + 1;

	auto bindPoint = toVkPipelineBindPoint(type);
	while (taskIndex < taskCount)
	{
		Vulkan::secondaryCommandBuffers[taskIndex++].bindDescriptorSets(
			bindPoint, (VkPipelineLayout)pipelineLayout, 0,
			(uint32)descriptorSets.size(), descriptorSets.data(), 0, nullptr);
	}

	BindDescriptorSetsCommand command;
	command.isAsync = true;
	command.descriptorDataSize = descriptorDataSize;
	command.descriptorData = descriptorData;
	GraphicsAPI::currentCommandBuffer->addCommand(command);
	descriptorSets.clear();

	updateDescriptorTime(descriptorData, descriptorDataSize);
}

//--------------------------------------------------------------------------------------------------
void Pipeline::pushConstants()
{
	GARDEN_ASSERT(pushConstantsSize > 0);
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(!Framebuffer::isCurrentRenderPassAsync());
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);

	PushConstantsCommand command;
	command.dataSize = pushConstantsSize;
	command.shaderStages = pushConstantsMask;
	command.pipelineLayout = pipelineLayout;
	command.data = pushConstantsBuffer.data();
	GraphicsAPI::currentCommandBuffer->addCommand(command);
}
void Pipeline::pushConstantsAsync(int32 taskIndex)
{
	GARDEN_ASSERT(pushConstantsSize > 0);
	GARDEN_ASSERT(useAsync);
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(taskIndex >= 0);
	GARDEN_ASSERT(taskIndex < thread::hardware_concurrency());
	GARDEN_ASSERT(Framebuffer::isCurrentRenderPassAsync());
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);

	Vulkan::secondaryCommandBuffers[taskIndex].pushConstants(
		vk::PipelineLayout((VkPipelineLayout)pipelineLayout),
		(vk::ShaderStageFlags)pushConstantsMask, 0, pushConstantsSize,
		(const uint8*)pushConstantsBuffer.data() + pushConstantsSize * taskIndex);
}