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

#include "garden/graphics/image.hpp"
#include "garden/graphics/vulkan/api.hpp"

using namespace math;
using namespace garden;
using namespace garden::graphics;

//**********************************************************************************************************************
static vk::ImageType toVkImageType(Image::Type imageType)
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
static vk::ImageViewType toVkImageViewType(Image::Type imageType)
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
static vk::ImageUsageFlags toVkImageUsages(Image::Bind imageBind)
{
	vk::ImageUsageFlags flags;
	if (hasAnyFlag(imageBind, Image::Bind::TransferSrc))
		flags |= vk::ImageUsageFlagBits::eTransferSrc;
	if (hasAnyFlag(imageBind, Image::Bind::TransferDst))
		flags |= vk::ImageUsageFlagBits::eTransferDst;
	if (hasAnyFlag(imageBind, Image::Bind::Sampled))
		flags |= vk::ImageUsageFlagBits::eSampled;
	if (hasAnyFlag(imageBind, Image::Bind::Storage))
		flags |= vk::ImageUsageFlagBits::eStorage;
	if (hasAnyFlag(imageBind, Image::Bind::ColorAttachment))
		flags |= vk::ImageUsageFlagBits::eColorAttachment;
	if (hasAnyFlag(imageBind, Image::Bind::DepthStencilAttachment))
		flags |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
	if (hasAnyFlag(imageBind, Image::Bind::InputAttachment))
		flags |= vk::ImageUsageFlagBits::eInputAttachment;
	return flags;
}
static VmaAllocationCreateFlagBits toVmaMemoryStrategy(Image::Strategy memoryUsage)
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
static void createVkImage(Image::Type type, Image::Format format, Image::Bind bind, Image::Strategy strategy,
	const uint3& size, uint8 mipCount, uint32 layerCount, void*& instance, void*& allocation)
{
	VkImageCreateInfo imageInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imageInfo.imageType = (VkImageType)toVkImageType(type);
	imageInfo.format = (VkFormat)toVkFormat(format);
	imageInfo.extent = { size.x, size.y, size.z };
	imageInfo.mipLevels = mipCount;
	imageInfo.arrayLayers = layerCount;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = (VkImageUsageFlags)toVkImageUsages(bind);
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // TODO: multi queue
	imageInfo.queueFamilyIndexCount = 0;
	imageInfo.pQueueFamilyIndices = nullptr;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

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
	GARDEN_ASSERT(size.x <= imageFormatProperties.imageFormatProperties.maxExtent.width);
	GARDEN_ASSERT(size.y <= imageFormatProperties.imageFormatProperties.maxExtent.height);
	GARDEN_ASSERT(size.z <= imageFormatProperties.imageFormatProperties.maxExtent.depth);
	GARDEN_ASSERT(mipCount <= imageFormatProperties.imageFormatProperties.maxMipLevels);
	GARDEN_ASSERT(layerCount <= imageFormatProperties.imageFormatProperties.maxArrayLayers);
	GARDEN_ASSERT(imageInfo.samples <= (VkSampleCountFlags)imageFormatProperties.imageFormatProperties.sampleCounts);
	#endif
 
	VmaAllocationCreateInfo allocationCreateInfo = {};
	allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
	allocationCreateInfo.flags = toVmaMemoryStrategy(strategy);

	if (hasAnyFlag(bind, Image::Bind::Fullscreen))
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

	instance = vmaInstance;
	allocation = vmaAllocation;
}

//**********************************************************************************************************************
Image::Image(Type type, Format format, Bind bind, Strategy strategy,
	const uint3& size, uint8 mipCount, uint32 layerCount, uint64 version) :
	Memory(0, Access::None, Usage::Auto, strategy, version), layouts(mipCount * layerCount)
{
	GARDEN_ASSERT(size > 0u);
	GARDEN_ASSERT(mipCount > 0);
	GARDEN_ASSERT(layerCount > 0);

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		createVkImage(type, format, bind, strategy, size, mipCount, layerCount, instance, allocation);
	else abort();

	this->binarySize = 0;
	this->type = type;
	this->format = format;
	this->bind = bind;
	this->swapchain = false;
	this->mipCount = mipCount;
	this->size = size;
	this->layerCount = layerCount;

	auto mipSize = size;
	auto formatBinarySize = (uint64)toBinarySize(format);

	for (uint8 i = 0; i < mipCount; i++)
	{
		this->binarySize += formatBinarySize *
			mipSize.x * mipSize.y * mipSize.z * layerCount;
		mipSize = max(mipSize / 2u, uint3(1));
	}

	for (auto& layout : this->layouts)
		layout = (uint32)vk::ImageLayout::eUndefined;
}

//**********************************************************************************************************************
Image::Image(void* instance, Format format, Bind bind, Strategy strategy, uint2 size, uint64 version) : 
	Memory(toBinarySize(format) * size.x * size.y, Access::None, Usage::Auto, strategy, version), layouts(1)
{
	GARDEN_ASSERT(size > 0u);

	this->instance = instance;
	this->type = Image::Type::Texture2D;
	this->format = format;
	this->bind = bind;
	this->mipCount = 1;
	this->swapchain = true;
	this->size = uint3(size, 1);
	this->layerCount = 1;
	this->layouts[0] = (uint32)vk::ImageLayout::eUndefined;
}
bool Image::destroy()
{
	if (!instance || readyLock > 0)
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

	instance = nullptr;
	return true;
}

//**********************************************************************************************************************
ID<ImageView> Image::getDefaultView()
{
	if (!defaultView)
	{
		GARDEN_ASSERT(instance); // is ready

		auto graphicsAPI = GraphicsAPI::get();
		defaultView = graphicsAPI->imageViewPool.create(true,
			graphicsAPI->imagePool.getID(this), type, format, 0, mipCount, 0, layerCount);

		#if GARDEN_DEBUG || GARDEN_EDITOR
		auto view = graphicsAPI->imageViewPool.get(defaultView);
		view->setDebugName(debugName + ".defaultView");
		#endif
	}

	return defaultView;
}

bool Image::isSupported(Type type, Format format, Bind bind, const uint3& size, uint8 mipCount, uint32 layerCount)
{
	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		vk::PhysicalDeviceImageFormatInfo2 imageFormatInfo;
		imageFormatInfo.format = toVkFormat(format);
		imageFormatInfo.type = toVkImageType(type);
		imageFormatInfo.tiling = vk::ImageTiling::eOptimal;
		imageFormatInfo.usage = toVkImageUsages(bind);
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

#if GARDEN_DEBUG
void Image::setDebugName(const string& name)
{
	Resource::setDebugName(name);

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		if (!vulkanAPI->hasDebugUtils || !instance)
			return;

		vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eImage, (uint64)instance, name.c_str());
		vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo, vulkanAPI->dynamicLoader);
	}
	else abort();
}
#endif

void Image::generateMips(SamplerFilter filter)
{
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(!GraphicsAPI::get()->currentFramebuffer);

	auto image = GraphicsAPI::get()->imagePool.getID(this);
	auto mipSize = size;

	for (uint8 i = 1; i < mipCount; i++)
	{
		Image::BlitRegion region;
		region.srcExtent = mipSize;
		mipSize = max(mipSize / 2u, uint3(1));
		region.dstExtent = mipSize;
		region.layerCount = layerCount;
		region.srcMipLevel = i - 1;
		region.dstMipLevel = i;
		Image::blit(image, image, region, filter);
	}
}

//**********************************************************************************************************************
void Image::clear(const float4& color, const ClearRegion* regions, uint32 count)
{
	GARDEN_ASSERT(regions);
	GARDEN_ASSERT(count > 0);
	GARDEN_ASSERT(!GraphicsAPI::get()->currentFramebuffer);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);
	GARDEN_ASSERT(isFormatFloat(format));
	GARDEN_ASSERT(hasAnyFlag(bind, Bind::TransferDst));
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
		readyLock++;
		graphicsAPI->currentCommandBuffer->addLockResource(command.image);
	}
}
void Image::clear(const int4& color, const ClearRegion* regions, uint32 count)
{
	GARDEN_ASSERT(regions);
	GARDEN_ASSERT(count > 0);
	GARDEN_ASSERT(!GraphicsAPI::get()->currentFramebuffer);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);
	GARDEN_ASSERT(isFormatInt(format));
	GARDEN_ASSERT(hasAnyFlag(bind, Bind::TransferDst));
	auto graphicsAPI = GraphicsAPI::get();

	ClearImageCommand command;
	command.clearType = 2;
	command.regionCount = count;
	command.image = graphicsAPI->imagePool.getID(this);
	memcpy(&command.color, &color, sizeof(float4));
	command.regions = regions;
	graphicsAPI->currentCommandBuffer->addCommand(command);

	if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
	{
		readyLock++;
		graphicsAPI->currentCommandBuffer->addLockResource(command.image);
	}
}
void Image::clear(const uint4& color, const ClearRegion* regions, uint32 count)
{
	GARDEN_ASSERT(regions);
	GARDEN_ASSERT(count > 0);
	GARDEN_ASSERT(!GraphicsAPI::get()->currentFramebuffer);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);
	GARDEN_ASSERT(isFormatInt(format));
	GARDEN_ASSERT(hasAnyFlag(bind, Bind::TransferDst));
	auto graphicsAPI = GraphicsAPI::get();

	ClearImageCommand command;
	command.clearType = 3;
	command.regionCount = count;
	command.image = graphicsAPI->imagePool.getID(this);
	memcpy(&command.color, &color, sizeof(float4));
	command.regions = regions;
	graphicsAPI->currentCommandBuffer->addCommand(command);

	if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
	{
		readyLock++;
		graphicsAPI->currentCommandBuffer->addLockResource(command.image);
	}
}
void Image::clear(float depth, uint32 stencil, const ClearRegion* regions, uint32 count)
{
	GARDEN_ASSERT(regions);
	GARDEN_ASSERT(count > 0);
	GARDEN_ASSERT(!GraphicsAPI::get()->currentFramebuffer);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);
	GARDEN_ASSERT(isFormatDepthOrStencil(format));
	GARDEN_ASSERT(hasAnyFlag(bind, Bind::TransferDst));
	auto graphicsAPI = GraphicsAPI::get();
	
	ClearImageCommand command;
	command.regionCount = count;
	command.image = graphicsAPI->imagePool.getID(this);
	command.color.x = depth;
	memcpy(&command.color.y, &stencil, sizeof(uint32));
	command.regions = regions;
	graphicsAPI->currentCommandBuffer->addCommand(command);

	if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
	{
		readyLock++;
		graphicsAPI->currentCommandBuffer->addLockResource(command.image);
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
	GARDEN_ASSERT(hasAnyFlag(srcView->bind, Bind::TransferSrc));

	auto dstView = graphicsAPI->imagePool.get(destination);
	GARDEN_ASSERT(dstView->instance); // is ready
	GARDEN_ASSERT(hasAnyFlag(dstView->bind, Bind::TransferDst));
	GARDEN_ASSERT(toBinarySize(srcView->format) == toBinarySize(dstView->format));

	#if GARDEN_DEBUG
	for (uint32 i = 0; i < count; i++)
	{
		auto region = regions[i];
		GARDEN_ASSERT(region.srcBaseLayer + region.layerCount <= srcView->layerCount);
		GARDEN_ASSERT(region.dstBaseLayer + region.layerCount <= dstView->layerCount);
		GARDEN_ASSERT(region.srcMipLevel <= srcView->mipCount);
		GARDEN_ASSERT(region.dstMipLevel <= dstView->mipCount);
		GARDEN_ASSERT((region.extent == 0u && region.srcOffset == 0u) || region.extent != 0u);

		if (region.extent == 0u)
		{
			GARDEN_ASSERT(region.srcOffset == 0u);
			GARDEN_ASSERT(region.dstOffset == 0u);
		}
		else
		{
			auto mipImageSize = calcSizeAtMip(srcView->size, region.srcMipLevel);
			GARDEN_ASSERT(region.extent + region.srcOffset <= mipImageSize);
			mipImageSize = calcSizeAtMip(srcView->size, region.dstMipLevel);
			GARDEN_ASSERT(region.extent + region.dstOffset <= mipImageSize);
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
		srcView->readyLock++;
		dstView->readyLock++;
		graphicsAPI->currentCommandBuffer->addLockResource(source);
		graphicsAPI->currentCommandBuffer->addLockResource(destination);
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
	GARDEN_ASSERT(hasAnyFlag(bufferView->getBind(), Buffer::Bind::TransferSrc));
	
	auto imageView = graphicsAPI->imagePool.get(destination);
	GARDEN_ASSERT(imageView->instance); // is ready
	GARDEN_ASSERT(hasAnyFlag(imageView->getBind(), Bind::TransferDst));
	
	#if GARDEN_DEBUG
	for (uint32 i = 0; i < count; i++)
	{
		auto region = regions[i];
		GARDEN_ASSERT(region.imageBaseLayer + region.imageLayerCount <= imageView->layerCount);
		GARDEN_ASSERT(region.imageMipLevel <= imageView->mipCount);

		if (region.imageExtent == 0u)
		{
			GARDEN_ASSERT(region.imageOffset == 0u);
		}
		else
		{
			auto mipImageSize = calcSizeAtMip(imageView->size, region.imageMipLevel);
			GARDEN_ASSERT(region.imageExtent + region.imageOffset <= mipImageSize);
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
		ResourceExt::getReadyLock(**bufferView)++;
		ResourceExt::getReadyLock(**imageView)++;
		graphicsAPI->currentCommandBuffer->addLockResource(source);
		graphicsAPI->currentCommandBuffer->addLockResource(destination);
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
	GARDEN_ASSERT(hasAnyFlag(imageView->getBind(), Bind::TransferSrc));

	auto bufferView = graphicsAPI->bufferPool.get(destination);
	GARDEN_ASSERT(ResourceExt::getInstance(**bufferView)); // is ready
	GARDEN_ASSERT(hasAnyFlag(bufferView->getBind(), Buffer::Bind::TransferDst));

	#if GARDEN_DEBUG
	for (uint32 i = 0; i < count; i++)
	{
		auto region = regions[i];
		GARDEN_ASSERT(region.imageBaseLayer + region.imageLayerCount <= imageView->layerCount);
		GARDEN_ASSERT(region.imageMipLevel <= imageView->mipCount);

		if (region.imageExtent == 0u)
		{
			GARDEN_ASSERT(region.imageOffset == 0u);
		}
		else
		{
			auto mipImageSize = calcSizeAtMip(imageView->size, region.imageMipLevel);
			GARDEN_ASSERT(region.imageExtent + region.imageOffset <= mipImageSize);
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
		ResourceExt::getReadyLock(**imageView)++;
		ResourceExt::getReadyLock(**bufferView)++;
		graphicsAPI->currentCommandBuffer->addLockResource(source);
		graphicsAPI->currentCommandBuffer->addLockResource(destination);
	}
}

//**********************************************************************************************************************
void Image::blit(ID<Image> source, ID<Image> destination, const BlitRegion* regions, uint32 count, SamplerFilter filter)
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
	GARDEN_ASSERT(hasAnyFlag(srcView->bind, Bind::TransferSrc));

	auto dstView = graphicsAPI->imagePool.get(destination);
	GARDEN_ASSERT(dstView->instance); // is ready
	GARDEN_ASSERT(hasAnyFlag(dstView->bind, Bind::TransferDst));

	#if GARDEN_DEBUG
	for (uint32 i = 0; i < count; i++)
	{
		auto region = regions[i];
		GARDEN_ASSERT(region.srcBaseLayer + region.layerCount <= srcView->layerCount);
		GARDEN_ASSERT(region.dstBaseLayer + region.layerCount <= dstView->layerCount);
		GARDEN_ASSERT(region.srcMipLevel <= srcView->mipCount);
		GARDEN_ASSERT(region.dstMipLevel <= dstView->mipCount);

		if (region.srcExtent == 0u)
		{
			GARDEN_ASSERT(region.srcOffset == 0u);
		}
		else
		{
			auto mipImageSize = calcSizeAtMip(srcView->size, region.srcMipLevel);
			GARDEN_ASSERT(region.srcExtent + region.srcOffset <= mipImageSize);
		}
		if (region.dstExtent == 0u)
		{
			GARDEN_ASSERT(region.dstOffset == 0u);
		}
		else
		{
			auto mipImageSize = calcSizeAtMip(dstView->size, region.dstMipLevel);
			GARDEN_ASSERT(region.dstExtent + region.dstOffset <= mipImageSize);
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
		ResourceExt::getReadyLock(**srcView)++;
		ResourceExt::getReadyLock(**dstView)++;
		graphicsAPI->currentCommandBuffer->addLockResource(source);
		graphicsAPI->currentCommandBuffer->addLockResource(destination);
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
	Image::Format format, uint8 baseMip, uint8 mipCount, uint32 baseLayer, uint32 layerCount)
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
	if (!instance || readyLock > 0)
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

	instance = nullptr;
	return true;
}

#if GARDEN_DEBUG
void ImageView::setDebugName(const string& name)
{
	Resource::setDebugName(name);

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		if (!vulkanAPI->hasDebugUtils)
			return;

		vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eImageView, (uint64)instance, name.c_str());
		vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo, vulkanAPI->dynamicLoader);
	}
	else abort();
}
#endif