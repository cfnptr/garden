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

#ifndef VK_DS_POOL_COMBINED_SAMPLER_COUNT
#define VK_DS_POOL_COMBINED_SAMPLER_COUNT 128
#endif
#ifndef VK_DS_POOL_STORAGE_IMAGE_COUNT
#define VK_DS_POOL_STORAGE_IMAGE_COUNT 128
#endif
#ifndef VK_DS_POOL_UNIFORM_BUFFER_COUNT
#define VK_DS_POOL_UNIFORM_BUFFER_COUNT 128
#endif
#ifndef VK_DS_POOL_STORAGE_BUFFER_COUNT
#define VK_DS_POOL_STORAGE_BUFFER_COUNT 128
#endif
#ifndef VK_DS_POOL_INPUT_ATTACHMENT_COUNT
#define VK_DS_POOL_INPUT_ATTACHMENT_COUNT 128
#endif

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
	inline static uint32 versionMajor = 0;
	inline static uint32 versionMinor = 0;
	inline static vk::Instance instance = {};
	inline static vk::DispatchLoaderDynamic dynamicLoader = {};
	inline static vk::PhysicalDevice physicalDevice = {};
	inline static vk::SurfaceKHR surface = {};
	inline static uint32 graphicsQueueFamilyIndex = 0;
	inline static uint32 transferQueueFamilyIndex = 0;
	inline static uint32 computeQueueFamilyIndex = 0;
	inline static vk::Device device = {};
	inline static VmaAllocator memoryAllocator = nullptr;
	inline static vk::Queue frameQueue = {};
	inline static vk::Queue graphicsQueue = {};
	inline static vk::Queue transferQueue = {};
	inline static vk::Queue computeQueue = {};
	inline static vk::CommandPool frameCommandPool = {};
	inline static vk::CommandPool graphicsCommandPool = {};
	inline static vk::CommandPool transferCommandPool = {};
	inline static vk::CommandPool computeCommandPool = {};
	inline static vk::DescriptorPool descriptorPool = {};
	inline static vk::PipelineCache pipelineCache = {};
	inline static vector<vk::CommandBuffer> secondaryCommandBuffers = {};
	inline static vector<bool> secondaryCommandStates = {};
	inline static Swapchain swapchain = {};
	inline static vk::PhysicalDeviceProperties2 deviceProperties = {};
	inline static vk::PhysicalDeviceFeatures2 deviceFeatures = {};
	inline static bool isCacheLoaded = false;
	inline static bool hasMemoryBudget = false;
	inline static bool hasMemoryPriority = false;
	inline static bool hasPageableMemory = false;
	inline static bool hasDynamicRendering = false;
	inline static bool hasDescriptorIndexing = false;

	#if GARDEN_DEBUG
	inline static vk::DebugUtilsMessengerEXT debugMessenger = {};
	inline static bool hasDebugUtils = false;
	#endif

	// TODO: move rest to api.hpp and make these calls universal.

	static void initialize(const string& appName, const string& appDataName, Version appVersion,
		uint2 windowSize, bool isFullscreen, bool useVsync, bool useTripleBuffering, bool useThreading);
	static void terminate();
	static void updateDestroyBuffer();
};

/***********************************************************************************************************************
 * @brief Returns Vulkan format type from the image data format.
 * @param formatType target image data format type
 */
static vk::Format toVkFormat(Image::Format formatType) noexcept
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
	case Image::Format::UnormA2B10G10R10: return vk::Format::eA2B10G10R10UnormPack32;
	case Image::Format::UfloatB10G11R11: return vk::Format::eB10G11R11UfloatPack32;
	case Image::Format::UnormD16: return vk::Format::eD16Unorm;
	case Image::Format::SfloatD32: return vk::Format::eD32Sfloat;
	case Image::Format::UnormD24UintS8: return vk::Format::eD24UnormS8Uint;
	case Image::Format::SfloatD32Uint8S: return vk::Format::eD32SfloatS8Uint;
	default: abort();
	}
}
/**
 * @brief Returns image data format type from the Vulkan format.
 * @param formatType target Vulkan format type
 */
static Image::Format toImageFormat(vk::Format formatType) noexcept
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
	case vk::Format::eA2B10G10R10UnormPack32: return Image::Format::UnormA2B10G10R10;
	case vk::Format::eB10G11R11UfloatPack32: return Image::Format::UfloatB10G11R11;
	case vk::Format::eD16Unorm: return Image::Format::UnormD16;
	case vk::Format::eD32Sfloat: return Image::Format::SfloatD32;
	case vk::Format::eD24UnormS8Uint: return Image::Format::UnormD24UintS8;
	case vk::Format::eD32SfloatS8Uint: return Image::Format::SfloatD32Uint8S;
	default: abort();
	}
}

/***********************************************************************************************************************
 * @brief Returns Vulkan format type from the GSL data type and format.
 * 
 * @param type GSL data type
 * @param format GSL data format
 */
static vk::Format toVkFormat(GslDataType type, GslDataFormat format) noexcept
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

/***********************************************************************************************************************
 * @brief Returns Vulkan comparison operator from the pipeline compare operation.
 * @param compareOperation target pipeline compare operation type
 */
static vk::CompareOp toVkCompareOp(Pipeline::CompareOperation compareOperation) noexcept
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

/**
 * @brief Returns Vulkan shader stage flag bits from the shader stage.
 * @param shaderStage target shader stage
 */
static vk::ShaderStageFlagBits toVkShaderStage(ShaderStage shaderStage) noexcept
{
	if (hasOneFlag(shaderStage, ShaderStage::Vertex))
		return vk::ShaderStageFlagBits::eVertex;
	if (hasOneFlag(shaderStage, ShaderStage::Fragment))
		return vk::ShaderStageFlagBits::eFragment;
	if (hasOneFlag(shaderStage, ShaderStage::Compute))
		return vk::ShaderStageFlagBits::eCompute;
	abort();
}
/**
 * @brief Returns Vulkan shader stage flags from the shader stage.
 * @param shaderStage target shader stage
 */
static vk::ShaderStageFlags toVkShaderStages(ShaderStage shaderStage) noexcept
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
/**
 * @brief Returns Vulkan pipeline stage flags from the shader stage.
 * @param shaderStage target shader stage
 */
static vk::PipelineStageFlags toVkPipelineStages(ShaderStage shaderStage) noexcept
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

/***********************************************************************************************************************
 * @brief Returns Vulkan pipeline bind point from the rendering pipeline type.
 * @param pipelineType target rendering pipeline type
 */
static vk::PipelineBindPoint toVkPipelineBindPoint(PipelineType pipelineType) noexcept
{
	switch (pipelineType)
	{
	case PipelineType::Graphics: return vk::PipelineBindPoint::eGraphics;
	case PipelineType::Compute: return vk::PipelineBindPoint::eCompute;
	default: abort();
	}
}

/**
 * @brief Returns Vulkan image aspect flags from the image data format.
 * @param imageFormat target image data format
 */
static vk::ImageAspectFlags toVkImageAspectFlags(Image::Format imageFormat) noexcept
{
	if (isFormatColor(imageFormat)) return vk::ImageAspectFlagBits::eColor;
	if (isFormatDepthOnly(imageFormat)) return vk::ImageAspectFlagBits::eDepth;
	if (isFormatStencilOnly(imageFormat)) return vk::ImageAspectFlagBits::eStencil;
	return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
}

/***********************************************************************************************************************
 * @brief Returns Vulkan descriptor type from the GSL uniform type.
 * @param uniformType target GSL uniform type
 */
static vk::DescriptorType toVkDescriptorType(GslUniformType uniformType) noexcept
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

/***********************************************************************************************************************
 * @brief Returns Vulkan index type from the graphics pipeline index type.
 * @param indexType target graphics pipeline index type
 */
static vk::IndexType toVkIndexType(GraphicsPipeline::Index indexType) noexcept
{
	switch (indexType)
	{
		case GraphicsPipeline::Index::Uint16: return vk::IndexType::eUint16;
		case GraphicsPipeline::Index::Uint32: return vk::IndexType::eUint32;
		default: abort();
	}
}

} // namespace garden::graphics