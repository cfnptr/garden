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
static constexpr vk::BufferUsageFlags toVkBufferUsages(Buffer::Usage bufferUsage) noexcept
{
	vk::BufferUsageFlags flags;
	if (hasAnyFlag(bufferUsage, Buffer::Usage::TransferSrc))
		flags |= vk::BufferUsageFlagBits::eTransferSrc;
	if (hasAnyFlag(bufferUsage, Buffer::Usage::TransferDst))
		flags |= vk::BufferUsageFlagBits::eTransferDst;
	if (hasAnyFlag(bufferUsage, Buffer::Usage::Vertex))
		flags |= vk::BufferUsageFlagBits::eVertexBuffer;
	if (hasAnyFlag(bufferUsage, Buffer::Usage::Index))
		flags |= vk::BufferUsageFlagBits::eIndexBuffer;
	if (hasAnyFlag(bufferUsage, Buffer::Usage::Uniform))
		flags |= vk::BufferUsageFlagBits::eUniformBuffer;
	if (hasAnyFlag(bufferUsage, Buffer::Usage::Storage))
		flags |= vk::BufferUsageFlagBits::eStorageBuffer;
	if (hasAnyFlag(bufferUsage, Buffer::Usage::Indirect))
		flags |= vk::BufferUsageFlagBits::eIndirectBuffer;
	if (hasAnyFlag(bufferUsage, Buffer::Usage::DeviceAddress))
		flags |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
	if (hasAnyFlag(bufferUsage, Buffer::Usage::StorageAS))
		flags |= vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR;
	if (hasAnyFlag(bufferUsage, Buffer::Usage::BuildInputAS))
		flags |= vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
	if (hasAnyFlag(bufferUsage, Buffer::Usage::SBT))
		flags |= vk::BufferUsageFlagBits::eShaderBindingTableKHR;
	return flags;
}
static VmaAllocationCreateFlagBits toVmaMemoryAccess(Buffer::CpuAccess memoryCpuAccess) noexcept
{
	switch (memoryCpuAccess)
	{
	case Buffer::CpuAccess::None: return {};
	case Buffer::CpuAccess::SequentialWrite:
		return VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
	case Buffer::CpuAccess::RandomReadWrite:
		return VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
	default: abort();
	}
}
static VmaMemoryUsage toVmaMemoryUsage(Buffer::Location memoryLocation) noexcept
{
	switch (memoryLocation)
	{
	case Buffer::Location::Auto: return VMA_MEMORY_USAGE_AUTO;
	case Buffer::Location::PreferGPU: return VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
	case Buffer::Location::PreferCPU: return VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
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
static void createVkBuffer(Buffer::Usage usage, Buffer::CpuAccess cpuAccess, Buffer::Location location, 
	Buffer::Strategy strategy, uint64 size, void*& instance, void*& allocation, uint8*& map, uint64& deviceAddress)
{
	VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.size = size;
	bufferInfo.usage = (VkBufferUsageFlags)toVkBufferUsages(usage);
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	bufferInfo.queueFamilyIndexCount = 0;
	bufferInfo.pQueueFamilyIndices = nullptr;

	VmaAllocationCreateInfo allocationCreateInfo = {};
	allocationCreateInfo.flags = toVmaMemoryAccess(cpuAccess) | toVmaMemoryStrategy(strategy);
	allocationCreateInfo.usage = toVmaMemoryUsage(location);
	allocationCreateInfo.priority = 0.5f; // TODO: expose this RAM offload priority?

	if (cpuAccess != Buffer::CpuAccess::None)
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
	if (cpuAccess != Buffer::CpuAccess::None && !allocationInfo.pMappedData)
		throw GardenError("Failed to map buffer memory.");
	map = (uint8*)allocationInfo.pMappedData;

	if (hasAnyFlag(usage, Buffer::Usage::DeviceAddress))
	{
		vk::BufferDeviceAddressInfo info(vmaInstance);
		deviceAddress = (uint64)VulkanAPI::get()->device.getBufferAddress(info);
	}
}

//**********************************************************************************************************************
Buffer::Buffer(Usage usage, CpuAccess cpuAccess, Location location, Strategy strategy, uint64 size,
	uint64 version) : Memory(size, cpuAccess, location, strategy, version)
{
	GARDEN_ASSERT(size > 0);

	auto graphicsAPI = GraphicsAPI::get();
	if (hasAnyFlag(usage, Buffer::Usage::DeviceAddress) && !graphicsAPI->hasBufferDeviceAddress())
		throw GardenError("Device buffer address is not supported on this GPU.");

	if (!graphicsAPI->hasRayTracing() && hasAnyFlag(usage, Buffer::Usage::StorageAS | 
		Buffer::Usage::BuildInputAS | Buffer::Usage::SBT))
	{
		throw GardenError("Ray tracing acceleration is not supported on this GPU.");
	}

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		createVkBuffer(usage, cpuAccess, location, strategy, size, instance, allocation, map, deviceAddress);
	else abort();

	this->usage = usage;
}

//**********************************************************************************************************************
bool Buffer::destroy()
{
	if (!instance || busyLock > 0)
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
			if (pipelineView->isBindless())
				continue;

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
	GARDEN_ASSERT(hasAnyFlag(usage, Usage::TransferDst));
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
		busyLock++;
		graphicsAPI->currentCommandBuffer->addLockedResource(command.buffer);
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
	GARDEN_ASSERT(hasAnyFlag(srcView->usage, Usage::TransferSrc));

	auto dstView = graphicsAPI->bufferPool.get(destination);
	GARDEN_ASSERT(dstView->instance); // is ready
	GARDEN_ASSERT(hasAnyFlag(dstView->usage, Usage::TransferDst));
	
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
		srcView->busyLock++;
		dstView->busyLock++;
		graphicsAPI->currentCommandBuffer->addLockedResource(source);
		graphicsAPI->currentCommandBuffer->addLockedResource(destination);
	}
}