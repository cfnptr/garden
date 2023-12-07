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

//--------------------------------------------------------------------------------------------------
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

//--------------------------------------------------------------------------------------------------
static VmaMemoryUsage toVmaMemoryUsage(Memory::Usage memoryUsage)
{
	switch (memoryUsage)
	{
	case Memory::Usage::GpuOnly: return VMA_MEMORY_USAGE_GPU_ONLY;
	case Memory::Usage::CpuOnly: return VMA_MEMORY_USAGE_CPU_ONLY;
	case Memory::Usage::CpuToGpu: return VMA_MEMORY_USAGE_CPU_TO_GPU;
	case Memory::Usage::GpuToCpu: return VMA_MEMORY_USAGE_GPU_TO_CPU;
	default: abort();
	}
}

//--------------------------------------------------------------------------------------------------
Buffer::Buffer(Bind bind, Usage usage, uint64 size, uint64 version) :
	Memory(size, usage, version)
{
	GARDEN_ASSERT(size > 0);

	VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.size = size;
	bufferInfo.usage = (VkBufferUsageFlags)toVkBufferUsages(bind);
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	bufferInfo.queueFamilyIndexCount = 0;
	bufferInfo.pQueueFamilyIndices = nullptr;
 
	VmaAllocationCreateInfo allocationCreateInfo = {};
	allocationCreateInfo.usage = toVmaMemoryUsage(usage);

	auto isMappable = usage == Usage::CpuOnly ||
		usage == Usage::CpuToGpu || usage == Usage::GpuToCpu;
	if (isMappable) allocationCreateInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VkBuffer instance; VmaAllocation allocation;
	auto result = vmaCreateBuffer(Vulkan::memoryAllocator,
		&bufferInfo, &allocationCreateInfo, &instance, &allocation, nullptr);
	if (result != VK_SUCCESS) throw runtime_error("Failed to allocate GPU buffer.");
	this->instance = instance; this->allocation = allocation;

	VmaAllocationInfo allocationInfo = {};
	vmaGetAllocationInfo(Vulkan::memoryAllocator, allocation, &allocationInfo);

	if (isMappable)
	{
		if (!allocationInfo.pMappedData)
			throw runtime_error("Failed to map GPU memory.");
		this->map = (uint8*)allocationInfo.pMappedData;
	}

	this->bind = bind;
}

//--------------------------------------------------------------------------------------------------
bool Buffer::destroy()
{
	if (isBusy()) return false;

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
void Buffer::invalidate(uint64 size, uint64 offset)
{
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT((size == 0 && offset == 0) || size + offset <= binarySize);
	GARDEN_ASSERT(usage == Usage::GpuToCpu);
	auto result = vmaInvalidateAllocation(Vulkan::memoryAllocator,
		(VmaAllocation)allocation, offset, size == 0 ? this->binarySize : size);
	if (result != VK_SUCCESS) throw runtime_error("Failed to invalidate GPU memory.");
}
void Buffer::flush(uint64 size, uint64 offset)
{
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT((size == 0 && offset == 0) || size + offset <= binarySize);
	GARDEN_ASSERT(usage == Usage::CpuOnly || usage == Usage::CpuToGpu);
	auto result = vmaFlushAllocation(Vulkan::memoryAllocator,
		(VmaAllocation)allocation, offset, size == 0 ? this->binarySize : size);
	if (result != VK_SUCCESS) throw runtime_error("Failed to flush GPU memory.");
}
void Buffer::setData(const void* data, uint64 size, uint64 offset)
{
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(data);
	GARDEN_ASSERT((size == 0 && offset == 0) || size + offset <= binarySize);
	GARDEN_ASSERT(usage == Usage::CpuOnly || usage == Usage::CpuToGpu);
	memcpy(map + offset, data, size == 0 ? this->binarySize : size);
	flush(size, offset);
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

	if (GraphicsAPI::currentCommandBuffer == &GraphicsAPI::graphicsCommandBuffer)
		lastGraphicsTime = GraphicsAPI::graphicsCommandBuffer.getBusyTime();
	else if (GraphicsAPI::currentCommandBuffer == &GraphicsAPI::transferCommandBuffer)
		lastTransferTime = GraphicsAPI::transferCommandBuffer.getBusyTime();
	else if (GraphicsAPI::currentCommandBuffer == &GraphicsAPI::computeCommandBuffer)
		lastComputeTime = GraphicsAPI::computeCommandBuffer.getBusyTime();
	else lastFrameTime = GraphicsAPI::frameCommandBuffer.getBusyTime();
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

	if (GraphicsAPI::currentCommandBuffer == &GraphicsAPI::graphicsCommandBuffer)
	{
		srcView->lastGraphicsTime = dstView->lastGraphicsTime =
			GraphicsAPI::graphicsCommandBuffer.getBusyTime();
	}
	else if (GraphicsAPI::currentCommandBuffer == &GraphicsAPI::transferCommandBuffer)
	{
		srcView->lastTransferTime = dstView->lastTransferTime =
			GraphicsAPI::transferCommandBuffer.getBusyTime();
	}
	else if (GraphicsAPI::currentCommandBuffer == &GraphicsAPI::computeCommandBuffer)
	{
		srcView->lastComputeTime = dstView->lastComputeTime =
			GraphicsAPI::computeCommandBuffer.getBusyTime();
	}
	else
	{
		srcView->lastFrameTime = dstView->lastFrameTime =
			GraphicsAPI::frameCommandBuffer.getBusyTime();
	}
}