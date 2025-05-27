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

#include "garden/graphics/image.hpp"
#include "garden/graphics/vulkan/api.hpp"

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
	if (hasAnyFlag(imageUsage, Image::Usage::InputAttachment))
		flags |= vk::ImageUsageFlagBits::eInputAttachment;
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
	Image::Strategy strategy, u32x4 size, void*& instance, void*& allocation)
{
	VkImageCreateInfo imageInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imageInfo.imageType = (VkImageType)toVkImageType(type);
	imageInfo.format = (VkFormat)toVkFormat(format);
	imageInfo.mipLevels = size.getW();
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = (VkImageUsageFlags)toVkImageUsages(usage);
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // TODO: multi queue
	imageInfo.queueFamilyIndexCount = 0;
	imageInfo.pQueueFamilyIndices = nullptr;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

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

	auto vulkanAPI = VulkanAPI::get();

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

	instance = vmaInstance;
	allocation = vmaAllocation;
}

//**********************************************************************************************************************
Image::Image(Type type, Format format, Usage usage, Strategy strategy, u32x4 size, uint64 version) :
	Memory(0, CpuAccess::None, Location::Auto, strategy, version), barrierStates(size.getZ() * size.getW())
{
	GARDEN_ASSERT(areAllTrue(size > u32x4::zero));

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		createVkImage(type, format, usage, strategy, size, instance, allocation);
	else abort();

	this->binarySize = 0;
	this->type = type;
	this->format = format;
	this->usage = usage;
	this->swapchain = false;
	this->size = size;

	auto mipSize = size;
	auto formatBinarySize = (uint64)toBinarySize(format);

	for (uint8 mip = 0, mipCount = getMipCount(); mip < mipCount; mip++)
	{
		this->binarySize += formatBinarySize * mipSize.getX() * mipSize.getY() * mipSize.getZ();
		mipSize = max(mipSize / 2u, u32x4::one);
	}
}

//**********************************************************************************************************************
Image::Image(void* instance, Format format, Usage usage, Strategy strategy, uint2 size, uint64 version) : 
	Memory(toBinarySize(format) * size.x * size.y, CpuAccess::None, Location::Auto, strategy, version), barrierStates(1)
{
	GARDEN_ASSERT(areAllTrue(size > uint2::zero));

	this->instance = instance;
	this->type = Image::Type::Texture2D;
	this->format = format;
	this->usage = usage;
	this->swapchain = true;
	this->size = u32x4(size.x, size.y, 1, 1);
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
			if (!ResourceExt::getInstance(imageView) || imageView.getImage() != imageInstance)
				continue;
			throw GardenError("Image view is still using destroyed image. (image: " +
				debugName + ", imageView: " + imageView.getDebugName() + ")");
		}
	}
	#endif

	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		if (!this->swapchain)
		{
			if (vulkanAPI->forceResourceDestroy)
				vmaDestroyImage(vulkanAPI->memoryAllocator, (VkImage)instance, (VmaAllocation)allocation);
			else
				vulkanAPI->destroyResource(GraphicsAPI::DestroyResourceType::Image, instance, allocation);
		}
	}
	else abort();

	return true;
}

//**********************************************************************************************************************
ID<ImageView> Image::getDefaultView()
{
	if (!defaultView)
	{
		GARDEN_ASSERT(instance); // is ready

		auto graphicsAPI = GraphicsAPI::get();
		auto image = graphicsAPI->imagePool.getID(this);
		defaultView = graphicsAPI->imageViewPool.create(true, image, 
			type, format, 0, getLayerCount(), 0, getMipCount());

		#if GARDEN_DEBUG || GARDEN_EDITOR
		auto view = graphicsAPI->imageViewPool.get(defaultView);
		view->setDebugName(debugName + ".defaultView");
		#endif
	}

	return defaultView;
}

bool Image::isSupported(Type type, Format format, Usage usage, uint3 size, uint8 mipCount, uint32 layerCount)
{
	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		vk::PhysicalDeviceImageFormatInfo2 imageFormatInfo;
		imageFormatInfo.format = toVkFormat(format);
		imageFormatInfo.type = toVkImageType(type);
		imageFormatInfo.tiling = vk::ImageTiling::eOptimal;
		imageFormatInfo.usage = toVkImageUsages(usage);
		if (type == Type::Cubemap)
			imageFormatInfo.flags |= vk::ImageCreateFlagBits::eCubeCompatible;

		auto imageFormatProperties = VulkanAPI::get()->physicalDevice.getImageFormatProperties2(imageFormatInfo);
		return size.x <= imageFormatProperties.imageFormatProperties.maxExtent.width &&
			size.y <= imageFormatProperties.imageFormatProperties.maxExtent.height &&
			size.z <= imageFormatProperties.imageFormatProperties.maxExtent.depth &&
			mipCount <= imageFormatProperties.imageFormatProperties.maxMipLevels &&
			layerCount <= imageFormatProperties.imageFormatProperties.maxArrayLayers;
	}
	else abort();
	
}

#if GARDEN_DEBUG || GARDEN_EDITOR
void Image::setDebugName(const string& name)
{
	Resource::setDebugName(name);

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		if (!vulkanAPI->hasDebugUtils || !instance)
			return;

		vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eImage, (uint64)instance, name.c_str());
		vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo);
	}
	else abort();
}
#endif

void Image::generateMips(Sampler::Filter filter)
{
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(!GraphicsAPI::get()->currentFramebuffer);

	auto image = GraphicsAPI::get()->imagePool.getID(this);
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
	GARDEN_ASSERT(regions);
	GARDEN_ASSERT(count > 0);
	GARDEN_ASSERT(!GraphicsAPI::get()->currentFramebuffer);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);
	GARDEN_ASSERT(isFormatFloat(format) || isFormatSrgb(format) || isFormatNorm(format));
	GARDEN_ASSERT(hasAnyFlag(usage, Usage::TransferDst));
	auto graphicsAPI = GraphicsAPI::get();

	ClearImageCommand command;
	command.clearType = 1;
	command.regionCount = count;
	command.image = graphicsAPI->imagePool.getID(this);
	command.color = color;
	command.regions = regions;
	graphicsAPI->currentCommandBuffer->addCommand(command);

	if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
	{
		busyLock++;
		graphicsAPI->currentCommandBuffer->addLockedResource(command.image);
	}
}
void Image::clear(int4 color, const ClearRegion* regions, uint32 count)
{
	GARDEN_ASSERT(regions);
	GARDEN_ASSERT(count > 0);
	GARDEN_ASSERT(!GraphicsAPI::get()->currentFramebuffer);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);
	GARDEN_ASSERT(isFormatSint(format));
	GARDEN_ASSERT(hasAnyFlag(usage, Usage::TransferDst));
	auto graphicsAPI = GraphicsAPI::get();

	ClearImageCommand command;
	command.clearType = 2;
	command.regionCount = count;
	command.image = graphicsAPI->imagePool.getID(this);
	*(int4*)&command.color = color;
	command.regions = regions;
	graphicsAPI->currentCommandBuffer->addCommand(command);

	if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
	{
		busyLock++;
		graphicsAPI->currentCommandBuffer->addLockedResource(command.image);
	}
}
void Image::clear(uint4 color, const ClearRegion* regions, uint32 count)
{
	GARDEN_ASSERT(regions);
	GARDEN_ASSERT(count > 0);
	GARDEN_ASSERT(!GraphicsAPI::get()->currentFramebuffer);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);
	GARDEN_ASSERT(isFormatUint(format));
	GARDEN_ASSERT(hasAnyFlag(usage, Usage::TransferDst));
	auto graphicsAPI = GraphicsAPI::get();

	ClearImageCommand command;
	command.clearType = 3;
	command.regionCount = count;
	command.image = graphicsAPI->imagePool.getID(this);
	*(uint4*)&command.color = color;
	command.regions = regions;
	graphicsAPI->currentCommandBuffer->addCommand(command);

	if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
	{
		busyLock++;
		graphicsAPI->currentCommandBuffer->addLockedResource(command.image);
	}
}
void Image::clear(float depth, uint32 stencil, const ClearRegion* regions, uint32 count)
{
	GARDEN_ASSERT(regions);
	GARDEN_ASSERT(count > 0);
	GARDEN_ASSERT(!GraphicsAPI::get()->currentFramebuffer);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);
	GARDEN_ASSERT(isFormatDepthOrStencil(format));
	GARDEN_ASSERT(hasAnyFlag(usage, Usage::TransferDst));
	auto graphicsAPI = GraphicsAPI::get();
	
	ClearImageCommand command;
	command.regionCount = count;
	command.image = graphicsAPI->imagePool.getID(this);
	command.color.x = depth;
	*(uint32*)&command.color.y = stencil;
	command.regions = regions;
	graphicsAPI->currentCommandBuffer->addCommand(command);

	if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
	{
		busyLock++;
		graphicsAPI->currentCommandBuffer->addLockedResource(command.image);
	}
}

//**********************************************************************************************************************
void Image::copy(ID<Image> source, ID<Image> destination, const CopyImageRegion* regions, uint32 count)
{
	GARDEN_ASSERT(source);
	GARDEN_ASSERT(destination);
	GARDEN_ASSERT(regions);
	GARDEN_ASSERT(count > 0);
	GARDEN_ASSERT(!GraphicsAPI::get()->currentFramebuffer);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);
	auto graphicsAPI = GraphicsAPI::get();

	auto srcView = graphicsAPI->imagePool.get(source);
	GARDEN_ASSERT(srcView->instance); // is ready
	GARDEN_ASSERT(hasAnyFlag(srcView->usage, Usage::TransferSrc));

	auto dstView = graphicsAPI->imagePool.get(destination);
	GARDEN_ASSERT(dstView->instance); // is ready
	GARDEN_ASSERT(hasAnyFlag(dstView->usage, Usage::TransferDst));
	GARDEN_ASSERT(toBinarySize(srcView->format) == toBinarySize(dstView->format));

	#if GARDEN_DEBUG
	for (uint32 i = 0; i < count; i++)
	{
		auto region = regions[i];
		GARDEN_ASSERT(region.srcBaseLayer + region.layerCount <= srcView->getLayerCount());
		GARDEN_ASSERT(region.dstBaseLayer + region.layerCount <= dstView->getLayerCount());
		GARDEN_ASSERT(region.srcMipLevel <= srcView->getMipCount());
		GARDEN_ASSERT(region.dstMipLevel <= dstView->getMipCount());
		GARDEN_ASSERT((region.extent == uint3::zero && region.srcOffset == uint3::zero) || region.extent != uint3::zero);

		if (region.extent == uint3::zero)
		{
			GARDEN_ASSERT(region.srcOffset == uint3::zero);
			GARDEN_ASSERT(region.dstOffset == uint3::zero);
		}
		else
		{
			auto mipImageSize = calcSizeAtMip3(srcView->size, region.srcMipLevel);
			GARDEN_ASSERT(areAllTrue(region.extent + region.srcOffset <= (uint3)mipImageSize));
			mipImageSize = calcSizeAtMip3(srcView->size, region.dstMipLevel);
			GARDEN_ASSERT(areAllTrue(region.extent + region.dstOffset <= (uint3)mipImageSize));
		}
	}
	#endif

	CopyImageCommand command;
	command.regionCount = count;
	command.source = source;
	command.destination = destination;
	command.regions = regions;
	graphicsAPI->currentCommandBuffer->addCommand(command);

	if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
	{
		srcView->busyLock++;
		dstView->busyLock++;
		graphicsAPI->currentCommandBuffer->addLockedResource(source);
		graphicsAPI->currentCommandBuffer->addLockedResource(destination);
	}
}

//**********************************************************************************************************************
void Image::copy(ID<Buffer> source, ID<Image> destination, const CopyBufferRegion* regions, uint32 count)
{
	GARDEN_ASSERT(source);
	GARDEN_ASSERT(destination);
	GARDEN_ASSERT(regions);
	GARDEN_ASSERT(count > 0);
	GARDEN_ASSERT(!GraphicsAPI::get()->currentFramebuffer);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);
	auto graphicsAPI = GraphicsAPI::get();

	auto bufferView = graphicsAPI->bufferPool.get(source);
	GARDEN_ASSERT(ResourceExt::getInstance(**bufferView)); // is ready
	GARDEN_ASSERT(hasAnyFlag(bufferView->getUsage(), Buffer::Usage::TransferSrc));
	
	auto imageView = graphicsAPI->imagePool.get(destination);
	GARDEN_ASSERT(imageView->instance); // is ready
	GARDEN_ASSERT(hasAnyFlag(imageView->getUsage(), Usage::TransferDst));
	
	#if GARDEN_DEBUG
	for (uint32 i = 0; i < count; i++)
	{
		auto region = regions[i];
		GARDEN_ASSERT(region.imageBaseLayer + region.imageLayerCount <= imageView->getLayerCount());
		GARDEN_ASSERT(region.imageMipLevel <= imageView->getMipCount());

		if (region.imageExtent == uint3::zero)
		{
			GARDEN_ASSERT(region.imageOffset == uint3::zero);
		}
		else
		{
			auto mipImageSize = calcSizeAtMip3(imageView->size, region.imageMipLevel);
			GARDEN_ASSERT(areAllTrue(region.imageExtent + region.imageOffset <= (uint3)mipImageSize));
		}

		// TODO: check out of buffer/image bounds.
	}
	#endif

	CopyBufferImageCommand command;
	command.toBuffer = false;
	command.regionCount = count;
	command.buffer = source;
	command.image = destination;
	command.regions = regions;
	graphicsAPI->currentCommandBuffer->addCommand(command);

	if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
	{
		ResourceExt::getBusyLock(**bufferView)++;
		ResourceExt::getBusyLock(**imageView)++;
		graphicsAPI->currentCommandBuffer->addLockedResource(source);
		graphicsAPI->currentCommandBuffer->addLockedResource(destination);
	}
}

//**********************************************************************************************************************
void Image::copy(ID<Image> source, ID<Buffer> destination, const CopyBufferRegion* regions, uint32 count)
{
	GARDEN_ASSERT(source);
	GARDEN_ASSERT(destination);
	GARDEN_ASSERT(regions);
	GARDEN_ASSERT(count > 0);
	GARDEN_ASSERT(!GraphicsAPI::get()->currentFramebuffer);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);
	auto graphicsAPI = GraphicsAPI::get();

	auto imageView = graphicsAPI->imagePool.get(source);
	GARDEN_ASSERT(imageView->instance); // is ready
	GARDEN_ASSERT(hasAnyFlag(imageView->getUsage(), Usage::TransferSrc));

	auto bufferView = graphicsAPI->bufferPool.get(destination);
	GARDEN_ASSERT(ResourceExt::getInstance(**bufferView)); // is ready
	GARDEN_ASSERT(hasAnyFlag(bufferView->getUsage(), Buffer::Usage::TransferDst));

	#if GARDEN_DEBUG
	for (uint32 i = 0; i < count; i++)
	{
		auto region = regions[i];
		GARDEN_ASSERT(region.imageBaseLayer + region.imageLayerCount <= imageView->getLayerCount());
		GARDEN_ASSERT(region.imageMipLevel <= imageView->getMipCount());

		if (region.imageExtent == uint3::zero)
		{
			GARDEN_ASSERT(region.imageOffset == uint3::zero);
		}
		else
		{
			auto mipImageSize = calcSizeAtMip3(imageView->size, region.imageMipLevel);
			GARDEN_ASSERT(areAllTrue(region.imageExtent + region.imageOffset <= (uint3)mipImageSize));
		}

		// TODO: check out of buffer/image bounds.
	}
	#endif

	CopyBufferImageCommand command;
	command.toBuffer = true;
	command.regionCount = count;
	command.buffer = destination;
	command.image = source;
	command.regions = regions;
	graphicsAPI->currentCommandBuffer->addCommand(command);

	if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
	{
		ResourceExt::getBusyLock(**imageView)++;
		ResourceExt::getBusyLock(**bufferView)++;
		graphicsAPI->currentCommandBuffer->addLockedResource(source);
		graphicsAPI->currentCommandBuffer->addLockedResource(destination);
	}
}

//**********************************************************************************************************************
void Image::blit(ID<Image> source, ID<Image> destination, 
	const BlitRegion* regions, uint32 count, Sampler::Filter filter)
{
	GARDEN_ASSERT(source);
	GARDEN_ASSERT(destination);
	GARDEN_ASSERT(regions);
	GARDEN_ASSERT(count > 0);
	GARDEN_ASSERT(!GraphicsAPI::get()->currentFramebuffer);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);
	auto graphicsAPI = GraphicsAPI::get();

	auto srcView = graphicsAPI->imagePool.get(source);
	GARDEN_ASSERT(srcView->instance); // is ready
	GARDEN_ASSERT(hasAnyFlag(srcView->usage, Usage::TransferSrc));

	auto dstView = graphicsAPI->imagePool.get(destination);
	GARDEN_ASSERT(dstView->instance); // is ready
	GARDEN_ASSERT(hasAnyFlag(dstView->usage, Usage::TransferDst));

	#if GARDEN_DEBUG
	for (uint32 i = 0; i < count; i++)
	{
		auto region = regions[i];
		GARDEN_ASSERT(region.srcBaseLayer + region.layerCount <= srcView->getLayerCount());
		GARDEN_ASSERT(region.dstBaseLayer + region.layerCount <= dstView->getLayerCount());
		GARDEN_ASSERT(region.srcMipLevel <= srcView->getMipCount());
		GARDEN_ASSERT(region.dstMipLevel <= dstView->getMipCount());

		if (region.srcExtent == uint3::zero)
		{
			GARDEN_ASSERT(region.srcOffset == uint3::zero);
		}
		else
		{
			auto mipImageSize = calcSizeAtMip3(srcView->size, region.srcMipLevel);
			GARDEN_ASSERT(areAllTrue(region.srcExtent + region.srcOffset <= (uint3)mipImageSize));
		}
		if (region.dstExtent == uint3::zero)
		{
			GARDEN_ASSERT(region.dstOffset == uint3::zero);
		}
		else
		{
			auto mipImageSize = calcSizeAtMip3(dstView->size, region.dstMipLevel);
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
	graphicsAPI->currentCommandBuffer->addCommand(command);

	if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
	{
		ResourceExt::getBusyLock(**srcView)++;
		ResourceExt::getBusyLock(**dstView)++;
		graphicsAPI->currentCommandBuffer->addLockedResource(source);
		graphicsAPI->currentCommandBuffer->addLockedResource(destination);
	}
}

//**********************************************************************************************************************
static void* createVkImageView(ID<Image> image, Image::Type type, Image::Format format, 
	uint8 baseMip, uint8 mipCount, uint32 baseLayer, uint32 layerCount)
{
	auto vulkanAPI = VulkanAPI::get();
	auto imageView = vulkanAPI->imagePool.get(image);
	vk::ImageViewCreateInfo imageViewInfo({}, (VkImage)ResourceExt::getInstance(**imageView),
		toVkImageViewType(type), toVkFormat(format), vk::ComponentMapping(
			vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity,
			vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity),
		// TODO: utilize component swizzle
		vk::ImageSubresourceRange(toVkImageAspectFlags(format),
			baseMip, mipCount, baseLayer, layerCount));
	return (VkImageView)vulkanAPI->device.createImageView(imageViewInfo);
}

//**********************************************************************************************************************
ImageView::ImageView(bool isDefault, ID<Image> image, Image::Type type,
	Image::Format format, uint32 baseLayer, uint32 layerCount, uint8 baseMip, uint8 mipCount)
{
	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		this->instance = createVkImageView(image, type, format, baseMip, mipCount, baseLayer, layerCount);
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

			const auto& uniforms = pipelineView->getUniforms();
			for (const auto& pair : descriptorUniforms)
			{
				const auto uniform = uniforms.find(pair.first);
				if (uniform == uniforms.end() || (!isSamplerType(uniform->second.type) && 
					!isImageType(uniform->second.type)) || uniform->second.descriptorSetIndex != descriptorSet.getIndex())
				{
					continue;
				}

				const auto& resourceSets = pair.second.resourceSets;
				for (const auto& resourceArray : resourceSets)
				{
					for (auto resource : resourceArray)
					{
						if (ID<ImageView>(resource) != imageViewInstance)
							continue;
						throw GardenError("Descriptor set is still using destroyed image view. (imageView: " +
							debugName + ", descriptorSet: " + descriptorSet.getDebugName() + ")");
					}
				}
			}
		}

		for (auto& framebuffer : graphicsAPI->framebufferPool)
		{
			if (!ResourceExt::getInstance(framebuffer))
				continue;

			const auto& colorAttachments = framebuffer.getColorAttachments();
			for (const auto& attachment : colorAttachments)
			{
				if (attachment.imageView != imageViewInstance)
					continue;
				throw GardenError("Framebuffer is still using destroyed image view. (imageView: " +
					debugName + ", framebuffer: " + framebuffer.getDebugName() + ")");
			}

			if (framebuffer.getDepthStencilAttachment().imageView == imageViewInstance)
			{
				throw GardenError("Framebuffer is still using destroyed image view. (imageView: " +
					debugName + ", framebuffer: " + framebuffer.getDebugName() + ")");
			}
		}
	}
	#endif

	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		if (vulkanAPI->forceResourceDestroy)
			vulkanAPI->device.destroyImageView((VkImageView)instance);
		else
			vulkanAPI->destroyResource(GraphicsAPI::DestroyResourceType::ImageView, instance);
	}
	else abort();

	return true;
}

#if GARDEN_DEBUG || GARDEN_EDITOR
void ImageView::setDebugName(const string& name)
{
	Resource::setDebugName(name);

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		if (!vulkanAPI->hasDebugUtils)
			return;

		vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eImageView, (uint64)instance, name.c_str());
		vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo);
	}
	else abort();
}
#endif