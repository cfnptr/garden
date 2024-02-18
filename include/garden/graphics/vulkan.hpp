// Copyright 2022-2024 Nikita Fediuchin. All rights reserved.
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

/***********************************************************************************************************************
 * @file
 * @brief Common Vulkan API functions.
 */

#pragma once
#include "garden/graphics/api.hpp"
#include "garden/graphics/swapchain.hpp"

namespace garden::graphics
{

#define VK_DS_POOL_COMBINED_SAMPLER_COUNT 128
#define VK_DS_POOL_STORAGE_IMAGE_COUNT 128
#define VK_DS_POOL_UNIFORM_BUFFER_COUNT 128
#define VK_DS_POOL_STORAGE_BUFFER_COUNT 128
#define VK_DS_POOL_INPUT_ATTACHMENT_COUNT 128

/**
 * @brief Low-level cross-platform graphics API.
 * 
 * @details
 * Vulkan API is a modern, low-level graphics and compute API developed by the Khronos Group. It is designed to 
 * provide high-efficiency, cross-platform access to modern GPUs used in a wide range of devices from PCs and 
 * consoles to mobile devices and embedded platforms. Vulkan aims to offer better control over graphics hardware 
 * than older graphics APIs like OpenGL, providing developers with more direct control over GPU operations and 
 * memory management. This approach helps in reducing driver overhead and improving performance, 
 * especially in applications where CPU bottlenecks are a concern.
 */
class Vulkan final
{
public:
	static uint32 versionMajor;
	static uint32 versionMinor;
	static vk::Instance instance;
	static vk::DispatchLoaderDynamic dynamicLoader;
	static vk::PhysicalDevice physicalDevice;
	static vk::SurfaceKHR surface;
	static uint32 graphicsQueueFamilyIndex;
	static uint32 transferQueueFamilyIndex;
	static uint32 computeQueueFamilyIndex;
	static vk::Device device;
	static VmaAllocator memoryAllocator;
	static vk::Queue frameQueue;
	static vk::Queue graphicsQueue;
	static vk::Queue transferQueue;
	static vk::Queue computeQueue;
	static vk::CommandPool frameCommandPool;
	static vk::CommandPool graphicsCommandPool;
	static vk::CommandPool transferCommandPool;
	static vk::CommandPool computeCommandPool;
	static vk::DescriptorPool descriptorPool;
	static vk::PipelineCache pipelineCache;
	static vector<vk::CommandBuffer> secondaryCommandBuffers;
	static vector<bool> secondaryCommandStates;
	static Swapchain swapchain;
	static vk::PhysicalDeviceProperties2 deviceProperties;
	static vk::PhysicalDeviceFeatures2 deviceFeatures;
	static bool hasMemoryBudget;
	static bool hasMemoryPriority;
	static bool hasPageableMemory;
	static bool hasDynamicRendering;
	static bool hasDescriptorIndexing;
	#if GARDEN_DEBUG
	static vk::DebugUtilsMessengerEXT debugMessenger;
	static bool hasDebugUtils;
	#endif

	// TODO: move rest to api.hpp and make these calls universal.

	static void initialize(const string& appName,
		const string& appDataName, Version appVersion, int2 windowSize,
		bool isFullscreen, bool useVsync, bool useTripleBuffering, bool useThreading);
	static void terminate();
	static void updateDestroyBuffer();
};

//--------------------------------------------------------------------------------------------------
static vk::Format toVkFormat(Image::Format formatType)
{
	switch (formatType)
	{
	case Image::Format::UintR8: return vk::Format::eR8Uint;
	case Image::Format::UintR16: return vk::Format::eR16Uint;
	case Image::Format::UintR32: return vk::Format::eR32Uint;
	case Image::Format::UnormR8: return vk::Format::eR8Unorm;
	case Image::Format::UnormR8G8: return vk::Format::eR8G8Unorm;
	case Image::Format::UnormR8G8B8A8: return vk::Format::eR8G8B8A8Unorm;
	case Image::Format::UnormB8G8R8A8: return vk::Format::eB8G8R8A8Unorm;
	case Image::Format::SrgbR8G8B8A8: return vk::Format::eR8G8B8A8Srgb;
	case Image::Format::SrgbB8G8R8A8: return vk::Format::eB8G8R8A8Srgb;
	case Image::Format::SfloatR16G16: return vk::Format::eR16G16Sfloat;
	case Image::Format::SfloatR32G32: return vk::Format::eR32G32Sfloat;
	case Image::Format::SfloatR16G16B16A16: return vk::Format::eR16G16B16A16Sfloat;
	case Image::Format::SfloatR32G32B32A32: return vk::Format::eR32G32B32A32Sfloat;
	case Image::Format::UnormA2R10G10B10: return vk::Format::eA2R10G10B10UnormPack32;
	case Image::Format::UfloatB10G11R11: return vk::Format::eB10G11R11UfloatPack32;
	case Image::Format::UnormD16: return vk::Format::eD16Unorm;
	case Image::Format::SfloatD32: return vk::Format::eD32Sfloat;
	case Image::Format::UnormD24UintS8: return vk::Format::eD24UnormS8Uint;
	case Image::Format::SfloatD32Uint8S: return vk::Format::eD32SfloatS8Uint;
	default: abort();
	}
}
static Image::Format toImageFormat(vk::Format formatType)
{
	switch (formatType)
	{
	case vk::Format::eR8Uint: return Image::Format::UintR8;
	case vk::Format::eR16Uint: return Image::Format::UintR16;
	case vk::Format::eR32Uint: return Image::Format::UintR32;
	case vk::Format::eR8Unorm: return Image::Format::UnormR8;
	case vk::Format::eR8G8Unorm: return Image::Format::UnormR8G8;
	case vk::Format::eR8G8B8A8Unorm: return Image::Format::UnormR8G8B8A8;
	case vk::Format::eB8G8R8A8Unorm: return Image::Format::UnormB8G8R8A8;
	case vk::Format::eR8G8B8A8Srgb: return Image::Format::SrgbR8G8B8A8;
	case vk::Format::eB8G8R8A8Srgb: return Image::Format::SrgbB8G8R8A8;
	case vk::Format::eR16G16Sfloat: return Image::Format::SfloatR16G16;
	case vk::Format::eR32G32Sfloat: return Image::Format::SfloatR32G32;
	case vk::Format::eR16G16B16A16Sfloat: return Image::Format::SfloatR16G16B16A16;
	case vk::Format::eR32G32B32A32Sfloat: return Image::Format::SfloatR32G32B32A32;
	case vk::Format::eA2R10G10B10UnormPack32: return Image::Format::UnormA2R10G10B10;
	case vk::Format::eB10G11R11UfloatPack32: return Image::Format::UfloatB10G11R11;
	case vk::Format::eD16Unorm: return Image::Format::UnormD16;
	case vk::Format::eD32Sfloat: return Image::Format::SfloatD32;
	case vk::Format::eD24UnormS8Uint: return Image::Format::UnormD24UintS8;
	case vk::Format::eD32SfloatS8Uint: return Image::Format::SfloatD32Uint8S;
	default: abort();
	}
}

//--------------------------------------------------------------------------------------------------
static vk::Format toVkFormat(GslDataType type, GslDataFormat format)
{
	auto componentCount = toComponentCount(type);

	switch (componentCount)
	{
		case 1:
			switch (format)
			{
			case GslDataFormat::F32: return vk::Format::eR32Sfloat;
			case GslDataFormat::I8: return vk::Format::eR8Sint;
			case GslDataFormat::I16: return vk::Format::eR16Sint;
			case GslDataFormat::I32: return vk::Format::eR32Sint;
			case GslDataFormat::U8: return vk::Format::eR8Uint;
			case GslDataFormat::U16: return vk::Format::eR16Uint;
			case GslDataFormat::U32: return vk::Format::eR32Uint;
			default: abort();
			}
			break;
		case 2:
			switch (format)
			{
			case GslDataFormat::F32: return vk::Format::eR32G32Sfloat;
			case GslDataFormat::I8: return vk::Format::eR8G8Sint;
			case GslDataFormat::I16: return vk::Format::eR16G16Sint;
			case GslDataFormat::I32: return vk::Format::eR32G32Sint;
			case GslDataFormat::U8: return vk::Format::eR8G8Uint;
			case GslDataFormat::U16: return vk::Format::eR16G16Uint;
			case GslDataFormat::U32: return vk::Format::eR32G32Uint;
			default: abort();
			}
			break;
		case 3:
			switch (format)
			{
			case GslDataFormat::F32: return vk::Format::eR32G32B32Sfloat;
			case GslDataFormat::I8: return vk::Format::eR8G8B8Sint;
			case GslDataFormat::I16: return vk::Format::eR16G16B16Sint;
			case GslDataFormat::I32: return vk::Format::eR32G32B32Sint;
			case GslDataFormat::U8: return vk::Format::eR8G8B8Uint;
			case GslDataFormat::U16: return vk::Format::eR16G16B16Uint;
			case GslDataFormat::U32: return vk::Format::eR32G32B32Uint;
			default: abort();
			}
			break;
		case 4:
			switch (format)
			{
			case GslDataFormat::F32: return vk::Format::eR32G32B32A32Sfloat;
			case GslDataFormat::I8: return vk::Format::eR8G8B8A8Sint;
			case GslDataFormat::I16: return vk::Format::eR16G16B16A16Sint;
			case GslDataFormat::I32: return vk::Format::eR32G32B32A32Sint;
			case GslDataFormat::U8: return vk::Format::eR8G8B8A8Uint;
			case GslDataFormat::U16: return vk::Format::eR16G16B16A16Uint;
			case GslDataFormat::U32: return vk::Format::eR32G32B32A32Uint;
			default: abort();
			}
			break;
		default: abort();
	}
}

//--------------------------------------------------------------------------------------------------
static vk::CompareOp toVkCompareOp(Pipeline::CompareOperation compareOperation)
{
	switch (compareOperation)
	{
	case Pipeline::CompareOperation::Never: return vk::CompareOp::eNever;
	case Pipeline::CompareOperation::Less: return vk::CompareOp::eLess;
	case Pipeline::CompareOperation::Equal: return vk::CompareOp::eEqual;
	case Pipeline::CompareOperation::LessOrEqual: return  vk::CompareOp::eLessOrEqual;
	case Pipeline::CompareOperation::Greater: return vk::CompareOp::eGreater;
	case Pipeline::CompareOperation::NotEqual: return vk::CompareOp::eNotEqual;
	case Pipeline::CompareOperation::GreaterOrEqual: return vk::CompareOp::eGreaterOrEqual;
	case Pipeline::CompareOperation::Always: return vk::CompareOp::eAlways;
	default: abort();
	}
}

//--------------------------------------------------------------------------------------------------
static vk::ShaderStageFlagBits toVkShaderStage(ShaderStage shaderStage)
{
	if (hasOneFlag(shaderStage, ShaderStage::Vertex))
		return vk::ShaderStageFlagBits::eVertex;
	if (hasOneFlag(shaderStage, ShaderStage::Fragment))
		return vk::ShaderStageFlagBits::eFragment;
	if (hasOneFlag(shaderStage, ShaderStage::Compute))
		return vk::ShaderStageFlagBits::eCompute;
	abort();
}
static vk::ShaderStageFlags toVkShaderStages(ShaderStage shaderStage)
{
	vk::ShaderStageFlags flags;
	if (hasAnyFlag(shaderStage, ShaderStage::Vertex))
		flags |= vk::ShaderStageFlagBits::eVertex;
	if (hasAnyFlag(shaderStage, ShaderStage::Fragment))
		flags |= vk::ShaderStageFlagBits::eFragment;
	if (hasAnyFlag(shaderStage, ShaderStage::Compute))
		flags |= vk::ShaderStageFlagBits::eCompute;
	return flags;
}
static vk::PipelineStageFlags toVkPipelineStages(ShaderStage shaderStage)
{
	vk::PipelineStageFlags flags;
	if (hasAnyFlag(shaderStage, ShaderStage::Vertex))
		flags |= vk::PipelineStageFlagBits::eVertexShader;
	if (hasAnyFlag(shaderStage, ShaderStage::Fragment))
		flags |= vk::PipelineStageFlagBits::eFragmentShader;
	if (hasAnyFlag(shaderStage, ShaderStage::Compute))
		flags |= vk::PipelineStageFlagBits::eComputeShader;
	return flags;
}

//--------------------------------------------------------------------------------------------------
static vk::PipelineBindPoint toVkPipelineBindPoint(PipelineType pipelineType)
{
	switch (pipelineType)
	{
	case PipelineType::Graphics: return vk::PipelineBindPoint::eGraphics;
	case PipelineType::Compute: return vk::PipelineBindPoint::eCompute;
	default: abort();
	}
}

//--------------------------------------------------------------------------------------------------
static vk::ImageAspectFlags toVkImageAspectFlags(Image::Format imageFormat)
{
	if (isFormatColor(imageFormat)) return vk::ImageAspectFlagBits::eColor;
	if (isFormatDepthOnly(imageFormat)) return vk::ImageAspectFlagBits::eDepth;
	if (isFormatStencilOnly(imageFormat)) return vk::ImageAspectFlagBits::eStencil;
	return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
}

//--------------------------------------------------------------------------------------------------
static vk::DescriptorType toVkDescriptorType(GslUniformType uniformType)
{
	switch (uniformType)
	{
	case GslUniformType::Sampler1D:
	case GslUniformType::Sampler2D:
	case GslUniformType::Sampler3D:
	case GslUniformType::SamplerCube:
	case GslUniformType::Sampler1DArray:
	case GslUniformType::Sampler2DArray:
	case GslUniformType::Isampler1D:
	case GslUniformType::Isampler2D:
	case GslUniformType::Isampler3D:
	case GslUniformType::IsamplerCube:
	case GslUniformType::Isampler1DArray:
	case GslUniformType::Isampler2DArray:
	case GslUniformType::Usampler1D:
	case GslUniformType::Usampler2D:
	case GslUniformType::Usampler3D:
	case GslUniformType::UsamplerCube:
	case GslUniformType::Usampler1DArray:
	case GslUniformType::Usampler2DArray:
	case GslUniformType::Sampler1DShadow:
	case GslUniformType::Sampler2DShadow:
	case GslUniformType::SamplerCubeShadow:
	case GslUniformType::Sampler1DArrayShadow:
	case GslUniformType::Sampler2DArrayShadow:
		return vk::DescriptorType::eCombinedImageSampler;
	case GslUniformType::Image1D:
	case GslUniformType::Image2D:
	case GslUniformType::Image3D:
	case GslUniformType::ImageCube:
	case GslUniformType::Image1DArray:
	case GslUniformType::Image2DArray:
	case GslUniformType::Iimage1D:
	case GslUniformType::Iimage2D:
	case GslUniformType::Iimage3D:
	case GslUniformType::IimageCube:
	case GslUniformType::Iimage1DArray:
	case GslUniformType::Iimage2DArray:
	case GslUniformType::Uimage1D:
	case GslUniformType::Uimage2D:
	case GslUniformType::Uimage3D:
	case GslUniformType::UimageCube:
	case GslUniformType::Uimage1DArray:
	case GslUniformType::Uimage2DArray:
		return vk::DescriptorType::eStorageImage;
	case GslUniformType::SubpassInput:
		return vk::DescriptorType::eInputAttachment;
	case GslUniformType::UniformBuffer:
		return vk::DescriptorType::eUniformBuffer;
	case GslUniformType::StorageBuffer:
		return vk::DescriptorType::eStorageBuffer;
	default: abort();
	}
}

//--------------------------------------------------------------------------------------------------
static vk::IndexType toVkIndexType(GraphicsPipeline::Index indexType)
{
	switch (indexType)
	{
		case GraphicsPipeline::Index::Uint16: return vk::IndexType::eUint16;
		case GraphicsPipeline::Index::Uint32: return vk::IndexType::eUint32;
		default: abort();
	}
}

} // namespace garden::graphics