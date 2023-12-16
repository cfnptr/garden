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

#include "garden/graphics/buffer.hpp"
#include "garden/graphics/vulkan.hpp"

using namespace std;
using namespace garden::graphics;

static vk::BufferUsageFlags toVkBufferUsages(Buffer::Bind bufferBind)
{
	vk::BufferUsageFlags flags;
	if (hasAnyFlag(bufferBind, Buffer::Bind::TransferSrc))
		flags |= vk::BufferUsageFlagBits::eTransferSrc;
	if (hasAnyFlag(bufferBind, Buffer::Bind::TransferDst))
		flags |= vk::BufferUsageFlagBits::eTransferDst;
	if (hasAnyFlag(bufferBind, Buffer::Bind::Vertex))
		flags |= vk::BufferUsageFlagBits::eVertexBuffer;
	if (hasAnyFlag(bufferBind, Buffer::Bind::Index))
		flags |= vk::BufferUsageFlagBits::eIndexBuffer;
	if (hasAnyFlag(bufferBind, Buffer::Bind::Uniform))
		flags |= vk::BufferUsageFlagBits::eUniformBuffer;
	if (hasAnyFlag(bufferBind, Buffer::Bind::Storage))
		flags |= vk::BufferUsageFlagBits::eStorageBuffer;
	return flags;
}
static VmaAllocationCreateFlagBits toVmaMemoryAccess(Buffer::Access memoryAccess)
{
	switch (memoryAccess)
	{
	case Buffer::Access::None: return {};
	case Buffer::Access::SequentialWrite:
		return VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
	case Buffer::Access::RandomReadWrite:
		return VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
	// TODO: support VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT
	default: abort();
	}
}
static VmaMemoryUsage toVmaMemoryUsage(Buffer::Usage memoryUsage)
{
	switch (memoryUsage)
	{
	case Buffer::Usage::Auto: return VMA_MEMORY_USAGE_AUTO;
	case Buffer::Usage::PreferGPU: return VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
	case Buffer::Usage::PreferCPU: return VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
	default: abort();
	}
}
static VmaAllocationCreateFlagBits toVmaMemoryStrategy(Buffer::Strategy memoryUsage)
{
	switch (memoryUsage)
	{
	case Buffer::Strategy::Default: return {};
	case Buffer::Strategy::Size: return VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT;
	case Buffer::Strategy::Speed: return VMA_ALLOCATION_CREATE_STRATEGY_MIN_TIME_BIT;
	default: abort();
	}
}

//--------------------------------------------------------------------------------------------------
Buffer::Buffer(Bind bind, Access access, Usage usage,
	Strategy strategy, uint64 size, uint64 version) :
	Memory(size, access, usage, strategy, version)
{
	GARDEN_ASSERT(size > 0);

	VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.size = size;
	bufferInfo.usage = (VkBufferUsageFlags)toVkBufferUsages(bind);
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	bufferInfo.queueFamilyIndexCount = 0;
	bufferInfo.pQueueFamilyIndices = nullptr;

	VmaAllocationCreateInfo allocationCreateInfo = {};
	allocationCreateInfo.flags = toVmaMemoryAccess(access) | toVmaMemoryStrategy(strategy);
	allocationCreateInfo.usage = toVmaMemoryUsage(usage);
	allocationCreateInfo.priority = 0.5f; // TODO: expose this RAM offload priority?
	if (access != Access::None) allocationCreateInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;

	// TODO: VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT?

	VkBuffer instance; VmaAllocation allocation;
	auto result = vmaCreateBuffer(Vulkan::memoryAllocator,
		&bufferInfo, &allocationCreateInfo, &instance, &allocation, nullptr);
	if (result != VK_SUCCESS) throw runtime_error("Failed to allocate GPU buffer.");
	this->instance = instance; this->allocation = allocation;

	VmaAllocationInfo allocationInfo = {};
	vmaGetAllocationInfo(Vulkan::memoryAllocator, allocation, &allocationInfo);

	if (access != Access::None && !allocationInfo.pMappedData)
		throw runtime_error("Failed to map GPU memory.");

	this->map = (uint8*)allocationInfo.pMappedData;
	this->bind = bind;
}

//--------------------------------------------------------------------------------------------------
bool Buffer::destroy()
{
	if (!instance || readyLock > 0) return false;

	if (GraphicsAPI::isRunning)
	{
		GraphicsAPI::destroyResource(GraphicsAPI::DestroyResourceType::Buffer,
			instance, allocation);
	}
	else
	{
		vmaDestroyBuffer(Vulkan::memoryAllocator,
			(VkBuffer)instance, (VmaAllocation)allocation);
	}

	instance = nullptr;
	return true;
}

//--------------------------------------------------------------------------------------------------
bool Buffer::isMappable() const
{
	if (map) return true;
	VkMemoryPropertyFlags memoryPropertyFlags;
	vmaGetAllocationMemoryProperties(Vulkan::memoryAllocator,
		(VmaAllocation)allocation, &memoryPropertyFlags);
	return memoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
}
void Buffer::invalidate(uint64 size, uint64 offset)
{
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT((size == 0 && offset == 0) || size + offset <= binarySize);
	GARDEN_ASSERT(isMappable());
	auto result = vmaInvalidateAllocation(Vulkan::memoryAllocator,
		(VmaAllocation)allocation, offset, size == 0 ? this->binarySize : size);
	if (result != VK_SUCCESS) throw runtime_error("Failed to invalidate GPU memory.");
}
void Buffer::flush(uint64 size, uint64 offset)
{
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT((size == 0 && offset == 0) || size + offset <= binarySize);
	GARDEN_ASSERT(isMappable());
	auto result = vmaFlushAllocation(Vulkan::memoryAllocator,
		(VmaAllocation)allocation, offset, size == 0 ? this->binarySize : size);
	if (result != VK_SUCCESS) throw runtime_error("Failed to flush GPU memory.");
}
void Buffer::setData(const void* data, uint64 size, uint64 offset)
{
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(data);
	GARDEN_ASSERT((size == 0 && offset == 0) || size + offset <= binarySize);
	GARDEN_ASSERT(isMappable());

	if (map)
	{
		memcpy(map + offset, data, size == 0 ? this->binarySize : size);
		flush(size, offset);
	}
	else
	{
		uint8* map;
		auto result = vmaMapMemory(Vulkan::memoryAllocator,
			(VmaAllocation)allocation, (void**)&map);
		if (result != VK_SUCCESS) throw runtime_error("Failed to map GPU memory.");
		memcpy(map + offset, data, size == 0 ? this->binarySize : size);
		flush(size, offset);
		vmaUnmapMemory(Vulkan::memoryAllocator, (VmaAllocation)allocation);
	}
}

#if GARDEN_DEBUG
//--------------------------------------------------------------------------------------------------
void Buffer::setDebugName(const string& name)
{
	Resource::setDebugName(name);
	if (!Vulkan::hasDebugUtils || !instance) return;
	vk::DebugUtilsObjectNameInfoEXT nameInfo(
		vk::ObjectType::eBuffer, (uint64)instance, name.c_str());
	Vulkan::device.setDebugUtilsObjectNameEXT(nameInfo, Vulkan::dynamicLoader);
}
#endif

//--------------------------------------------------------------------------------------------------
void Buffer::fill(uint32 data, uint64 size, uint64 offset)
{
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(size == 0 || size % 4 == 0);
	GARDEN_ASSERT(size == 0 || size + offset <= binarySize);
	GARDEN_ASSERT(hasAnyFlag(bind, Bind::TransferDst));
	GARDEN_ASSERT(!Framebuffer::getCurrent());
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);

	FillBufferCommand command;
	command.buffer = GraphicsAPI::bufferPool.getID(this);
	command.data = data;
	command.size = size;
	command.offset = offset;
	GraphicsAPI::currentCommandBuffer->addCommand(command);

	if (GraphicsAPI::currentCommandBuffer != &GraphicsAPI::frameCommandBuffer)
	{ readyLock++; GraphicsAPI::currentCommandBuffer->addLockResource(command.buffer); }
}

//--------------------------------------------------------------------------------------------------
void Buffer::copy(ID<Buffer> source, ID<Buffer> destination,
	const CopyRegion* regions, uint32 count)
{
	GARDEN_ASSERT(source);
	GARDEN_ASSERT(destination);
	GARDEN_ASSERT(regions);
	GARDEN_ASSERT(count > 0);
	GARDEN_ASSERT(!Framebuffer::getCurrent());
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);

	auto srcView = GraphicsAPI::bufferPool.get(source);
	GARDEN_ASSERT(srcView->instance); // is ready
	GARDEN_ASSERT(hasAnyFlag(srcView->bind, Bind::TransferSrc));

	auto dstView = GraphicsAPI::bufferPool.get(destination);
	GARDEN_ASSERT(dstView->instance); // is ready
	GARDEN_ASSERT(hasAnyFlag(dstView->bind, Bind::TransferDst));
	
	#if GARDEN_DEBUG
	for (uint32 i = 0; i < count; i++)
	{
		auto region = regions[i];

		GARDEN_ASSERT(region.size != 0 || (region.size == 0 &&
			region.srcOffset == 0 && region.dstOffset == 0));
		
		if (region.size == 0)
		{
			GARDEN_ASSERT(srcView->binarySize <= dstView->binarySize);
		}
		else
		{
			GARDEN_ASSERT(region.size + region.srcOffset <= srcView->binarySize);
			GARDEN_ASSERT(region.size + region.dstOffset <= dstView->binarySize);
		}
	}
	#endif

	CopyBufferCommand command;
	command.regionCount = count;
	command.source = source;
	command.destination = destination;
	command.regions = regions;
	GraphicsAPI::currentCommandBuffer->addCommand(command);

	if (GraphicsAPI::currentCommandBuffer != &GraphicsAPI::frameCommandBuffer)
	{
		srcView->readyLock++; dstView->readyLock++;
		GraphicsAPI::currentCommandBuffer->addLockResource(source);
		GraphicsAPI::currentCommandBuffer->addLockResource(destination);
	}
}