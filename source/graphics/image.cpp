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

#include "garden/graphics/image.hpp"
#include "garden/graphics/vulkan/api.hpp"
#include "garden/file.hpp"

#include "png.h"
#include "webp/decode.h"
#include "webp/encode.h"
#include "ImfRgbaFile.h"
#include "ImfInputFile.h"

#if GARDEN_USE_BASIS_UNIVERSAL
	#if GARDEN_EDITOR
	#include "basisu_comp.h"
	#endif
#include "basisu_transcoder.h"
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <fstream>

using namespace math;
using namespace garden;
using namespace garden::graphics;

//**********************************************************************************************************************
static vk::ImageType toVkImageType(Image::Type imageType) noexcept
{
	switch (imageType)
	{
		case Image::Type::Texture1DArray:
		case Image::Type::Texture1D:
			return vk::ImageType::e1D;
		case Image::Type::Texture2DArray:
		case Image::Type::Texture2D:
		case Image::Type::Cubemap:
			return vk::ImageType::e2D;
		case Image::Type::Texture3D:
			return vk::ImageType::e3D;
		default: abort();
	}
}
static vk::ImageViewType toVkImageViewType(Image::Type imageType) noexcept
{
	switch (imageType)
	{
		case Image::Type::Texture1D: return vk::ImageViewType::e1D;
		case Image::Type::Texture2D: return vk::ImageViewType::e2D;
		case Image::Type::Texture3D: return vk::ImageViewType::e3D;
		case Image::Type::Texture1DArray: return vk::ImageViewType::e1DArray;
		case Image::Type::Texture2DArray: return vk::ImageViewType::e2DArray;
		case Image::Type::Cubemap: return vk::ImageViewType::eCube;
		default: abort();
	}
}
static constexpr vk::ImageUsageFlags toVkImageUsages(Image::Usage imageUsage) noexcept
{
	vk::ImageUsageFlags flags;
	if (hasAnyFlag(imageUsage, Image::Usage::TransferSrc))
		flags |= vk::ImageUsageFlagBits::eTransferSrc;
	if (hasAnyFlag(imageUsage, Image::Usage::TransferDst))
		flags |= vk::ImageUsageFlagBits::eTransferDst;
	if (hasAnyFlag(imageUsage, Image::Usage::Sampled))
		flags |= vk::ImageUsageFlagBits::eSampled;
	if (hasAnyFlag(imageUsage, Image::Usage::Storage))
		flags |= vk::ImageUsageFlagBits::eStorage;
	if (hasAnyFlag(imageUsage, Image::Usage::ColorAttachment))
		flags |= vk::ImageUsageFlagBits::eColorAttachment;
	if (hasAnyFlag(imageUsage, Image::Usage::DepthStencilAttachment))
		flags |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
	return flags;
}
static VmaAllocationCreateFlagBits toVmaMemoryStrategy(Image::Strategy memoryUsage) noexcept
{
	switch (memoryUsage)
	{
		case Image::Strategy::Default: return {};
		case Image::Strategy::Size: return VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT;
		case Image::Strategy::Speed: return VMA_ALLOCATION_CREATE_STRATEGY_MIN_TIME_BIT;
		default: abort();
	}
}

//**********************************************************************************************************************
static void createVkImage(Image::Type type, Image::Format format, Image::Usage usage, 
	Image::Strategy strategy, u32x4 size, void*& instance, void*& allocation, uint32& aspectFlags)
{
	VkImageCreateInfo imageInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imageInfo.imageType = (VkImageType)toVkImageType(type);
	imageInfo.format = (VkFormat)toVkFormat(format);
	imageInfo.mipLevels = size.getW();
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = (VkImageUsageFlags)toVkImageUsages(usage);
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.queueFamilyIndexCount = 0;
	imageInfo.pQueueFamilyIndices = nullptr;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	auto vulkanAPI = VulkanAPI::get();

	uint32 queueFamilyIndices[3];
	if (hasAnyFlag(usage, Image::Usage::TransferQ | Image::Usage::ComputeQ))
	{
		imageInfo.queueFamilyIndexCount = 1;

		if (hasAnyFlag(usage, Image::Usage::TransferQ) && 
			vulkanAPI->graphicsQueueFamilyIndex != vulkanAPI->transferQueueFamilyIndex)
		{
			queueFamilyIndices[imageInfo.queueFamilyIndexCount++] = vulkanAPI->transferQueueFamilyIndex;
		}
		if (hasAnyFlag(usage, Image::Usage::ComputeQ) &&
			vulkanAPI->graphicsQueueFamilyIndex != vulkanAPI->computeQueueFamilyIndex &&
			vulkanAPI->transferQueueFamilyIndex != vulkanAPI->computeQueueFamilyIndex)
		{
			queueFamilyIndices[imageInfo.queueFamilyIndexCount++] = vulkanAPI->computeQueueFamilyIndex;
		}

		if (imageInfo.queueFamilyIndexCount > 1)
		{
			queueFamilyIndices[0] = vulkanAPI->graphicsQueueFamilyIndex;
			imageInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
			imageInfo.pQueueFamilyIndices = queueFamilyIndices;
		}
		else imageInfo.queueFamilyIndexCount = 0;
	}

	if (type == Image::Type::Texture3D)
	{
		imageInfo.extent = { size.getX(), size.getY(), size.getZ() };
		imageInfo.arrayLayers = 1;
	}
	else
	{
		imageInfo.extent = { size.getX(), size.getY(), 1 };
		imageInfo.arrayLayers = size.getZ();
	}

	if (type == Image::Type::Cubemap)
		imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

	#if GARDEN_DEBUG
	vk::PhysicalDeviceImageFormatInfo2 imageFormatInfo;
	imageFormatInfo.format = vk::Format(imageInfo.format);
	imageFormatInfo.type = vk::ImageType(imageInfo.imageType);
	imageFormatInfo.tiling = vk::ImageTiling(imageInfo.tiling);
	imageFormatInfo.usage = vk::ImageUsageFlags(imageInfo.usage);
	imageFormatInfo.flags = vk::ImageCreateFlags(imageInfo.flags);
	auto imageFormatProperties = vulkanAPI->physicalDevice.getImageFormatProperties2(imageFormatInfo);
	GARDEN_ASSERT(imageInfo.extent.width <= imageFormatProperties.imageFormatProperties.maxExtent.width);
	GARDEN_ASSERT(imageInfo.extent.height <= imageFormatProperties.imageFormatProperties.maxExtent.height);
	GARDEN_ASSERT(imageInfo.extent.depth <= imageFormatProperties.imageFormatProperties.maxExtent.depth);
	GARDEN_ASSERT(imageInfo.arrayLayers <= imageFormatProperties.imageFormatProperties.maxArrayLayers);
	GARDEN_ASSERT(imageInfo.mipLevels <= imageFormatProperties.imageFormatProperties.maxMipLevels);
	GARDEN_ASSERT(imageInfo.samples <= (VkSampleCountFlags)imageFormatProperties.imageFormatProperties.sampleCounts);
	#endif
 
	VmaAllocationCreateInfo allocationCreateInfo = {};
	allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
	allocationCreateInfo.flags = toVmaMemoryStrategy(strategy);

	if (hasAnyFlag(usage, Image::Usage::Fullscreen))
	{
		allocationCreateInfo.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
		allocationCreateInfo.priority = 1.0f;
	}
	else
	{
		allocationCreateInfo.priority = 0.5f;
	}

	VkImage vmaInstance; VmaAllocation vmaAllocation;
	auto result = vmaCreateImage(vulkanAPI->memoryAllocator, &imageInfo,
		&allocationCreateInfo, &vmaInstance, &vmaAllocation, nullptr);
	if (result != VK_SUCCESS)
		throw GardenError("Failed to allocate image.");

	// TODO: https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/resource_aliasing.html

	instance = vmaInstance; allocation = vmaAllocation;
	aspectFlags = (uint32)toVkImageAspectFlags(format);
}

//**********************************************************************************************************************
Image::Image(Type type, Format format, Usage usage, Strategy strategy, u32x4 size, uint64 version) :
	Memory(0, CpuAccess::None, Location::Auto, strategy, version), barrierStates(size.getZ() * size.getW())
{
	GARDEN_ASSERT(areAllTrue(size > u32x4::zero));

	auto graphicsBackend = GraphicsAPI::get()->getBackendType();
	if (graphicsBackend == GraphicsBackend::VulkanAPI)
		createVkImage(type, format, usage, strategy, size, instance, allocation, aspectFlags);
	else abort();

	this->binarySize = 0;
	this->type = type;
	this->format = format;
	this->usage = usage;
	this->swapchain = false;
	this->size = size;

	auto mipSize = size;
	for (uint8 mip = 0, mipCount = getMipCount(); mip < mipCount; mip++)
	{
		auto mipBinarySize = toBinarySize((psize)mipSize.getX() * 
			mipSize.getY() * mipSize.getZ(), format);
		GARDEN_ASSERT(mipBinarySize > 0);
	
		this->binarySize += mipBinarySize;
		mipSize = max(mipSize / 2u, u32x4::one);	
	}
}

//**********************************************************************************************************************
Image::Image(void* instance, Format format, Usage usage, Strategy strategy, uint2 size, uint8 backend) : Memory(
	toBinarySize((psize)size.x * size.y, format), CpuAccess::None, Location::Auto, strategy, 0), barrierStates(1)
{
	GARDEN_ASSERT(areAllTrue(size > uint2::zero));
	GARDEN_ASSERT(binarySize > 0);

	this->instance = instance;
	this->type = Image::Type::Texture2D;
	this->format = format;
	this->usage = usage;
	this->swapchain = true;
	this->size = u32x4(size.x, size.y, 1, 1);

	if ((GraphicsBackend)backend == GraphicsBackend::VulkanAPI)
	{
		barrierStates[0].stage = (uint64)vk::PipelineStageFlagBits2::eColorAttachmentOutput;
		aspectFlags = (uint32)toVkImageAspectFlags(format);
	}
	else abort();
}
bool Image::destroy()
{
	if (!instance || busyLock > 0)
		return false;

	auto graphicsAPI = GraphicsAPI::get();

	#if GARDEN_DEBUG
	if (!graphicsAPI->forceResourceDestroy)
	{
		auto imageInstance = graphicsAPI->imagePool.getID(this);
		for (auto& imageView : graphicsAPI->imageViewPool)
		{
			if (!ResourceExt::getInstance(imageView))
				continue;
			GARDEN_ASSERT_MSG(imageInstance != imageView.getImage(), 
				"Image view [" + imageView.getDebugName() + "] is "
				"still using destroyed image [" + debugName + "]");
		}
	}
	#endif

	auto graphicsBackend = graphicsAPI->getBackendType();
	if (graphicsBackend == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		if (!this->swapchain)
		{
			if (vulkanAPI->forceResourceDestroy)
				vmaDestroyImage(vulkanAPI->memoryAllocator, (VkImage)instance, (VmaAllocation)allocation);
			else vulkanAPI->destroyResource(GraphicsAPI::DestroyResourceType::Image, instance, allocation);
		}
	}
	else abort();

	return true;
}

uint32 Image::calcApiLayerIndex(Type type, uint32 layerIndex) noexcept
{
	if (type == Image::Type::Cubemap)
	{
		GARDEN_ASSERT(layerIndex < Image::cubemapFaceCount);
		auto graphicsBackend = GraphicsAPI::get()->getBackendType();
		if (graphicsBackend == GraphicsBackend::VulkanAPI)
			return layerIndex ^ 1; // Note: remapping Vulkan API cubemap face indices.
		else abort();
	}
	return layerIndex;
}

//**********************************************************************************************************************
ID<ImageView> Image::getView()
{
	if (!imageView)
	{
		GARDEN_ASSERT_MSG(instance, "Image [" + debugName + "] is not ready yet");

		auto graphicsAPI = GraphicsAPI::get();
		auto image = graphicsAPI->imagePool.getID(this);
		imageView = graphicsAPI->imageViewPool.create(true, image, 
			type, format, 0, getLayerCount(), 0, getMipCount());

		#if GARDEN_DEBUG || GARDEN_EDITOR
		auto view = graphicsAPI->imageViewPool.get(imageView);
		view->setDebugName(debugName + ".view");
		#endif
	}
	return imageView;
}
void Image::freeView()
{
	GraphicsAPI::get()->imageViewPool.destroy(imageView);
}

ID<ImageView> Image::getView(uint32 layer, uint8 mip)
{
	GARDEN_ASSERT(layer < getLayerCount());
	GARDEN_ASSERT(mip < getMipCount());

	auto index = getLayerCount() * mip + layer;
	auto view = barrierStates[index].view;
	if (!view)
	{
		GARDEN_ASSERT_MSG(instance, "Image [" + debugName + "] is not ready yet");

		Image::Type viewType;
		if (type == Image::Type::Texture2DArray || type == Image::Type::Texture3D || type == Image::Type::Cubemap)
			viewType = Image::Type::Texture2D;
		else if (type == Image::Type::Texture1DArray)
			viewType = Image::Type::Texture1D;
		else viewType = type;

		auto graphicsAPI = GraphicsAPI::get();
		auto image = graphicsAPI->imagePool.getID(this);
		view = graphicsAPI->imageViewPool.create(true, image, viewType, format, layer, 1, mip, 1);
		barrierStates[index].view = view;

		#if GARDEN_DEBUG || GARDEN_EDITOR
		auto viewView = graphicsAPI->imageViewPool.get(view);
		viewView->setDebugName(debugName + ".view_l" + to_string(layer) + "_m" + to_string(mip));
		#endif
	}
	return view;
}
void Image::freeView(uint32 layer, uint8 mip)
{
	GARDEN_ASSERT(layer < getLayerCount());
	GARDEN_ASSERT(mip < getMipCount());
	auto index = getLayerCount() * mip + layer;
	GraphicsAPI::get()->imageViewPool.destroy(barrierStates[index].view);
}

void Image::freeSpecificViews()
{
	auto graphicsAPI = GraphicsAPI::get();
	for (auto& barrierState : barrierStates)
		graphicsAPI->imageViewPool.destroy(barrierState.view);
}

//**********************************************************************************************************************
bool Image::isSupported(Type type, Format format, Usage usage, uint3 size, uint8 mipCount, uint32 layerCount) noexcept
{
	auto graphicsBackend = GraphicsAPI::get()->getBackendType();
	if (graphicsBackend == GraphicsBackend::VulkanAPI)
	{
		vk::PhysicalDeviceImageFormatInfo2 imageFormatInfo;
		imageFormatInfo.format = toVkFormat(format);
		imageFormatInfo.type = toVkImageType(type);
		imageFormatInfo.tiling = vk::ImageTiling::eOptimal;
		imageFormatInfo.usage = toVkImageUsages(usage);
		if (type == Type::Cubemap)
			imageFormatInfo.flags |= vk::ImageCreateFlagBits::eCubeCompatible;

		vk::ImageFormatProperties2 imageFormatProperties;
		if (VulkanAPI::get()->physicalDevice.getImageFormatProperties2(
			&imageFormatInfo, &imageFormatProperties) != vk::Result::eSuccess)
		{
			return false;
		}

		return size.x <= imageFormatProperties.imageFormatProperties.maxExtent.width &&
			size.y <= imageFormatProperties.imageFormatProperties.maxExtent.height &&
			size.z <= imageFormatProperties.imageFormatProperties.maxExtent.depth &&
			mipCount <= imageFormatProperties.imageFormatProperties.maxMipLevels &&
			layerCount <= imageFormatProperties.imageFormatProperties.maxArrayLayers;
	}
	else abort();
}

void Image::generateMips(Sampler::Filter filter)
{
	auto graphicsAPI = GraphicsAPI::get();
	GARDEN_ASSERT_MSG(!graphicsAPI->renderPassFramebuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(instance, "Image [" + debugName + "] is not ready");

	#if GARDEN_DEBUG
	auto commandBufferType = graphicsAPI->currentCommandBuffer->getType();
	if (commandBufferType == CommandBufferType::TransferOnly)
	{
		GARDEN_ASSERT_MSG(hasAnyFlag(usage, Usage::TransferQ), 
			"Image [" + debugName + "] does not have transfer queue flag");
	}
	if (commandBufferType == CommandBufferType::Compute)
	{
		GARDEN_ASSERT_MSG(hasAnyFlag(usage, Usage::ComputeQ), 
			"Image [" + debugName + "] does not have compute queue flag");
	}
	#endif

	SET_GPU_DEBUG_LABEL("Generate Mips");

	auto image = graphicsAPI->imagePool.getID(this);
	auto layerCount = getLayerCount();
	auto mipSize = size;

	for (uint8 mip = 1, mipCount = getMipCount(); mip < mipCount; mip++)
	{
		Image::BlitRegion region;
		region.srcExtent = (uint3)mipSize;
		mipSize = max(mipSize / 2u, u32x4::one);
		region.dstExtent = (uint3)mipSize;
		region.layerCount = 1;
		region.srcMipLevel = mip - 1;
		region.dstMipLevel = mip;

		if (type != Image::Type::Texture3D)
			region.srcExtent.z = region.dstExtent.z = 1;

		// Note: We should not blit all layers in one mip,
		//       because result differs across GPUs.
		
		for (uint8 layer = 0; layer < layerCount; layer++)
		{
			region.srcBaseLayer = region.dstBaseLayer = layer;
			Image::blit(image, image, region, filter);
		}
	}
}

//**********************************************************************************************************************
void Image::clear(float4 color, const ClearRegion* regions, uint32 count)
{
	auto graphicsAPI = GraphicsAPI::get();
	auto currentCommandBuffer = graphicsAPI->currentCommandBuffer;
	GARDEN_ASSERT_MSG(regions, "Assert " + debugName);
	GARDEN_ASSERT_MSG(count > 0, "Assert " + debugName);
	GARDEN_ASSERT_MSG(currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(!graphicsAPI->renderPassFramebuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(isFormatFloat(format) || isFormatSrgb(format) || isFormatNorm(format), "Assert " + debugName);
	GARDEN_ASSERT_MSG(hasAnyFlag(usage, Usage::TransferDst), "Assert " + debugName);

	auto commandBufferType = currentCommandBuffer->getType();
	#if GARDEN_DEBUG
	if (commandBufferType == CommandBufferType::Compute)
	{
		GARDEN_ASSERT_MSG(hasAnyFlag(usage, Usage::ComputeQ), 
			"Image [" + debugName + "] does not have compute queue flag");
	}
	#endif

	ClearImageCommand command;
	command.clearType = 1;
	command.regionCount = count;
	command.image = graphicsAPI->imagePool.getID(this);
	command.color = color;
	command.regions = regions;
	currentCommandBuffer->addCommand(command);

	if (commandBufferType != CommandBufferType::Frame)
	{
		busyLock++;
		currentCommandBuffer->addLockedResource(command.image);
	}
}
void Image::clear(int4 color, const ClearRegion* regions, uint32 count)
{
	auto graphicsAPI = GraphicsAPI::get();
	auto currentCommandBuffer = graphicsAPI->currentCommandBuffer;
	GARDEN_ASSERT_MSG(regions, "Assert " + debugName);
	GARDEN_ASSERT_MSG(count > 0, "Assert " + debugName);
	GARDEN_ASSERT_MSG(currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(!graphicsAPI->renderPassFramebuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(isFormatSint(format), "Assert " + debugName);
	GARDEN_ASSERT_MSG(hasAnyFlag(usage, Usage::TransferDst), "Assert " + debugName);

	auto commandBufferType = currentCommandBuffer->getType();
	#if GARDEN_DEBUG
	if (commandBufferType == CommandBufferType::Compute)
	{
		GARDEN_ASSERT_MSG(hasAnyFlag(usage, Usage::ComputeQ), 
			"Image [" + debugName + "] does not have compute queue flag");
	}
	#endif

	ClearImageCommand command;
	command.clearType = 2;
	command.regionCount = count;
	command.image = graphicsAPI->imagePool.getID(this);
	*(int4*)&command.color = color;
	command.regions = regions;
	currentCommandBuffer->addCommand(command);

	if (commandBufferType != CommandBufferType::Frame)
	{
		busyLock++;
		currentCommandBuffer->addLockedResource(command.image);
	}
}
void Image::clear(uint4 color, const ClearRegion* regions, uint32 count)
{
	auto graphicsAPI = GraphicsAPI::get();
	auto currentCommandBuffer = graphicsAPI->currentCommandBuffer;
	GARDEN_ASSERT_MSG(regions, "Assert " + debugName);
	GARDEN_ASSERT_MSG(count > 0, "Assert " + debugName);
	GARDEN_ASSERT_MSG(currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(!graphicsAPI->renderPassFramebuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(isFormatUint(format), "Assert " + debugName);
	GARDEN_ASSERT_MSG(hasAnyFlag(usage, Usage::TransferDst), "Assert " + debugName);

	auto commandBufferType = currentCommandBuffer->getType();
	#if GARDEN_DEBUG
	if (commandBufferType == CommandBufferType::Compute)
	{
		GARDEN_ASSERT_MSG(hasAnyFlag(usage, Usage::ComputeQ), 
			"Image [" + debugName + "] does not have compute queue flag");
	}
	#endif

	ClearImageCommand command;
	command.clearType = 3;
	command.regionCount = count;
	command.image = graphicsAPI->imagePool.getID(this);
	*(uint4*)&command.color = color;
	command.regions = regions;
	currentCommandBuffer->addCommand(command);

	if (commandBufferType != CommandBufferType::Frame)
	{
		busyLock++;
		currentCommandBuffer->addLockedResource(command.image);
	}
}
void Image::clear(float depth, uint32 stencil, const ClearRegion* regions, uint32 count)
{
	auto graphicsAPI = GraphicsAPI::get();
	auto currentCommandBuffer = graphicsAPI->currentCommandBuffer;
	GARDEN_ASSERT_MSG(regions, "Assert " + debugName);
	GARDEN_ASSERT_MSG(count > 0, "Assert " + debugName);
	GARDEN_ASSERT_MSG(currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(!graphicsAPI->renderPassFramebuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(isFormatDepthOrStencil(format), "Assert " + debugName);
	GARDEN_ASSERT_MSG(hasAnyFlag(usage, Usage::TransferDst), "Assert " + debugName);

	auto commandBufferType = currentCommandBuffer->getType();
	#if GARDEN_DEBUG
	if (commandBufferType == CommandBufferType::Compute)
	{
		GARDEN_ASSERT_MSG(hasAnyFlag(usage, Usage::ComputeQ), 
			"Image [" + debugName + "] does not have compute queue flag");
	}
	#endif

	ClearImageCommand command;
	command.regionCount = count;
	command.image = graphicsAPI->imagePool.getID(this);
	command.color.x = depth;
	*(uint32*)&command.color.y = stencil;
	command.regions = regions;
	currentCommandBuffer->addCommand(command);

	if (commandBufferType != CommandBufferType::Frame)
	{
		busyLock++;
		currentCommandBuffer->addLockedResource(command.image);
	}
}

//**********************************************************************************************************************
void Image::copy(ID<Image> source, ID<Image> destination, const CopyImageRegion* regions, uint32 count)
{
	auto graphicsAPI = GraphicsAPI::get();
	auto currentCommandBuffer = graphicsAPI->currentCommandBuffer;
	GARDEN_ASSERT(source);
	GARDEN_ASSERT(destination);
	GARDEN_ASSERT(regions);
	GARDEN_ASSERT(count > 0);
	GARDEN_ASSERT(currentCommandBuffer);
	GARDEN_ASSERT(!graphicsAPI->renderPassFramebuffer);

	auto srcView = graphicsAPI->imagePool.get(source);
	GARDEN_ASSERT_MSG(hasAnyFlag(srcView->usage, Usage::TransferSrc), 
		"Missing source image [" + srcView->getDebugName() + "] flag");
	GARDEN_ASSERT_MSG(srcView->instance, "Source image [" + srcView->getDebugName() + "] is not ready");

	auto dstView = graphicsAPI->imagePool.get(destination);
	GARDEN_ASSERT_MSG(hasAnyFlag(dstView->usage, Usage::TransferDst),
		"Missing destination image [" + dstView->getDebugName() + "] flag");
	GARDEN_ASSERT_MSG(toBinarySize(256, srcView->format) == toBinarySize(256, dstView->format), "Different source [" +
		srcView->getDebugName() + "] and destination [" + dstView->getDebugName() + "] image format binary sizes");
	GARDEN_ASSERT_MSG(dstView->instance, "Destination image [" + dstView->getDebugName() + "] is not ready");

	auto commandBufferType = currentCommandBuffer->getType();
	#if GARDEN_DEBUG
	if (commandBufferType == CommandBufferType::TransferOnly)
	{
		GARDEN_ASSERT_MSG(hasAnyFlag(srcView->getUsage(), Usage::TransferQ),
			"Source image [" + srcView->getDebugName() + "] does not have transfer queue flag");
		GARDEN_ASSERT_MSG(hasAnyFlag(dstView->getUsage(), Usage::TransferQ),
			"Destination image [" + dstView->getDebugName() + "] does not have transfer queue flag");
	}
	if (commandBufferType == CommandBufferType::Compute)
	{
		GARDEN_ASSERT_MSG(hasAnyFlag(srcView->getUsage(), Usage::ComputeQ),
			"Source image [" + srcView->getDebugName() + "] does not have compute queue flag");
		GARDEN_ASSERT_MSG(hasAnyFlag(dstView->getUsage(), Usage::ComputeQ),
			"Destination image [" + dstView->getDebugName() + "] does not have compute queue flag");
	}

	for (uint32 i = 0; i < count; i++)
	{
		auto region = regions[i];
		GARDEN_ASSERT(region.srcBaseLayer + region.layerCount <= srcView->getLayerCount());
		GARDEN_ASSERT(region.dstBaseLayer + region.layerCount <= dstView->getLayerCount());
		GARDEN_ASSERT(region.srcMipLevel < srcView->getMipCount());
		GARDEN_ASSERT(region.dstMipLevel < dstView->getMipCount());

		if (region.extent == uint3::zero)
		{
			GARDEN_ASSERT(region.srcOffset == uint3::zero);
			GARDEN_ASSERT(region.dstOffset == uint3::zero);
		}
		else
		{
			auto mipImageSize = (uint3)calcSizeAtMip3(srcView->size, region.srcMipLevel);
			GARDEN_ASSERT(areAllTrue(region.srcOffset < mipImageSize));
			GARDEN_ASSERT(areAllTrue(region.extent <= mipImageSize));
			GARDEN_ASSERT(areAllTrue(region.extent + region.srcOffset <= mipImageSize));
			mipImageSize = (uint3)calcSizeAtMip3(dstView->size, region.dstMipLevel);
			GARDEN_ASSERT(areAllTrue(region.dstOffset < mipImageSize));
			GARDEN_ASSERT(areAllTrue(region.extent <= mipImageSize));
			GARDEN_ASSERT(areAllTrue(region.extent + region.dstOffset <= mipImageSize));
		}
	}
	#endif

	CopyImageCommand command;
	command.regionCount = count;
	command.source = source;
	command.destination = destination;
	command.regions = regions;
	currentCommandBuffer->addCommand(command);

	if (commandBufferType != CommandBufferType::Frame)
	{
		srcView->busyLock++;
		dstView->busyLock++;
		currentCommandBuffer->addLockedResource(source);
		currentCommandBuffer->addLockedResource(destination);
	}
}

//**********************************************************************************************************************
void Image::copy(ID<Buffer> source, ID<Image> destination, const CopyBufferRegion* regions, uint32 count)
{
	auto graphicsAPI = GraphicsAPI::get();
	auto currentCommandBuffer = graphicsAPI->currentCommandBuffer;
	GARDEN_ASSERT(source);
	GARDEN_ASSERT(destination);
	GARDEN_ASSERT(regions);
	GARDEN_ASSERT(count > 0);
	GARDEN_ASSERT(currentCommandBuffer);
	GARDEN_ASSERT(!graphicsAPI->renderPassFramebuffer);

	auto bufferView = graphicsAPI->bufferPool.get(source);
	GARDEN_ASSERT_MSG(hasAnyFlag(bufferView->getUsage(), Buffer::Usage::TransferSrc),
		"Missing source buffer [" + bufferView->getDebugName() + "] flag");
	GARDEN_ASSERT_MSG(ResourceExt::getInstance(**bufferView), "Buffer [" + 
		bufferView->getDebugName() + "] is not ready");
	
	auto imageView = graphicsAPI->imagePool.get(destination);
	GARDEN_ASSERT_MSG(hasAnyFlag(imageView->getUsage(), Usage::TransferDst),
		"Missing destination image [" + imageView->getDebugName() + "] flag");
	GARDEN_ASSERT_MSG(imageView->instance, "Image [" + imageView->getDebugName() + "] is not ready");
	
	auto commandBufferType = currentCommandBuffer->getType();
	#if GARDEN_DEBUG
	if (commandBufferType == CommandBufferType::TransferOnly)
	{
		GARDEN_ASSERT_MSG(hasAnyFlag(bufferView->getUsage(), Buffer::Usage::TransferQ),
			"Source buffer [" + bufferView->getDebugName() + "] does not have transfer queue flag");
		GARDEN_ASSERT_MSG(hasAnyFlag(imageView->getUsage(), Usage::TransferQ),
			"Destination image [" + imageView->getDebugName() + "] does not have transfer queue flag");
	}
	if (commandBufferType == CommandBufferType::Compute)
	{
		GARDEN_ASSERT_MSG(hasAnyFlag(bufferView->getUsage(), Buffer::Usage::ComputeQ),
			"Source buffer [" + bufferView->getDebugName() + "] does not have compute queue flag");
		GARDEN_ASSERT_MSG(hasAnyFlag(imageView->getUsage(), Usage::ComputeQ),
			"Destination image [" + imageView->getDebugName() + "] does not have compute queue flag");
	}

	for (uint32 i = 0; i < count; i++)
	{
		auto region = regions[i];
		GARDEN_ASSERT(region.imageBaseLayer + region.imageLayerCount <= imageView->getLayerCount());
		GARDEN_ASSERT(region.imageMipLevel < imageView->getMipCount());

		if (region.imageExtent == uint3::zero)
		{
			GARDEN_ASSERT(region.imageOffset == uint3::zero);
		}
		else
		{
			auto mipImageSize = (uint3)calcSizeAtMip3(imageView->size, region.imageMipLevel);
			GARDEN_ASSERT(areAllTrue(region.imageOffset < mipImageSize));
			GARDEN_ASSERT(areAllTrue(region.imageExtent <= mipImageSize));
			GARDEN_ASSERT(areAllTrue(region.imageExtent + region.imageOffset <= mipImageSize));
		}
		if (region.bufferRowLength == 0 && region.bufferImageHeight == 0)
		{
			auto regionBinarySize = toBinarySize((psize)region.imageExtent.x * 
				region.imageExtent.y * region.imageExtent.z, imageView->format);
			GARDEN_ASSERT(region.bufferOffset < bufferView->getBinarySize());
			GARDEN_ASSERT(regionBinarySize <= bufferView->getBinarySize());
			GARDEN_ASSERT(regionBinarySize + region.bufferOffset <= bufferView->getBinarySize());
		}
		else
		{
			// TODO: check buffer out of bounds
		}
	}
	#endif

	CopyBufferImageCommand command;
	command.toBuffer = false;
	command.regionCount = count;
	command.buffer = source;
	command.image = destination;
	command.regions = regions;
	currentCommandBuffer->addCommand(command);

	if (commandBufferType != CommandBufferType::Frame)
	{
		ResourceExt::getBusyLock(**bufferView)++;
		ResourceExt::getBusyLock(**imageView)++;
		currentCommandBuffer->addLockedResource(source);
		currentCommandBuffer->addLockedResource(destination);
	}
}

//**********************************************************************************************************************
void Image::copy(ID<Image> source, ID<Buffer> destination, const CopyBufferRegion* regions, uint32 count)
{
	auto graphicsAPI = GraphicsAPI::get();
	auto currentCommandBuffer = graphicsAPI->currentCommandBuffer;
	GARDEN_ASSERT(source);
	GARDEN_ASSERT(destination);
	GARDEN_ASSERT(regions);
	GARDEN_ASSERT(count > 0);
	GARDEN_ASSERT(currentCommandBuffer);
	GARDEN_ASSERT(!graphicsAPI->renderPassFramebuffer);

	auto imageView = graphicsAPI->imagePool.get(source);
	GARDEN_ASSERT_MSG(hasAnyFlag(imageView->getUsage(), Usage::TransferSrc),
		"Missing source image [" + imageView->getDebugName() + "] flag");
	GARDEN_ASSERT_MSG(imageView->instance, "Image [" + imageView->getDebugName() + "] is not ready");

	auto bufferView = graphicsAPI->bufferPool.get(destination);
	GARDEN_ASSERT_MSG(hasAnyFlag(bufferView->getUsage(), Buffer::Usage::TransferSrc),
		"Missing destination buffer [" + bufferView->getDebugName() + "] flag");
	GARDEN_ASSERT_MSG(ResourceExt::getInstance(**bufferView), "Buffer [" + 
		bufferView->getDebugName() + "] is not ready");

	auto commandBufferType = currentCommandBuffer->getType();
	#if GARDEN_DEBUG
	if (commandBufferType == CommandBufferType::TransferOnly)
	{
		GARDEN_ASSERT_MSG(hasAnyFlag(imageView->getUsage(), Usage::TransferQ),
			"Source image [" + imageView->getDebugName() + "] does not have transfer queue flag");
		GARDEN_ASSERT_MSG(hasAnyFlag(bufferView->getUsage(), Buffer::Usage::TransferQ),
			"Destination buffer [" + bufferView->getDebugName() + "] does not have transfer queue flag");
	}
	if (commandBufferType == CommandBufferType::Compute)
	{
		GARDEN_ASSERT_MSG(hasAnyFlag(imageView->getUsage(), Usage::ComputeQ),
			"Source image [" + imageView->getDebugName() + "] does not have compute queue flag");
		GARDEN_ASSERT_MSG(hasAnyFlag(bufferView->getUsage(), Buffer::Usage::ComputeQ),
			"Destination buffer [" + bufferView->getDebugName() + "] does not have compute queue flag");
	}

	for (uint32 i = 0; i < count; i++)
	{
		auto region = regions[i];
		GARDEN_ASSERT(region.imageBaseLayer + region.imageLayerCount <= imageView->getLayerCount());
		GARDEN_ASSERT(region.imageMipLevel < imageView->getMipCount());

		if (region.imageExtent == uint3::zero)
		{
			GARDEN_ASSERT(region.imageOffset == uint3::zero);
		}
		else
		{
			auto mipImageSize = (uint3)calcSizeAtMip3(imageView->size, region.imageMipLevel);
			GARDEN_ASSERT(areAllTrue(region.imageOffset < mipImageSize));
			GARDEN_ASSERT(areAllTrue(region.imageExtent <= mipImageSize));
			GARDEN_ASSERT(areAllTrue(region.imageExtent + region.imageOffset <= mipImageSize));
		}
		if (region.bufferRowLength == 0 && region.bufferImageHeight == 0)
		{
			auto regionBinarySize = toBinarySize((psize)region.imageExtent.x * 
				region.imageExtent.y * region.imageExtent.z, imageView->format);
			GARDEN_ASSERT(region.bufferOffset < bufferView->getBinarySize());
			GARDEN_ASSERT(regionBinarySize <= bufferView->getBinarySize());
			GARDEN_ASSERT(regionBinarySize + region.bufferOffset <= bufferView->getBinarySize());
		}
		else
		{
			// TODO: check buffer out of bounds
		}
	}
	#endif

	CopyBufferImageCommand command;
	command.toBuffer = true;
	command.regionCount = count;
	command.buffer = destination;
	command.image = source;
	command.regions = regions;
	currentCommandBuffer->addCommand(command);

	if (commandBufferType != CommandBufferType::Frame)
	{
		ResourceExt::getBusyLock(**imageView)++;
		ResourceExt::getBusyLock(**bufferView)++;
		currentCommandBuffer->addLockedResource(source);
		currentCommandBuffer->addLockedResource(destination);
	}
}

//**********************************************************************************************************************
void Image::blit(ID<Image> source, ID<Image> destination, 
	const BlitRegion* regions, uint32 count, Sampler::Filter filter)
{
	auto graphicsAPI = GraphicsAPI::get();
	auto currentCommandBuffer = graphicsAPI->currentCommandBuffer;
	GARDEN_ASSERT(source);
	GARDEN_ASSERT(destination);
	GARDEN_ASSERT(regions);
	GARDEN_ASSERT(count > 0);
	GARDEN_ASSERT(currentCommandBuffer);
	GARDEN_ASSERT(!graphicsAPI->renderPassFramebuffer);

	auto srcView = graphicsAPI->imagePool.get(source);
	GARDEN_ASSERT_MSG(hasAnyFlag(srcView->usage, Usage::TransferSrc),
		"Missing source image [" + srcView->getDebugName() + "] flag");
	GARDEN_ASSERT_MSG(srcView->instance, "Source image [" + srcView->getDebugName() + "] is not ready");

	auto dstView = graphicsAPI->imagePool.get(destination);
	GARDEN_ASSERT_MSG(hasAnyFlag(dstView->usage, Usage::TransferDst),
		"Missing destination image [" + srcView->getDebugName() + "] flag");
	GARDEN_ASSERT_MSG(dstView->instance, "Destination image [" + dstView->getDebugName() + "] is not ready");

	#if GARDEN_DEBUG
	for (uint32 i = 0; i < count; i++)
	{
		auto region = regions[i];
		GARDEN_ASSERT(region.srcBaseLayer + region.layerCount <= srcView->getLayerCount());
		GARDEN_ASSERT(region.dstBaseLayer + region.layerCount <= dstView->getLayerCount());
		GARDEN_ASSERT(region.srcMipLevel < srcView->getMipCount());
		GARDEN_ASSERT(region.dstMipLevel < dstView->getMipCount());

		if (region.srcExtent == uint3::zero)
		{
			GARDEN_ASSERT(region.srcOffset == uint3::zero);
		}
		else
		{
			auto mipImageSize = calcSizeAtMip3(srcView->size, region.srcMipLevel);
			GARDEN_ASSERT(areAllTrue(region.srcOffset < (uint3)mipImageSize));
			GARDEN_ASSERT(areAllTrue(region.srcExtent <= (uint3)mipImageSize));
			GARDEN_ASSERT(areAllTrue(region.srcExtent + region.srcOffset <= (uint3)mipImageSize));
		}
		if (region.dstExtent == uint3::zero)
		{
			GARDEN_ASSERT(region.dstOffset == uint3::zero);
		}
		else
		{
			auto mipImageSize = calcSizeAtMip3(dstView->size, region.dstMipLevel);
			GARDEN_ASSERT(areAllTrue(region.dstOffset < (uint3)mipImageSize));
			GARDEN_ASSERT(areAllTrue(region.dstExtent <= (uint3)mipImageSize));
			GARDEN_ASSERT(areAllTrue(region.dstExtent + region.dstOffset <= (uint3)mipImageSize));
		}

		// TODO: take into account format texel size.
	}
	#endif

	BlitImageCommand command;
	command.filter = filter;
	command.regionCount = count;
	command.source = source;
	command.destination = destination;
	command.regions = regions;
	currentCommandBuffer->addCommand(command);

	if (currentCommandBuffer->getType() != CommandBufferType::Frame)
	{
		ResourceExt::getBusyLock(**srcView)++;
		ResourceExt::getBusyLock(**dstView)++;
		currentCommandBuffer->addLockedResource(source);
		currentCommandBuffer->addLockedResource(destination);
	}
}

#if GARDEN_DEBUG || GARDEN_EDITOR
void Image::setDebugName(const string& name)
{
	Resource::setDebugName(name);

	auto graphicsBackend = GraphicsAPI::get()->getBackendType();
	if (graphicsBackend == GraphicsBackend::VulkanAPI)
	{
		#if GARDEN_DEBUG // Note: No GARDEN_EDITOR
		auto vulkanAPI = VulkanAPI::get();
		if (!vulkanAPI->features.debugUtils || !instance)
			return;

		vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eImage, (uint64)instance, name.c_str());
		vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo);
		#endif
	}
	else abort();
}
#endif

//**********************************************************************************************************************
template<typename S, typename D>
static void convertPixels(const void* src, vector<uint8>& dst, psize count) noexcept
{
	dst.resize(count * sizeof(D));
	auto dstPixels = (D*)dst.data();
	auto srcPixels = (const S*)src;

	for (psize i = 0; i < count; i++)
		dstPixels[i] = (D)srcPixels[i];
}
template<typename S, typename D, S MAX_VALUE>
static void convertPixelsMin(const void* src, vector<uint8>& dst, psize count) noexcept
{
	dst.resize(count * sizeof(D));
	auto dstPixels = (D*)dst.data();
	auto srcPixels = (const S*)src;

	for (psize i = 0; i < count; i++)
		dstPixels[i] = (D)std::min(srcPixels[i], MAX_VALUE);
}
template<typename S, typename D>
static void convertPixelsMin(const void* src, vector<uint8>& dst, psize count, S minValue) noexcept
{
	dst.resize(count * sizeof(D));
	auto dstPixels = (D*)dst.data();
	auto srcPixels = (const S*)src;

	for (psize i = 0; i < count; i++)
		dstPixels[i] = (D)std::min(srcPixels[i], minValue);
}
template<typename S, typename D, S MIN_VALUE>
static void convertPixelsMax(const void* src, vector<uint8>& dst, psize count) noexcept
{
	dst.resize(count * sizeof(D));
	auto dstPixels = (D*)dst.data();
	auto srcPixels = (const S*)src;

	for (psize i = 0; i < count; i++)
		dstPixels[i] = (D)std::max(srcPixels[i], MIN_VALUE);
}
template<typename S, typename D, S MIN_VALUE, S MAX_VALUE>
static void convertPixelsClamp(const void* src, vector<uint8>& dst, psize count) noexcept
{
	dst.resize(count * sizeof(D));
	auto dstPixels = (D*)dst.data();
	auto srcPixels = (const S*)src;

	for (psize i = 0; i < count; i++)
		dstPixels[i] = (D)std::clamp(srcPixels[i], MIN_VALUE, MAX_VALUE);
}
template<typename S, typename D, S SRC_MAX_VALUE, D DST_MAX_VALUE>
static void convertPixelsUnorm(const void* src, vector<uint8>& dst, psize count) noexcept
{
	dst.resize(count * sizeof(D));
	auto dstPixels = (D*)dst.data();
	auto srcPixels = (const S*)src;

	static constexpr float mul = (float)((1.0 / SRC_MAX_VALUE) * DST_MAX_VALUE);
	for (psize i = 0; i < count; i++)
		dstPixels[i] = (D)(srcPixels[i] * mul + 0.5f);
}
template<typename S, typename D, S MAX_VALUE>
static void convertPixelsToFloat(const void* src, vector<uint8>& dst, psize count) noexcept
{
	dst.resize(count * sizeof(D));
	auto dstPixels = (D*)dst.data();
	auto srcPixels = (const S*)src;

	static constexpr float mul = 1.0f / MAX_VALUE;
	for (psize i = 0; i < count; i++)
		dstPixels[i] = (D)(srcPixels[i] * mul);
}
template<typename S, typename D, D MAX_VALUE>
static void convertPixelsFromFloat(const void* src, vector<uint8>& dst, psize count) noexcept
{
	dst.resize(count * sizeof(D));
	auto dstPixels = (D*)dst.data();
	auto srcPixels = (const S*)src;

	static constexpr float mul = 1.0f / MAX_VALUE;
	for (psize i = 0; i < count; i++)
		dstPixels[i] = (D)(std::clamp((float)srcPixels[i], 0.0f, 1.0f) * MAX_VALUE + 0.5f);
}
template<typename S>
static void convertPixelsToSrgb(const void* src, vector<uint8>& dst, psize count) noexcept
{
	dst.resize(count * sizeof(Color)); count /= 4;
	auto dstPixels = (Color*)dst.data();
	auto srcPixels = (const S*)src;

	for (psize i = 0; i < count; i++)
		dstPixels[i] = (Color)rgbToSrgb((f32x4)srcPixels[i]);
}
template<typename S, typename D>
static void convertPixelsFromSrgb(const void* src, vector<uint8>& dst, psize count) noexcept
{
	dst.resize(count * sizeof(D)); count /= 4;
	auto dstPixels = (D*)dst.data();
	auto srcPixels = (const S*)src;

	for (psize i = 0; i < count; i++)
		dstPixels[i] = (D)srgbToRgb((f32x4)srcPixels[i]);
}

//**********************************************************************************************************************
const void* Image::convertFormat(const void* src, uint2 size, 
	vector<uint8>& dst, Format srcFormat, Format dstFormat)
{
	GARDEN_ASSERT(src);
	GARDEN_ASSERT(areAllTrue(size > uint2::zero));

	if (srcFormat == dstFormat)
		return src;

	auto srcCompCount = toComponentCount(dstFormat);
	auto dstCompCount = toComponentCount(dstFormat);
	GARDEN_ASSERT(srcCompCount > 0 && dstCompCount > 0);

	vector<uint8> tmpBuffer;
	if (srcCompCount < dstCompCount)
	{
		auto pixelCount = (psize)size.x * size.y;
		auto srcBinarySize = toBinarySize(pixelCount, srcFormat) / pixelCount;
		GARDEN_ASSERT(srcBinarySize > 0);

		auto compBinarySize = srcBinarySize / srcCompCount;
		auto dstBinarySize = dstCompCount * compBinarySize;
		tmpBuffer.resize(pixelCount * dstBinarySize);

		auto end = tmpBuffer.data() + tmpBuffer.size();
		auto srcData = (const uint8*)src;
	
		for (auto i = tmpBuffer.data(); i < end; i += dstBinarySize, srcData += srcBinarySize)
			memcpy(i, srcData, srcBinarySize);
		src = tmpBuffer.data();
	}
	else if (srcCompCount > dstCompCount)
	{
		// TODO: maybe add channels reduction?
		throw GardenError("Image channels count reduction is unsupported."); 
	}

	auto count = (psize)size.x * size.y * dstCompCount;
	switch (srcFormat)
	{
	case Format::UintR8: case Format::UintS8:
		switch (dstFormat)
		{
			case Format::UintR16: convertPixels<uint8, uint16>(src, dst, count); break;
			case Format::UintR32: convertPixels<uint8, uint32>(src, dst, count); break;
			case Format::SintR8: convertPixelsMin<uint8, int8, INT8_MAX>(src, dst, count); break;
			case Format::SintR16: convertPixels<uint8, int16>(src, dst, count); break;
			case Format::SintR32: convertPixels<uint8, int32>(src, dst, count); break;
			case Format::UintR8: case Format::UintS8: return dst.data();
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;
	case Format::UintR8G8:
		switch (dstFormat)
		{
			case Format::UintR16G16: convertPixels<uint8, uint16>(src, dst, count); break;
			case Format::UintR32G32: convertPixels<uint8, uint32>(src, dst, count); break;
			case Format::SintR8G8: convertPixelsMin<uint8, int8, INT8_MAX>(src, dst, count); break;
			case Format::SintR16G16: convertPixels<uint8, int16>(src, dst, count); break;
			case Format::SintR32G32: convertPixels<uint8, int32>(src, dst, count); break;
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;
	case Format::UintR8G8B8A8:
		switch (dstFormat)
		{
			case Format::UintR16G16B16A16: convertPixels<uint8, uint16>(src, dst, count); break;
			case Format::UintR32G32B32A32: convertPixels<uint8, uint32>(src, dst, count); break;
			case Format::SintR8G8B8A8: convertPixelsMin<uint8, int8, INT8_MAX>(src, dst, count); break;
			case Format::SintR16G16B16A16: convertPixels<uint8, int16>(src, dst, count); break;
			case Format::SintR32G32B32A32: convertPixels<uint8, int32>(src, dst, count); break;
			case Format::SrgbR8G8B8A8: convertPixelsToSrgb<Color>(src, dst, count); break;
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;

	case Format::UintR16:
		switch (dstFormat)
		{
			case Format::UintR8: convertPixelsMin<uint16, uint8, UINT8_MAX>(src, dst, count); break;
			case Format::UintR32: convertPixels<uint16, uint32>(src, dst, count); break;
			case Format::SintR8: convertPixelsMin<uint16, int8, INT8_MAX>(src, dst, count); break;
			case Format::SintR16: convertPixelsMin<uint16, int16, INT16_MAX>(src, dst, count); break;
			case Format::SintR32: convertPixels<uint16, int32>(src, dst, count); break;
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;
	case Format::UintR16G16:
		switch (dstFormat)
		{
			case Format::UintR8G8: convertPixelsMin<uint16, uint8, UINT8_MAX>(src, dst, count); break;
			case Format::UintR32G32: convertPixels<uint16, uint32>(src, dst, count); break;
			case Format::SintR8G8: convertPixelsMin<uint16, int8, INT8_MAX>(src, dst, count); break;
			case Format::SintR16G16: convertPixelsMin<uint16, int16, INT16_MAX>(src, dst, count); break;
			case Format::SintR32G32: convertPixels<uint16, int32>(src, dst, count); break;
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;
	case Format::UintR16G16B16A16:
		switch (dstFormat)
		{
			case Format::UintR8G8B8A8: convertPixelsMin<uint16, uint8, UINT8_MAX>(src, dst, count); break;
			case Format::UintR32G32B32A32: convertPixels<uint16, uint32>(src, dst, count); break;
			case Format::SintR8G8B8A8: convertPixelsMin<uint16, int8, INT8_MAX>(src, dst, count); break;
			case Format::SintR16G16B16A16: convertPixelsMin<uint16, int16, INT16_MAX>(src, dst, count); break;
			case Format::SintR32G32B32A32: convertPixels<uint16, int32>(src, dst, count); break;
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;

	case Format::UintR32:
		switch (dstFormat)
		{
			case Format::UintR8: convertPixelsMin<uint32, uint8, UINT8_MAX>(src, dst, count); break;
			case Format::UintR16: convertPixelsMin<uint32, uint16, UINT16_MAX>(src, dst, count); break;
			case Format::SintR8: convertPixelsMin<uint32, int8, INT8_MAX>(src, dst, count); break;
			case Format::SintR16: convertPixelsMin<uint32, int16, INT16_MAX>(src, dst, count); break;
			case Format::SintR32: convertPixelsMin<uint32, int32, INT32_MAX>(src, dst, count); break;
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;
	case Format::UintR32G32:
		switch (dstFormat)
		{
			case Format::UintR8G8: convertPixelsMin<uint32, uint8, UINT8_MAX>(src, dst, count); break;
			case Format::UintR16G16: convertPixelsMin<uint32, uint16, UINT16_MAX>(src, dst, count); break;
			case Format::SintR8G8: convertPixelsMin<uint32, int8, INT8_MAX>(src, dst, count); break;
			case Format::SintR16G16: convertPixelsMin<uint32, int16, INT16_MAX>(src, dst, count); break;
			case Format::SintR32G32: convertPixelsMin<uint32, int32, INT32_MAX>(src, dst, count); break;
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;
	case Format::UintR32G32B32A32:
		switch (dstFormat)
		{
			case Format::UintR8G8B8A8: convertPixelsMin<uint32, uint8, UINT8_MAX>(src, dst, count); break;
			case Format::UintR16G16B16A16: convertPixelsMin<uint32, uint16, UINT16_MAX>(src, dst, count); break;
			case Format::SintR8G8B8A8: convertPixelsMin<uint32, int8, INT8_MAX>(src, dst, count); break;
			case Format::SintR16G16B16A16: convertPixelsMin<uint32, int16, INT16_MAX>(src, dst, count); break;
			case Format::SintR32G32B32A32: convertPixelsMin<uint32, int32, INT32_MAX>(src, dst, count); break;
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;

	case Format::SintR8:
		switch (dstFormat)
		{
			case Format::SintR16: convertPixels<int8, int16>(src, dst, count); break;
			case Format::SintR32: convertPixels<int8, int32>(src, dst, count); break;
			case Format::UintR8: convertPixelsMax<int8, uint8, 0>(src, dst, count); break;
			case Format::UintR16: convertPixelsMax<int8, uint16, 0>(src, dst, count); break;
			case Format::UintR32: convertPixelsMax<int8, uint32, 0>(src, dst, count); break;
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;
	case Format::SintR8G8:
		switch (dstFormat)
		{
			case Format::SintR16G16: convertPixels<int8, int16>(src, dst, count); break;
			case Format::SintR32G32: convertPixels<int8, int32>(src, dst, count); break;
			case Format::UintR8G8: convertPixelsMax<int8, uint8, 0>(src, dst, count); break;
			case Format::UintR16G16: convertPixelsMax<int8, uint16, 0>(src, dst, count); break;
			case Format::UintR32G32: convertPixelsMax<int8, uint32, 0>(src, dst, count); break;
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;
	case Format::SintR8G8B8A8:
		switch (dstFormat)
		{
			case Format::SintR16G16B16A16: convertPixels<int8, int16>(src, dst, count); break;
			case Format::SintR32G32B32A32: convertPixels<int8, int32>(src, dst, count); break;
			case Format::UintR8G8B8A8: convertPixelsMax<int8, uint8, 0>(src, dst, count); break;
			case Format::UintR16G16B16A16: convertPixelsMax<int8, uint16, 0>(src, dst, count); break;
			case Format::UintR32G32B32A32: convertPixelsMax<int8, uint32, 0>(src, dst, count); break;
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;

	case Format::SintR16:
		switch (dstFormat)
		{
			case Format::SintR8: convertPixelsClamp<int16, int8, INT8_MIN, INT8_MAX>(src, dst, count); break;
			case Format::SintR32: convertPixels<int16, int32>(src, dst, count); break;
			case Format::UintR8: convertPixelsClamp<int16, uint8, 0, UINT8_MAX>(src, dst, count); break;
			case Format::UintR16: convertPixelsMax<int16, uint16, 0>(src, dst, count); break;
			case Format::UintR32: convertPixelsMax<int16, uint32, 0>(src, dst, count); break;
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;
	case Format::SintR16G16:
		switch (dstFormat)
		{
			case Format::SintR8G8: convertPixelsClamp<int16, int8, INT8_MIN, INT8_MAX>(src, dst, count); break;
			case Format::SintR32G32: convertPixels<int16, int32>(src, dst, count); break;
			case Format::UintR8G8: convertPixelsClamp<int16, uint8, 0, UINT8_MAX>(src, dst, count); break;
			case Format::UintR16G16: convertPixelsMax<int16, uint16, 0>(src, dst, count); break;
			case Format::UintR32G32: convertPixelsMax<int16, uint32, 0>(src, dst, count); break;
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;
	case Format::SintR16G16B16A16:
		switch (dstFormat)
		{
			case Format::SintR8G8B8A8: convertPixelsClamp<int16, int8, INT8_MIN, INT8_MAX>(src, dst, count); break;
			case Format::SintR32G32B32A32: convertPixels<int16, int32>(src, dst, count); break;
			case Format::UintR8G8B8A8: convertPixelsClamp<int16, uint8, 0, UINT8_MAX>(src, dst, count); break;
			case Format::UintR16G16B16A16: convertPixelsMax<int16, uint16, 0>(src, dst, count); break;
			case Format::UintR32G32B32A32: convertPixelsMax<int16, uint32, 0>(src, dst, count); break;
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;

	case Format::SintR32:
		switch (dstFormat)
		{
			case Format::SintR8: convertPixelsClamp<int32, int8, INT8_MIN, INT8_MAX>(src, dst, count); break;
			case Format::SintR16: convertPixelsClamp<int32, int16, INT16_MIN, INT16_MAX>(src, dst, count); break;
			case Format::UintR8: convertPixelsClamp<int32, uint8, 0, UINT8_MAX>(src, dst, count); break;
			case Format::UintR16: convertPixelsClamp<int32, uint16, 0, UINT16_MAX>(src, dst, count); break;
			case Format::UintR32: convertPixelsMax<int32, uint32, 0>(src, dst, count); break;
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;
	case Format::SintR32G32:
		switch (dstFormat)
		{
			case Format::SintR8G8: convertPixelsClamp<int32, int8, INT8_MIN, INT8_MAX>(src, dst, count); break;
			case Format::SintR16G16: convertPixelsClamp<int32, int16, INT16_MIN, INT16_MAX>(src, dst, count); break;
			case Format::UintR8G8: convertPixelsClamp<int32, uint8, 0, UINT8_MAX>(src, dst, count); break;
			case Format::UintR16G16: convertPixelsClamp<int32, uint16, 0, UINT16_MAX>(src, dst, count); break;
			case Format::UintR32G32: convertPixelsMax<int32, uint32, 0>(src, dst, count); break;
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;
	case Format::SintR32G32B32A32:
		switch (dstFormat)
		{
			case Format::SintR8G8B8A8: convertPixelsClamp<int32, int8, INT8_MIN, INT8_MAX>(src, dst, count); break;
			case Format::SintR16G16B16A16: convertPixelsClamp<int32, int16, INT16_MIN, INT16_MAX>(src, dst, count); break;
			case Format::UintR8G8B8A8: convertPixelsClamp<int32, uint8, 0, UINT8_MAX>(src, dst, count); break;
			case Format::UintR16G16B16A16: convertPixelsClamp<int32, uint16, 0, UINT16_MAX>(src, dst, count); break;
			case Format::UintR32G32B32A32: convertPixelsMax<int32, uint32, 0>(src, dst, count); break;
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;

	case Format::UnormR8:
		switch (dstFormat)
		{
			case Format::UnormR16: convertPixelsUnorm<uint8, uint16, UINT8_MAX, UINT16_MAX>(src, dst, count); break;
			case Format::SfloatR16: convertPixelsToFloat<uint8, math::half, UINT8_MAX>(src, dst, count); break;
			case Format::SfloatR32: convertPixelsToFloat<uint8, float, UINT8_MAX>(src, dst, count); break;

			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;
	case Format::UnormR8G8:
		switch (dstFormat)
		{
			case Format::UnormR16G16: convertPixelsUnorm<uint8, uint16, UINT8_MAX, UINT16_MAX>(src, dst, count); break;
			case Format::SfloatR16G16: convertPixelsToFloat<uint8, math::half, UINT8_MAX>(src, dst, count); break;
			case Format::SfloatR32G32: convertPixelsToFloat<uint8, float, UINT8_MAX>(src, dst, count); break;
			case Format::UintR8G8: case Format::SintR8G8: case Format::SnormR8G8: return dst.data();
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;
	case Format::UnormR8G8B8A8:
		switch (dstFormat)
		{
			case Format::UnormR16G16B16A16: convertPixelsUnorm<uint8, uint16, UINT8_MAX, UINT16_MAX>(src, dst, count); break;
			case Format::SfloatR16G16B16A16: convertPixelsToFloat<uint8, math::half, UINT8_MAX>(src, dst, count); break;
			case Format::SfloatR32G32B32A32: convertPixelsToFloat<uint8, float, UINT8_MAX>(src, dst, count); break;
			case Format::UintR8G8B8A8: case Format::SintR8G8B8A8: case Format::SnormR8G8B8A8: return dst.data();
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;

	case Format::UnormR16: case Format::UnormD16:
		switch (dstFormat)
		{
			case Format::UnormR8: convertPixelsUnorm<uint16, uint8, UINT16_MAX, UINT8_MAX>(src, dst, count); break;
			case Format::SfloatR16: convertPixelsToFloat<uint16, math::half, UINT16_MAX>(src, dst, count); break;
			case Format::SfloatR32: convertPixelsToFloat<uint16, float, UINT16_MAX>(src, dst, count); break;
			case Format::UnormR16: case Format::UnormD16: return dst.data();
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;
	case Format::UnormR16G16:
		switch (dstFormat)
		{
			case Format::UnormR8G8: convertPixelsUnorm<uint16, uint8, UINT16_MAX, UINT8_MAX>(src, dst, count); break;
			case Format::SfloatR16G16: convertPixelsToFloat<uint16, math::half, UINT16_MAX>(src, dst, count); break;
			case Format::SfloatR32G32: convertPixelsToFloat<uint16, float, UINT16_MAX>(src, dst, count); break;
			case Format::UintR16G16: case Format::SintR16G16: case Format::SnormR16G16: return dst.data();
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;
	case Format::UnormR16G16B16A16:
		switch (dstFormat)
		{
			case Format::UnormR8G8B8A8: case Format::UnormB8G8R8A8:
				convertPixelsUnorm<uint16, uint8, UINT16_MAX, UINT8_MAX>(src, dst, count); break;
			case Format::SfloatR16G16B16A16: convertPixelsToFloat<uint16, math::half, UINT16_MAX>(src, dst, count); break;
			case Format::SfloatR32G32B32A32: convertPixelsToFloat<uint16, float, UINT16_MAX>(src, dst, count); break;
			case Format::UintR16G16B16A16: case Format::SintR16G16B16A16: case Format::SnormR16G16B16A16: return dst.data();
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;

	case Format::SfloatR16:
		switch (dstFormat)
		{
			case Format::SfloatR32: case Format::SfloatD32: convertPixels<math::half, float>(src, dst, count); break;
			case Format::UnormR8: convertPixelsFromFloat<math::half, uint8, UINT8_MAX>(src, dst, count); break;
			case Format::UnormR16: convertPixelsFromFloat<math::half, uint16, UINT16_MAX>(src, dst, count); break;
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;
	case Format::SfloatR16G16:
		switch (dstFormat)
		{
			case Format::SfloatR32G32: convertPixels<math::half, float>(src, dst, count); break;
			case Format::UnormR8G8: convertPixelsFromFloat<math::half, uint8, UINT8_MAX>(src, dst, count); break;
			case Format::UnormR16G16: convertPixelsFromFloat<math::half, uint16, UINT16_MAX>(src, dst, count); break;
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;
	case Format::SfloatR16G16B16A16:
		switch (dstFormat)
		{
			case Format::SfloatR32G32B32A32: convertPixels<math::half, float>(src, dst, count); break;
			case Format::UnormR8G8B8A8: convertPixelsFromFloat<math::half, uint8, UINT8_MAX>(src, dst, count); break;
			case Format::UnormR16G16B16A16: convertPixelsFromFloat<math::half, uint16, UINT16_MAX>(src, dst, count); break;
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;

	case Format::SfloatR32: case Format::SfloatD32:
		switch (dstFormat)
		{
			case Format::SfloatR16: convertPixelsMin<float, math::half>(src, dst, count, FLOAT_BIG_16); break;
			case Format::UnormR8: convertPixelsFromFloat<math::half, uint8, UINT8_MAX>(src, dst, count); break;
			case Format::UnormR16: convertPixelsFromFloat<math::half, uint16, UINT16_MAX>(src, dst, count); break;
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;
	case Format::SfloatR32G32:
		switch (dstFormat)
		{
			case Format::SfloatR16G16: convertPixelsMin<float, math::half>(src, dst, count, FLOAT_BIG_16); break;
			case Format::UnormR8G8: convertPixelsFromFloat<math::half, uint8, UINT8_MAX>(src, dst, count); break;
			case Format::UnormR16G16: convertPixelsFromFloat<math::half, uint16, UINT16_MAX>(src, dst, count); break;
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;
	case Format::SfloatR32G32B32A32:
		switch (dstFormat)
		{
			case Format::SfloatR16G16B16A16: convertPixelsMin<float, math::half>(src, dst, count, FLOAT_BIG_16); break;
			case Format::UnormR8G8B8A8: convertPixelsFromFloat<math::half, uint8, UINT8_MAX>(src, dst, count); break;
			case Format::UnormR16G16B16A16: convertPixelsFromFloat<math::half, uint16, UINT16_MAX>(src, dst, count); break;
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;

	case Format::SrgbR8G8B8A8:
		switch (dstFormat)
		{
			case Format::UintR8G8B8A8: convertPixelsFromSrgb<Color, Color>(src, dst, count); break;
			case Format::SfloatR16G16B16A16: convertPixelsFromSrgb<Color, f16x4>(src, dst, count); break;
			case Format::SfloatR32G32B32A32: convertPixelsFromSrgb<Color, f32x4>(src, dst, count); break;
			default: throw GardenError("Unsupported image formats conversion.");
		}
		break;

	default: throw GardenError("Unsupported image formats conversion.");
	}
	return dst.data();
}

//**********************************************************************************************************************
namespace
{
	class ExrMemoryStream final : public Imf::IStream
	{
		const uint8* data = nullptr;
		psize size = 0, pos = 0;
	public:
		ExrMemoryStream(const uint8* data, psize size) : 
			Imf::IStream("memory"), data(data), size(size) { }
		~ExrMemoryStream() override { }

		bool isMemoryMapped() const override { return true; }

		bool read(char c[/*n*/], int n) override
		{
			if (pos + n > size)
				throw runtime_error("Out of EXR file bounds.");
			memcpy(c, data + pos, n); pos += n;
			return pos != size;
		}
		char* readMemoryMapped(int n) override
		{
			if (pos + n > size)
				throw runtime_error("Out of EXR file bounds.");
			auto memory = (char*)data + pos; pos += n;
			return memory;
		}

		uint64_t tellg() override { return pos; }
		void seekg(uint64_t pos) override { this->pos = pos; }
		void clear() override { }

		/* TODO: uncomment when will be widely supported.

		int64_t size() override { return (int64_t)size; }
		bool isStatelessRead() const override { return true; }

		int64_t read(void* buf, uint64_t sz, uint64_t offset) override
		{
			if (offset > size)
				return 0;
			auto remaining = (uint64_t)size - offset;
    		auto bytesToRead = (sz < remaining) ? sz : remaining;
			memcpy(buf, data + offset, bytesToRead);
			return bytesToRead;
		}
		*/
	};
}

//**********************************************************************************************************************
static int webpWriter(const uint8_t* data, size_t dataSize, const WebPPicture* picture) noexcept
{
	auto outputStream = (ofstream*)picture->custom_ptr;
	return outputStream->write((const char*)data, dataSize) ? true : false;
}

static uint32 toPngFormat(int componentCount)
{
	switch (componentCount)
	{
		case 4: return PNG_FORMAT_RGBA;
		case 2: return PNG_FORMAT_GA;
		case 1: return PNG_FORMAT_GRAY;
		default: throw GardenError("Unsupported PNG image channel count.");
	}
}
static Image::Format toSrgbFormat(int componentCount)
{
	switch (componentCount)
	{
		case 4: return Image::Format::SrgbR8G8B8A8;
		case 2: return Image::Format::SrgbR8G8;
		case 1: return Image::Format::SrgbR8;
		default: throw GardenError("Unsupported sRGB image channel count.");
	}
}
static Image::Format toFloatFormat(int componentCount)
{
	switch (componentCount)
	{
		case 4: return Image::Format::SfloatR32G32B32A32;
		case 2: return Image::Format::SfloatR32G32;
		case 1: return Image::Format::SfloatR32;
		default: throw GardenError("Unsupported float image channel count.");
	}
}

//**********************************************************************************************************************
static Image::Format toImageFormat(Image::Format imageFormat, 
	basist::basis_tex_format texFormat, int componentCount, bool isSrgb)
{
	bool hasASTC_LDR, hasASTC_HDR, hasBCn;
	if (GraphicsAPI::isInitialized())
	{
		auto graphicsAPI = GraphicsAPI::get();
		hasASTC_LDR = graphicsAPI->hasASTC_LDR();
		hasASTC_HDR = graphicsAPI->hasASTC_HDR();
		hasBCn = graphicsAPI->hasBCn();
	}
	else hasASTC_LDR = hasASTC_HDR = hasBCn = false;

	switch (texFormat)
	{
	case basist::basis_tex_format::cXUASTC_LDR_4x4:
		if (imageFormat == Image::Format::SrgbR8G8B8A8 || imageFormat == Image::Format::UnormR8G8B8A8)
			return imageFormat;
		if (hasASTC_LDR)
			return isSrgb ? Image::Format::SrgbAstc4x4 : Image::Format::UnormAstc4x4;
		if (hasBCn)
		{
			switch (componentCount)
			{
				case 1: return Image::Format::UnormBC4;
				case 2: return Image::Format::UnormBC5;
				case 3: return Image::Format::UnormRgbBC1;
				case 4: return Image::Format::UnormBC7;
				default: abort();
			}
		}
		return isSrgb ? Image::Format::SrgbR8G8B8A8 : Image::Format::UnormR8G8B8A8;
	case basist::basis_tex_format::cUASTC_HDR_4x4:
		if (imageFormat == Image::Format::SfloatR16G16B16A16)
			return Image::Format::SfloatR16G16B16A16;
		if (hasASTC_HDR) return Image::Format::SfloatAstc4x4;
		if (hasBCn) return Image::Format::UfloatBC6H;
		return Image::Format::SfloatR16G16B16A16;
	default: throw GardenError("Unsupported basis universal texture format.");
	}
}
static basist::transcoder_texture_format toTranscoderFormat(Image::Format imageFormat)
{
	switch (imageFormat)
	{
		case Image::Format::SrgbRgbBC1: case Image::Format::UnormRgbBC1:
			return basist::transcoder_texture_format::cTFBC1_RGB;
		case Image::Format::SrgbBC3: case Image::Format::UnormBC3:
			return basist::transcoder_texture_format::cTFBC3_RGBA;
		case Image::Format::UnormBC4: return basist::transcoder_texture_format::cTFBC4_R;
		case Image::Format::UnormBC5: return basist::transcoder_texture_format::cTFBC5_RG;
		case Image::Format::UnormBC7: return basist::transcoder_texture_format::cTFBC7_RGBA;
		case Image::Format::SrgbAstc4x4: case Image::Format::UnormAstc4x4:
			return basist::transcoder_texture_format::cTFASTC_LDR_4x4_RGBA;
		case Image::Format::UfloatBC6H: return basist::transcoder_texture_format::cTFBC6H;
		case Image::Format::SfloatAstc4x4: return basist::transcoder_texture_format::cTFASTC_HDR_4x4_RGBA;
		case Image::Format::SrgbR8G8B8A8: case Image::Format::UnormR8G8B8A8:
			return basist::transcoder_texture_format::cTFRGBA32;
		case Image::Format::UnormR5G6B5: return basist::transcoder_texture_format::cTFRGB565;
		case Image::Format::UnormB5G6R5: return basist::transcoder_texture_format::cTFBGR565;
		case Image::Format::UnormR4G4B4A4: return basist::transcoder_texture_format::cTFRGBA4444;
		case Image::Format::SfloatR16G16B16A16: return basist::transcoder_texture_format::cTFRGBA_HALF;
		case Image::Format::UfloatE5B9G9R9: return basist::transcoder_texture_format::cTFRGB_9E5;
		default: throw GardenError("Unsupported basis universal transcoder format.");
	}
}

//**********************************************************************************************************************
void Image::loadFileData(const void* data, psize dataSize, vector<uint8>& pixels, 
	uint2& imageSize, FileType fileType, Format& imageFormat)
{
	GARDEN_ASSERT(data);
	GARDEN_ASSERT(dataSize > 0);

	auto componentCount = imageFormat == Format::Undefined ? 
		4 : toComponentCount(imageFormat);
	GARDEN_ASSERT(componentCount > 0);

	vector<uint8> tmpPixels; Format loadedFormat;
	if (fileType == FileType::KTX2)
	{
		#if GARDEN_USE_BASIS_UNIVERSAL
		if (dataSize > UINT32_MAX)
			throw GardenError("KTX2 image data size is too big.");

		basist::ktx2_transcoder transcoder;
		if (!transcoder.init(data, (uint32_t)dataSize))
			throw GardenError("Invalid KTX2 basis universal image data.");

		imageSize = uint2(transcoder.get_width(), transcoder.get_height());
		loadedFormat = ::toImageFormat(imageFormat, 
			transcoder.get_basis_tex_format(), componentCount, transcoder.is_srgb());
		auto transcoderFormat = toTranscoderFormat(loadedFormat);
		auto blockSize = basist::basis_transcoder_format_is_uncompressed(transcoderFormat) ? 1 : 
			basist::basis_get_block_width(transcoderFormat) * basist::basis_get_block_height(transcoderFormat);
		auto pixelCount = (psize)imageSize.x * imageSize.y;
		auto imageBinarySize = toBinarySize(pixelCount, loadedFormat);
		pixels.resize(imageBinarySize);

		auto decodeFlags = basist::basisu_decode_flags::cDecodeFlagsHighQuality;
		transcoder.start_transcoding();

		if (!transcoder.transcode_image_level(0, 0, 0, pixels.data(), 
			pixelCount / blockSize, transcoderFormat, decodeFlags))
		{
			throw GardenError("Failed to transcode basis universal image.");
		}
		#else
		throw GardenError("No Binomial Basis Universal support.");
		#endif
	}
	else if (fileType == FileType::WebP)
	{
		int sizeX = 0, sizeY = 0;
		if (!WebPGetInfo((const uint8_t*)data, dataSize, &sizeX, &sizeY))
			throw GardenError("Invalid WebP image info.");

		imageSize = uint2(sizeX, sizeY);
		loadedFormat = toSrgbFormat(componentCount);
		pixels.resize((psize)imageSize.x * imageSize.y * sizeof(Color));

		auto decodeResult = WebPDecodeRGBAInto((const uint8_t*)data, dataSize, 
			pixels.data(), pixels.size(), (int)(imageSize.x  * sizeof(Color)));
		if (!decodeResult)
			throw GardenError("Invalid WebP image data.");

		if (componentCount != 4)
		{
			auto pixelCount = (psize)imageSize.x * imageSize.y;
			tmpPixels.resize(pixelCount * componentCount);
			swap(pixels, tmpPixels);

			auto srcPixels = (const Color*)tmpPixels.data();
			auto dstPixels = (uint8*)pixels.data();
			if (componentCount == 2)
			{
				for (psize i = 0, j = 0; i < pixelCount; i++, j += 2)
				{
					auto p = srcPixels[i];
					dstPixels[j] = p.r; dstPixels[j + 1] = p.g;
				}
			}
			else if (componentCount == 1)
			{
				for (psize i = 0; i < pixelCount; i++)
					dstPixels[i] = srcPixels[i].r;
			}
			else throw GardenError("Unsupported WebP image channel count.");
		}
	}
	else if (fileType == FileType::PNG)
	{
		png_image image; memset(&image, 0, sizeof(png_image));
		image.version = PNG_IMAGE_VERSION;

		if (!png_image_begin_read_from_memory(&image, data, dataSize))
			throw GardenError("Invalid PNG image info.");

		imageSize = uint2(image.width, image.height);
		loadedFormat = toSrgbFormat(componentCount);
		image.format = toPngFormat(componentCount);
		pixels.resize((psize)imageSize.x * imageSize.y * componentCount);

		if (!png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr))
			throw GardenError("Invalid PNG image data.");
	}
	else if (fileType == FileType::EXR)
	{
		ExrMemoryStream exrStream((const uint8*)data, dataSize);
		if (componentCount == 4 && (imageFormat == Format::Undefined || 
			imageFormat == Format::SfloatR16G16B16A16))
		{
			Imf::RgbaInputFile exrFile(exrStream);
			auto dw = exrFile.dataWindow();
			imageSize = uint2(dw.max.x - dw.min.x + 1, dw.max.y - dw.min.y + 1);
			loadedFormat = Format::SfloatR16G16B16A16;
			auto pixelCount = (psize)imageSize.x * imageSize.y;
			pixels.resize(pixelCount * sizeof(Imf::Rgba));
			auto pixels16 = (Imf::Rgba*)pixels.data();
			exrFile.setFrameBuffer(pixels16 - dw.min.x - dw.min.y * imageSize.x, 1, imageSize.x);
			exrFile.readPixels(dw.min.y, dw.max.y);
		}
		else
		{
			Imf::InputFile exrFile(exrStream);
			auto dw = exrFile.header().dataWindow();
			imageSize = uint2(dw.max.x - dw.min.x + 1, dw.max.y - dw.min.y + 1);
			auto pixelCount = (psize)imageSize.x * imageSize.y;
			auto imageBinarySize = toBinarySize(pixelCount, imageFormat);

			if (imageBinarySize == 0)
				throw GardenError("Unsupported EXR image format.");

			auto pixelBinarySize = imageBinarySize / pixelCount;
			auto floatSize = pixelBinarySize / componentCount;
			auto strideY = pixelBinarySize * imageSize.x;
			pixels.resize(imageBinarySize);

			char* exrPixels; Imf::PixelType pixelType;
			if (floatSize == 4)
			{
				exrPixels = (char*)((float*)pixels.data() - dw.min.x - dw.min.y * imageSize.x);
				pixelType = Imf::FLOAT;
			}
			else if (floatSize == 2)
			{
				exrPixels = (char*)((Imath::half*)pixels.data() - dw.min.x - dw.min.y * imageSize.x);
				pixelType = Imf::HALF;
			}
			else throw GardenError("Unsupported EXR image float binary size.");

			Imf::FrameBuffer exrFrameBuffer;
			exrFrameBuffer.insert("R", Imf::Slice(pixelType, exrPixels, pixelBinarySize, strideY));
			if (componentCount > 1)
				exrFrameBuffer.insert("G", Imf::Slice(pixelType, exrPixels + floatSize * 1, pixelBinarySize, strideY));
			if (componentCount > 2)
				exrFrameBuffer.insert("B", Imf::Slice(pixelType, exrPixels + floatSize * 2, pixelBinarySize, strideY));
			if (componentCount > 3)
				exrFrameBuffer.insert("A", Imf::Slice(pixelType, exrPixels + floatSize * 3, pixelBinarySize, strideY));
			exrFile.setFrameBuffer(exrFrameBuffer);
			exrFile.readPixels(dw.min.y, dw.max.y);
		}
	}
	else if (fileType == FileType::HDR)
	{
		if (dataSize > INT32_MAX)
			throw GardenError("HDR image data size is too big.");

		int sizeX = 0, sizeY = 0;
		auto pixelData = stbi_loadf_from_memory((const stbi_uc*)data, 
			(int)dataSize, &sizeX, &sizeY, nullptr, componentCount);
		if (!pixelData)
			throw GardenError("Invalid HDR image data.");

		imageSize = uint2(sizeX, sizeY);
		loadedFormat = toFloatFormat(componentCount);
		pixels.assign((const uint8*)pixelData, (const uint8*)pixelData +
			(psize)imageSize.x * imageSize.y * sizeof(float4));
		stbi_image_free(pixelData); // TODO: this is suboptimal to copy data over.
	}
	else if (fileType == FileType::JPEG | fileType == FileType::BMP |
		fileType == FileType::PSD | fileType == FileType::TGA |
		fileType == FileType::PIC | fileType == FileType::GIF)
	{
		if (dataSize > INT32_MAX)
			throw GardenError("STB image data size is too big.");

		int sizeX = 0, sizeY = 0;
		auto pixelData = stbi_load_from_memory((const stbi_uc*)data, 
			(int)dataSize, &sizeX, &sizeY, nullptr, componentCount);
		if (!pixelData)
			throw GardenError("Invalid STB image data.");

		imageSize = uint2(sizeX, sizeY);
		loadedFormat = toSrgbFormat(componentCount);
		pixels.assign(pixelData, pixelData + (psize)imageSize.x * imageSize.y * sizeof(Color));
		stbi_image_free(pixelData); // TODO: this is suboptimal to copy data over.
	}
	else abort();

	if (imageFormat == Format::Undefined)
	{
		imageFormat = loadedFormat;
		return;
	}

	auto dstPixels = convertFormat(pixels.data(), 
		imageSize, tmpPixels, loadedFormat, imageFormat);
	if (dstPixels == tmpPixels.data())
		swap(pixels, tmpPixels);
}

//**********************************************************************************************************************
void Image::storeFileData(const fs::path& path, const void* pixels, uint2 size, 
	FileType fileType, Format imageFormat, float quality, float effort, StoreFlag flags)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT_MSG(pixels, "Assert " + path.generic_string());
	GARDEN_ASSERT_MSG(areAllTrue(size > uint2::zero), "Assert " + path.generic_string());
	GARDEN_ASSERT_MSG(imageFormat != Format::Undefined, "Assert " + path.generic_string());
	GARDEN_ASSERT_MSG(quality >= 0.0f && quality <= 1.0f, "Assert " + path.generic_string());
	GARDEN_ASSERT_MSG(effort >= 0.0f && effort <= 1.0f, "Assert " + path.generic_string());

	auto filePath = path; vector<uint8> tmpPixels; 
	auto componentCount = toComponentCount(imageFormat);
	GARDEN_ASSERT(componentCount > 0);

	if (fileType == FileType::KTX2)
	{
		#if GARDEN_EDITOR
		uint32_t basisFlags = basisu::cFlagUseOpenCL | basisu::cFlagKTX2 | 
			basisu::cFlagKTX2UASTCSuperCompression | basisu::cFlagXUASTCLDRSyntaxFullZStd;
		if (hasAnyFlag(flags, StoreFlag::GenerateMips))
			basisFlags |= basisu::cFlagGenMipsClamp;

		const void* dstPixels = pixels;
		basist::basis_tex_format basisFormat; bool isHDR;

		switch (imageFormat)
		{
			case Image::Format::SrgbR8G8B8A8:
				basisFlags |= basisu::cFlagSRGB;
				// Note: falling through to the next case.
			case Image::Format::UintR8G8B8A8:
				if (quality < 1.0f)
				{
					GARDEN_ASSERT_MSG(!hasAnyFlag(flags, StoreFlag::BlockSize6x6 | 
						StoreFlag::BlockSize8x8), "Unsupported ETC1S block size.");
					basisFormat = basist::basis_tex_format::cETC1S;
				}
				else
				{
					if (hasAnyFlag(flags, StoreFlag::BlockSize6x6))
						basisFormat = basist::basis_tex_format::cXUASTC_LDR_6x6;
					else if (hasAnyFlag(flags, StoreFlag::BlockSize8x8))
						basisFormat = basist::basis_tex_format::cXUASTC_LDR_8x8;
					// TODO: more ASTC block sizes? Any use cases?
					else basisFormat = basist::basis_tex_format::cXUASTC_LDR_4x4;
				}
				isHDR = false;
				break;
			case Image::Format::SfloatR32G32B32A32:
				dstPixels = convertFormat(pixels, size, tmpPixels, imageFormat, Format::SfloatR32G32B32A32);
				// Note: falling through to the next case.
			case Image::Format::SfloatR16G16B16A16:
				if (hasAnyFlag(flags, StoreFlag::BlockSize6x6))
					basisFormat = basist::basis_tex_format::cUASTC_HDR_6x6_INTERMEDIATE;
				else if (hasAnyFlag(flags, StoreFlag::BlockSize8x8))
					throw GardenError("Unsupported UASTC HDR block size. (path: " + path.generic_string() + ")");
				else basisFormat = basist::basis_tex_format::cUASTC_HDR_4x4;
				isHDR = true;
				break;

			default:
				throw GardenError("Unsupported basis universal "
					"format conversion. (path: " + path.generic_string() + ")");;
		}

		auto basisQuality = (int)std::fma(quality, 100.0f, 0.5f);
		auto basisEffort = (int)std::fma(effort, 10.0f, 0.5f);
		
		void* ktxData; size_t ktxDataSize = 0;
		if (isHDR)
		{
			basisu::vector<basisu::imagef> images(1);
			auto image = &images[0]; image->resize(size.x, size.y);
			memcpy((void*)image->get_ptr(), dstPixels, (psize)size.x * size.y * sizeof(basisu::vec4F));
			ktxData = basisu::basis_compress2(basisFormat, images, basisFlags, basisQuality, basisEffort, &ktxDataSize);
		}
		else
		{
			basisu::vector<basisu::image> images(1);
			auto image = &images[0]; image->resize(size.x, size.y);
			memcpy((void*)image->get_ptr(), dstPixels, (psize)size.x * size.y * sizeof(basisu::color_rgba));
			ktxData = basisu::basis_compress2(basisFormat, images, basisFlags, basisQuality, basisEffort, &ktxDataSize);
		}

		if (!ktxData)
		{
			throw GardenError("Failed to compress basis universal image file. ("
				"path: " + path.generic_string() + ")");
		}

		filePath.replace_extension(".ktx2");
		File::storeBinary(filePath, ktxData, ktxDataSize);
		basisu::basis_free_data(ktxData);
		#else
		throw GardenError("No basis universal compression support."); // TODO: allow to override with CMake define?
		#endif
	}
	else if (fileType == FileType::WebP)
	{
		GARDEN_ASSERT_MSG(!hasAnyFlag(flags, StoreFlag::GenerateMips), "WebP does not store mip map levels");
		GARDEN_ASSERT_MSG(!hasAnyFlag(flags, StoreFlag::BlockSize6x6 | 
			StoreFlag::BlockSize8x8), "WebP does not support block compression");
		auto dstPixels = convertFormat(pixels, size, tmpPixels, imageFormat, Format::SrgbR8G8B8A8);

		filePath.replace_extension(".webp");
		ofstream outputStream(filePath, ios::binary | ios::trunc);
		if (!outputStream.is_open())
			throw GardenError("Failed to open WebP image file. (path: " + path.generic_string() + ")");

		WebPConfig config;
		if (!WebPConfigPreset(&config, WEBP_PRESET_DEFAULT, quality * 100.0f))
			throw GardenError("Failed to configure WebP presset. (path: " + path.generic_string() + ")");

		if (quality == 1.0f) config.lossless = 1;
		config.method = (int)std::fma(effort, 6.0f, 0.5f);

		if (!WebPValidateConfig(&config))
			throw GardenError("Invalid WebP config. (path: " + path.generic_string() + ")");

		WebPPicture pic;
		if (!WebPPictureInit(&pic))
			throw GardenError("Failed to init WebP picture. (path: " + path.generic_string() + ")");

		pic.use_argb = 1;
		pic.width = size.x;
		pic.height = size.y;

		if (!WebPPictureImportRGBA(&pic, (uint8_t*)pixels, size.x * sizeof(Color)))
			throw GardenError("Failed to import WebP picture. (path: " + path.generic_string() + ")");

		pic.writer = webpWriter;
		pic.custom_ptr = &outputStream;

		auto webpResult = WebPEncode(&config, &pic);
		WebPPictureFree(&pic);

		if (!webpResult)
			throw GardenError("Failed to encode WebP image. (path: " + path.generic_string() + ")");
	}
	else if (fileType == FileType::PNG)
	{
		GARDEN_ASSERT_MSG(quality == 1.0f, "PNG is a lossless format, can't specify quality");
		GARDEN_ASSERT_MSG(!hasAnyFlag(flags, StoreFlag::GenerateMips), "PNG does not store mip map levels");
		GARDEN_ASSERT_MSG(!hasAnyFlag(flags, StoreFlag::BlockSize6x6 | 
			StoreFlag::BlockSize8x8), "PNG does not support block compression");
		auto dstPixels = convertFormat(pixels, size, tmpPixels, imageFormat, toSrgbFormat(componentCount));

		filePath.replace_extension(".png");
		auto pngFile = fopen(filePath.generic_string().c_str(), "wb");
		if (!pngFile)
			throw GardenError("Failed to open PNG image file. (path: " + path.generic_string() + ")");

		auto png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
		auto pngInfo = png_create_info_struct(png);

		if (!pngInfo || !pngInfo)
		{
			png_destroy_write_struct(&png, &pngInfo);
			fclose(pngFile);
			throw GardenError("Failed to create PNG structs. (path: " + path.generic_string() + ")");
		}

		if (setjmp(png_jmpbuf(png)))
		{
			png_destroy_write_struct(&png, &pngInfo);
			fclose(pngFile);
			throw GardenError("Failed to write PNG image. (path: " + path.generic_string() + ")");
		}

		png_init_io(png, pngFile);
		png_set_IHDR(png, pngInfo, size.x, size.y, 8, PNG_COLOR_TYPE_RGBA, 
			PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
		png_set_compression_level(png, (int)std::fma(effort, 9.0f, 0.5f));

		png_write_info(png, pngInfo);
		auto pngPixels = (png_const_bytep)dstPixels;
		auto rowStride = sizeof(Color) * size.x;
		for (uint32 y = 0; y < size.y; y++)
			png_write_row(png, pngPixels + y * rowStride);
		png_write_end(png, NULL);

		png_destroy_write_struct(&png, &pngInfo);
		fclose(pngFile);
	}
	else if (fileType == FileType::EXR)
	{
		GARDEN_ASSERT_MSG(quality == 1.0f, "EXR is a lossless format, can't specify quality");
		GARDEN_ASSERT_MSG(!hasAnyFlag(flags, StoreFlag::GenerateMips), "Not implemented yet");
		GARDEN_ASSERT_MSG(!hasAnyFlag(flags, StoreFlag::BlockSize6x6 | 
			StoreFlag::BlockSize8x8), "EXR does not support block compression");
		auto dstPixels = convertFormat(pixels, size, tmpPixels, imageFormat, Format::SfloatR16G16B16A16);
		filePath.replace_extension(".exr");

		try // TODO: also support writing SfloatR32G32B32A32 and R/RG channel formats?
		{
			Imf::RgbaOutputFile file(filePath.generic_string().c_str(), size.x, size.y, 
				Imf::WRITE_RGBA, 1, Imath::V2f(0, 0), 1, Imf::INCREASING_Y, Imf::PIZ_COMPRESSION, 1);
			file.setFrameBuffer((const Imf::Rgba*)dstPixels, 1, size.x);
			file.writePixels(size.y);
		}
		catch (exception& e)
		{
			throw GardenError("Failed to write EXR image. (path: " + 
				filePath.generic_string() + ", error: " + string(e.what()) + ")");
		}
	}
	else if (fileType == FileType::HDR)
	{
		GARDEN_ASSERT_MSG(quality == 1.0f, "HDR is a lossless format, can't specify quality");
		GARDEN_ASSERT_MSG(!hasAnyFlag(flags, StoreFlag::GenerateMips), "HDR does not store mip map levels");
		GARDEN_ASSERT_MSG(!hasAnyFlag(flags, StoreFlag::BlockSize6x6 | 
			StoreFlag::BlockSize8x8), "HDR does not support block compression");
		auto dstPixels = convertFormat(pixels, size, tmpPixels, imageFormat, toFloatFormat(componentCount));
	
		filePath.replace_extension(".hdr");
		if (!stbi_write_hdr(filePath.generic_string().c_str(), size.x, size.y, componentCount, (const float*)dstPixels))
			throw GardenError("Failed to write HDR image. (path: " + path.generic_string() + ")");
	}
	else if (fileType == FileType::JPEG)
	{
		GARDEN_ASSERT_MSG(!hasAnyFlag(flags, StoreFlag::GenerateMips), "JPEG does not store mip map levels");
		GARDEN_ASSERT_MSG(!hasAnyFlag(flags, StoreFlag::BlockSize6x6 | 
			StoreFlag::BlockSize8x8), "JPEG does not support custom block compression");
		auto dstPixels = convertFormat(pixels, size, tmpPixels, imageFormat, toSrgbFormat(componentCount));
		auto jpgQuality = (int)std::fma(quality, 100.0f, 0.5f);

		filePath.replace_extension(".jpg");
		if (!stbi_write_jpg(filePath.generic_string().c_str(), size.x, size.y, componentCount, dstPixels, jpgQuality))
			throw GardenError("Failed to write JPEG image. (path: " + path.generic_string() + ")");
	}
	else if (fileType == FileType::BMP)
	{
		GARDEN_ASSERT_MSG(quality == 1.0f, "BMP is a lossless format, can't specify quality");
		GARDEN_ASSERT_MSG(!hasAnyFlag(flags, StoreFlag::BlockSize6x6 | 
			StoreFlag::BlockSize8x8), "BMP does not support block compression");
		GARDEN_ASSERT_MSG(!hasAnyFlag(flags, StoreFlag::GenerateMips), "BMP does not store mip map levels");
		auto dstPixels = convertFormat(pixels, size, tmpPixels, imageFormat, toSrgbFormat(componentCount));

		filePath.replace_extension(".bmp");
		if (!stbi_write_bmp(filePath.generic_string().c_str(), size.x, size.y, componentCount, dstPixels))
			throw GardenError("Failed to write BMP image. (path: " + path.generic_string() + ")");
	}
	else if (fileType == FileType::TGA)
	{
		GARDEN_ASSERT_MSG(quality == 1.0f, "TGA is a lossless format, can't specify quality");
		GARDEN_ASSERT_MSG(!hasAnyFlag(flags, StoreFlag::BlockSize6x6 | 
			StoreFlag::BlockSize8x8), "TGA does not support block compression");
		GARDEN_ASSERT_MSG(!hasAnyFlag(flags, StoreFlag::GenerateMips), "TGA does not store mip map levels");
		auto dstPixels = convertFormat(pixels, size, tmpPixels, imageFormat, toSrgbFormat(componentCount));

		filePath.replace_extension(".tga");
		if (!stbi_write_tga(filePath.generic_string().c_str(), size.x, size.y, componentCount, dstPixels))
			throw GardenError("Failed to write TGA image. (path: " + path.generic_string() + ")");
	}
	else throw GardenError("Unsupported image write file format. (path: " + path.generic_string() + ")");
}

//**********************************************************************************************************************
static void* createVkImageView(View<Image> imageView, Image::Type type, Image::Format format, 
	uint32 baseLayer, uint32 layerCount, uint8 baseMip, uint8 mipCount, uint32& apiFormat, uint32& aspectFlags)
{
	auto imageFormat = isFormatDepthAndStencil(imageView->getFormat()) ? imageView->getFormat() : format;
	vk::ImageViewCreateInfo imageViewInfo({}, (VkImage)ResourceExt::getInstance(**imageView),
		toVkImageViewType(type), toVkFormat(imageFormat), vk::ComponentMapping(
			vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity,
			vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity),
		// TODO: utilize component swizzle
		vk::ImageSubresourceRange(toVkImageAspectFlags(format), baseMip, mipCount, baseLayer, layerCount));
	apiFormat = (uint32)imageViewInfo.format; aspectFlags = (uint32)imageViewInfo.subresourceRange.aspectMask;
	return (VkImageView)VulkanAPI::get()->device.createImageView(imageViewInfo);
}

ImageView::ImageView(bool isDefault, ID<Image> image, Image::Type type,
	Image::Format format, uint32 baseLayer, uint32 layerCount, uint8 baseMip, uint8 mipCount)
{
	auto graphicsBackend = GraphicsAPI::get()->getBackendType();
	if (graphicsBackend == GraphicsBackend::VulkanAPI)
	{
		auto imageView = VulkanAPI::get()->imagePool.get(image);
		if (imageView->getType() == Image::Type::Cubemap && layerCount == 1)
		{
			GARDEN_ASSERT(baseLayer < Image::cubemapFaceCount);
			baseLayer ^= 1; // Note: remapping Vulkan API cubemap face indices.
		}

		this->instance = createVkImageView(imageView, type, format, 
			baseLayer, layerCount, baseMip, mipCount, apiFormat, aspectFlags);
	}
	else abort();

	this->image = image;
	this->baseLayer = baseLayer;
	this->layerCount = layerCount;
	this->baseMip = baseMip;
	this->mipCount = mipCount;
	this->type = type;
	this->format = format;
	this->_default = isDefault;
}
bool ImageView::destroy()
{
	if (!instance || busyLock > 0)
		return false;

	auto graphicsAPI = GraphicsAPI::get();

	#if GARDEN_DEBUG
	if (!graphicsAPI->forceResourceDestroy)
	{
		auto imageViewInstance = graphicsAPI->imageViewPool.getID(this);
		for (auto& descriptorSet : graphicsAPI->descriptorSetPool)
		{
			if (!ResourceExt::getInstance(descriptorSet))
				continue;

			const auto& descriptorUniforms = descriptorSet.getUniforms();
			auto pipelineView = graphicsAPI->getPipelineView(
				descriptorSet.getPipelineType(), descriptorSet.getPipeline());
			if (pipelineView->isBindless())
				continue;

			const auto& pipelineUniforms = pipelineView->getUniforms();
			for (const auto& pair : descriptorUniforms)
			{
				const auto uniformPair = pipelineUniforms.find(pair.first);
				if (uniformPair == pipelineUniforms.end() || (!uniformPair->second.isSamplerType && 
					!uniformPair->second.isImageType) || uniformPair->second.descriptorSetIndex != descriptorSet.getIndex())
				{
					continue;
				}

				const auto& resourceSets = pair.second.resourceSets;
				for (const auto& resourceArray : resourceSets)
				{
					for (auto resource : resourceArray)
					{
						GARDEN_ASSERT_MSG(imageViewInstance != ID<ImageView>(resource), 
							"Descriptor set [" + descriptorSet.getDebugName() + "] is "
							"still using destroyed image view [" + debugName + "]");
					}
				}
			}
		}

		for (auto& framebuffer : graphicsAPI->framebufferPool)
		{
			if (!framebuffer.isValid())
				continue;

			const auto& colorAttachments = framebuffer.getColorAttachments();
			for (const auto& attachment : colorAttachments)
			{
				GARDEN_ASSERT_MSG(imageViewInstance != attachment.imageView, 
					"Framebuffer [" + framebuffer.getDebugName() + "] is "
					"still using destroyed image view [" + debugName + "]");
			}

			GARDEN_ASSERT_MSG(imageViewInstance != framebuffer.getDepthStencilAttachment().imageView, 
				"Framebuffer [" + framebuffer.getDebugName() + "] is "
				"still using destroyed image view [" + debugName + "]");
		}
	}
	#endif

	auto graphicsBackend = graphicsAPI->getBackendType();
	if (graphicsBackend == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		if (vulkanAPI->forceResourceDestroy)
			vulkanAPI->device.destroyImageView((VkImageView)instance);
		else vulkanAPI->destroyResource(GraphicsAPI::DestroyResourceType::ImageView, instance);
	}
	else abort();

	return true;
}

uint2 ImageView::calcSize(uint8 mipOffset) const noexcept
{
	GARDEN_ASSERT(mipOffset < mipCount);
	auto imageView = GraphicsAPI::get()->imagePool.get(image);
	return calcSizeAtMip((uint2)imageView->getSize(), baseMip + mipOffset);
}
u32x4 ImageView::calcSize3(uint8 mipOffset) const noexcept
{
	GARDEN_ASSERT(mipOffset < mipCount);
	auto imageView = GraphicsAPI::get()->imagePool.get(image);
	return calcSizeAtMip3(imageView->getSize(), baseMip + mipOffset);
}

#if GARDEN_DEBUG || GARDEN_EDITOR
void ImageView::setDebugName(const string& name)
{
	Resource::setDebugName(name);

	auto graphicsBackend = GraphicsAPI::get()->getBackendType();
	if (graphicsBackend == GraphicsBackend::VulkanAPI)
	{
		#if GARDEN_DEBUG // Note: No GARDEN_EDITOR
		auto vulkanAPI = VulkanAPI::get();
		if (!vulkanAPI->features.debugUtils)
			return;

		vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eImageView, (uint64)instance, name.c_str());
		vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo);
		#endif
	}
	else abort();
}
#endif