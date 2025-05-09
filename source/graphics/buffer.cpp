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

#include "garden/graphics/buffer.hpp"
#include "garden/graphics/vulkan/api.hpp"

using namespace std;
using namespace math;
using namespace garden;
using namespace garden::graphics;

//**********************************************************************************************************************
static constexpr vk::BufferUsageFlags toVkBufferUsages(Buffer::Bind bufferBind) noexcept
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
static VmaAllocationCreateFlagBits toVmaMemoryAccess(Buffer::Access memoryAccess) noexcept
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
static VmaMemoryUsage toVmaMemoryUsage(Buffer::Usage memoryUsage) noexcept
{
	switch (memoryUsage)
	{
	case Buffer::Usage::Auto: return VMA_MEMORY_USAGE_AUTO;
	case Buffer::Usage::PreferGPU: return VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
	case Buffer::Usage::PreferCPU: return VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
	default: abort();
	}
}
static VmaAllocationCreateFlagBits toVmaMemoryStrategy(Buffer::Strategy memoryUsage) noexcept
{
	switch (memoryUsage)
	{
	case Buffer::Strategy::Default: return {};
	case Buffer::Strategy::Size: return VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT;
	case Buffer::Strategy::Speed: return VMA_ALLOCATION_CREATE_STRATEGY_MIN_TIME_BIT;
	default: abort();
	}
}

//**********************************************************************************************************************
static void createVkBuffer(Buffer::Bind bind, Buffer::Access access, Buffer::Usage usage, 
	Buffer::Strategy strategy, uint64 size, void*& instance, void*& allocation, uint8*& map)
{
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

	if (access != Buffer::Access::None)
		allocationCreateInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;

	// TODO: VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT?

	auto vulkanAPI = VulkanAPI::get();
	VkBuffer vmaInstance; VmaAllocation vmaAllocation;
	auto result = vmaCreateBuffer(vulkanAPI->memoryAllocator,
		&bufferInfo, &allocationCreateInfo, &vmaInstance, &vmaAllocation, nullptr);
	if (result != VK_SUCCESS)
		throw GardenError("Failed to allocate GPU buffer.");

	instance = vmaInstance;
	allocation = vmaAllocation;

	VmaAllocationInfo allocationInfo = {};
	vmaGetAllocationInfo(vulkanAPI->memoryAllocator, vmaAllocation, &allocationInfo);
	if (access != Buffer::Access::None && !allocationInfo.pMappedData)
		throw GardenError("Failed to map buffer memory.");
	map = (uint8*)allocationInfo.pMappedData;
}

//**********************************************************************************************************************
Buffer::Buffer(Bind bind, Access access, Usage usage, Strategy strategy, uint64 size,
	uint64 version) : Memory(size, access, usage, strategy, version)
{
	GARDEN_ASSERT(size > 0);

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		createVkBuffer(bind, access, usage, strategy, size, instance, allocation, map);
	else abort();

	this->bind = bind;
}

//**********************************************************************************************************************
bool Buffer::destroy()
{
	if (!instance || readyLock > 0)
		return false;

	auto graphicsAPI = GraphicsAPI::get();

	#if GARDEN_DEBUG
	if (!graphicsAPI->forceResourceDestroy)
	{
		auto bufferInstance = graphicsAPI->bufferPool.getID(this);
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
				if (uniform == uniforms.end() || !isBufferType(uniform->second.type) ||
					uniform->second.descriptorSetIndex != descriptorSet.getIndex())
				{
					continue;
				}

				const auto& resourceSets = pair.second.resourceSets;
				for (const auto& resourceArray : resourceSets)
				{
					for (auto resource : resourceArray)
					{
						if (ID<Buffer>(resource) != bufferInstance)
							continue;
						throw GardenError("Descriptor set is still using destroyed buffer. (buffer: " +
							debugName + ", descriptorSet: " + descriptorSet.getDebugName() + ")");
					}
				}
			}
		}
	}
	#endif

	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		if (vulkanAPI->forceResourceDestroy)
			vmaDestroyBuffer(vulkanAPI->memoryAllocator, (VkBuffer)instance, (VmaAllocation)allocation);
		else
			vulkanAPI->destroyResource(GraphicsAPI::DestroyResourceType::Buffer, instance, allocation);
	}
	else abort();

	instance = nullptr;
	return true;
}

//**********************************************************************************************************************
bool Buffer::isMappable() const
{
	if (map)
		return true;

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		VkMemoryPropertyFlags memoryPropertyFlags;
		vmaGetAllocationMemoryProperties(VulkanAPI::get()->memoryAllocator,
			(VmaAllocation)allocation, &memoryPropertyFlags);
		return memoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
	}
	else abort();
}
void Buffer::invalidate(uint64 size, uint64 offset)
{
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT((size == 0 && offset == 0) || size + offset <= binarySize);
	GARDEN_ASSERT(isMappable());

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto result = vmaInvalidateAllocation(VulkanAPI::get()->memoryAllocator,
			(VmaAllocation)allocation, offset, size == 0 ? VK_WHOLE_SIZE : size);
		if (result != VK_SUCCESS)
			throw GardenError("Failed to invalidate buffer memory.");
	}
	else abort();
}
void Buffer::flush(uint64 size, uint64 offset)
{
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT((size == 0 && offset == 0) || size + offset <= binarySize);
	GARDEN_ASSERT(isMappable());

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto result = vmaFlushAllocation(VulkanAPI::get()->memoryAllocator,
			(VmaAllocation)allocation, offset, size == 0 ? VK_WHOLE_SIZE : size);
		if (result != VK_SUCCESS)
			throw GardenError("Failed to flush buffer memory.");
	}
	else abort();
}
void Buffer::writeData(const void* data, uint64 size, uint64 offset)
{
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(data);
	GARDEN_ASSERT((size == 0 && offset == 0) || size + offset <= binarySize);
	GARDEN_ASSERT(isMappable());

	if (map)
	{
		memcpy(map + offset, data, size == 0 ? this->binarySize : size);
		flush(size, offset);
		return;
	}

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		uint8* map = nullptr;
		auto result = vmaMapMemory(vulkanAPI->memoryAllocator,
			(VmaAllocation)allocation, (void**)&map);
		if (result != VK_SUCCESS)
			throw GardenError("Failed to map GPU memory.");
		memcpy(map + offset, data, size == 0 ? this->binarySize : size);
		flush(size, offset);
		vmaUnmapMemory(vulkanAPI->memoryAllocator, (VmaAllocation)allocation);
	}
	else abort();
}

#if GARDEN_DEBUG || GARDEN_EDITOR
//**********************************************************************************************************************
void Buffer::setDebugName(const string& name)
{
	Resource::setDebugName(name);

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		if (!vulkanAPI->hasDebugUtils || !instance)
			return;

		vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eBuffer, (uint64)instance, name.c_str());
		vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo);
	}
	else abort();
}
#endif

//**********************************************************************************************************************
void Buffer::fill(uint32 data, uint64 size, uint64 offset)
{
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(size == 0 || size % 4 == 0);
	GARDEN_ASSERT(size == 0 || size + offset <= binarySize);
	GARDEN_ASSERT(hasAnyFlag(bind, Bind::TransferDst));
	GARDEN_ASSERT(!GraphicsAPI::get()->currentFramebuffer);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);
	auto graphicsAPI = GraphicsAPI::get();

	FillBufferCommand command;
	command.buffer = graphicsAPI->bufferPool.getID(this);
	command.data = data;
	command.size = size;
	command.offset = offset;
	graphicsAPI->currentCommandBuffer->addCommand(command);

	if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
	{
		readyLock++;
		graphicsAPI->currentCommandBuffer->addLockResource(command.buffer);
	}
}

//**********************************************************************************************************************
void Buffer::copy(ID<Buffer> source, ID<Buffer> destination, const CopyRegion* regions, uint32 count)
{
	GARDEN_ASSERT(source);
	GARDEN_ASSERT(destination);
	GARDEN_ASSERT(regions);
	GARDEN_ASSERT(count > 0);
	GARDEN_ASSERT(!GraphicsAPI::get()->currentFramebuffer);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);
	auto graphicsAPI = GraphicsAPI::get();

	auto srcView = graphicsAPI->bufferPool.get(source);
	GARDEN_ASSERT(srcView->instance); // is ready
	GARDEN_ASSERT(hasAnyFlag(srcView->bind, Bind::TransferSrc));

	auto dstView = graphicsAPI->bufferPool.get(destination);
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
	graphicsAPI->currentCommandBuffer->addCommand(command);

	if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
	{
		srcView->readyLock++;
		dstView->readyLock++;
		graphicsAPI->currentCommandBuffer->addLockResource(source);
		graphicsAPI->currentCommandBuffer->addLockResource(destination);
	}
}