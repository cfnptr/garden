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

#include "garden/graphics/address-pool.hpp"
#include "garden/graphics/vulkan/command-buffer.hpp"

using namespace garden;
using namespace garden::graphics;

static DescriptorSet::Buffers createAddressBuffers(uint32 capacity, 
	uint32 inFlightCount, Buffer::Usage addressBufferUsage)
{
	auto graphicsAPI = GraphicsAPI::get();
	DescriptorSet::Buffers addressBuffers(inFlightCount);

	for (auto& addressBuffer : addressBuffers)
	{
		auto buffer = graphicsAPI->bufferPool.create(Buffer::Usage::Storage | 
			addressBufferUsage, Buffer::CpuAccess::SequentialWrite, 
			Buffer::Location::Auto, Buffer::Strategy::Default, sizeof(uint64) * capacity, 0);
		addressBuffer.resize(1); addressBuffer[0] = buffer;
	}

	return addressBuffers;
}
static void destroyAddressBuffers(GraphicsAPI* graphicsAPI, const DescriptorSet::Buffers& addressBuffers)
{
	for (const auto& buffers : addressBuffers)
	{
		for (auto buffer : buffers)
			graphicsAPI->bufferPool.destroy(buffer);
	}
}

AddressPool::AddressPool(uint32 inFlightCount, Buffer::Usage addressBufferUsage) : isFlushed(inFlightCount, 0)
{
	this->inFlightCount = inFlightCount;
	this->addressBufferUsage = addressBufferUsage;
}

//**********************************************************************************************************************
uint32 AddressPool::allocate(ID<Buffer> buffer)
{
	uint64 deviceAddress = 0;
	if (buffer)
	{
		auto bufferView = GraphicsAPI::get()->bufferPool.get(buffer);
		deviceAddress = bufferView->getDeviceAddress();
	}

	uint32 allocation;
	if (freeAllocs.empty())
	{
		allocation = (uint32)resources.size();
		resources.push_back(buffer);
		deviceAddresses.push_back(deviceAddress);

		if (capacity == 0)
		{
			capacity = 16;
			resources.reserve(capacity);
			deviceAddresses.reserve(capacity);
			barrierBuffers.resize(capacity);
		}
		else if (allocation == capacity)
		{
			capacity *= 2;
			resources.reserve(capacity);
			deviceAddresses.reserve(capacity);
			barrierBuffers.resize(capacity);
		}
	}
	else
	{
		allocation = freeAllocs.back();
		freeAllocs.pop_back();
		resources[allocation] = buffer;
		deviceAddresses[allocation] = deviceAddress;
	}
	
	memset(isFlushed.data(), 0, inFlightCount);
	return allocation;
}
void AddressPool::update(uint32 allocation, ID<Buffer> newBuffer)
{
	GARDEN_ASSERT(allocation < resources.size());
	if (newBuffer)
	{
		auto bufferView = GraphicsAPI::get()->bufferPool.get(newBuffer);
		deviceAddresses[allocation] = bufferView->getDeviceAddress();
		memset(isFlushed.data(), 0, inFlightCount);
	}
	else
	{
		deviceAddresses[allocation] = 0;
	}
	resources[allocation] = newBuffer;
}
void AddressPool::free(uint32 allocation)
{
	if (allocation == UINT32_MAX)
		return;

	#if GARDEN_DEBUG
	GARDEN_ASSERT(allocation < resources.size());
	for (auto freeAlloc : freeAllocs)
	{
		GARDEN_ASSERT_MSG(allocation != freeAlloc, "Allocation [" + 
			to_string(allocation) + "] is already freed");
	}
	#endif

	// Note: Do not remove cleaners! We need it for addBufferBarriers().
	resources[allocation] = {};
	deviceAddresses[allocation] = 0;
	freeAllocs.push_back(allocation);
}

//**********************************************************************************************************************
void AddressPool::recreate()
{
	auto graphicsAPI = GraphicsAPI::get();
	destroyAddressBuffers(graphicsAPI, addressBuffers);
	addressBuffers = createAddressBuffers(capacity, inFlightCount, addressBufferUsage);
	memset(isFlushed.data(), 0, inFlightCount);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	for (const auto& buffers : addressBuffers)
	{
		for (auto buffer : buffers)
		{
			auto bufferView = graphicsAPI->bufferPool.get(buffer);
			bufferView->setDebugName(debugName + to_string(*buffer));
		}
	}
	#endif
}

void AddressPool::flush(uint32 inFlightIndex, bool& newAddressBuffer)
{
	GARDEN_ASSERT(inFlightIndex < inFlightCount);

	if (isFlushed[inFlightIndex])
		return;

	auto graphicsAPI = GraphicsAPI::get();
	newAddressBuffer = false;

	if (!addressBuffers.empty())
	{
		auto bufferView = graphicsAPI->bufferPool.get(addressBuffers[0][0]);
		if (bufferView->getBinarySize() < capacity * sizeof(uint64))
		{
			destroyAddressBuffers(graphicsAPI, addressBuffers);
			addressBuffers = createAddressBuffers(capacity, inFlightCount, addressBufferUsage);
			newAddressBuffer = true;
		}
	}
	else
	{
		addressBuffers = createAddressBuffers(capacity, inFlightCount, addressBufferUsage);
		newAddressBuffer = true;
	}

	#if GARDEN_DEBUG || GARDEN_EDITOR
	if (newAddressBuffer)
	{
		for (const auto& buffers : addressBuffers)
		{
			for (auto buffer : buffers)
			{
				auto bufferView = graphicsAPI->bufferPool.get(buffer);
				bufferView->setDebugName(debugName + to_string(*buffer));
			}
		}
	}
	#endif
	
	auto buffer = addressBuffers[inFlightIndex][0];
	auto bufferView = graphicsAPI->bufferPool.get(buffer);
	memcpy(bufferView->getMap(), deviceAddresses.data(), deviceAddresses.size() * sizeof(uint64));
	bufferView->flush();

	isFlushed[inFlightIndex] = 1;
}

void AddressPool::destroy()
{
	destroyAddressBuffers(GraphicsAPI::get(), addressBuffers);
	resources.clear(); deviceAddresses.clear(); freeAllocs.clear(); addressBuffers.clear();
}

//**********************************************************************************************************************
void AddressPool::addBufferBarriers(Buffer::BarrierState newState)
{
	auto graphicsAPI = GraphicsAPI::get();
	GARDEN_ASSERT(graphicsAPI->currentCommandBuffer);

	#if GARDEN_DEBUG
	if (graphicsAPI->currentCommandBuffer == graphicsAPI->transferCommandBuffer)
	{
		for (uint32 i = 0; i < (uint32)resources.size(); i++)
		{
			if (!resources[i])
				continue;
			auto bufferView = graphicsAPI->bufferPool.get(resources[i]);
			GARDEN_ASSERT_MSG(hasAnyFlag(bufferView->getUsage(), Buffer::Usage::TransferQ), 
				"Buffer [" + bufferView->getDebugName() + "] does not have transfer queue flag");
		}
	}
	else if (graphicsAPI->currentCommandBuffer == graphicsAPI->computeCommandBuffer)
	{
		for (uint32 i = 0; i < (uint32)resources.size(); i++)
		{
			if (!resources[i])
				continue;
			auto bufferView = graphicsAPI->bufferPool.get(resources[i]);
			GARDEN_ASSERT_MSG(hasAnyFlag(bufferView->getUsage(), Buffer::Usage::ComputeQ), 
				"Buffer [" + bufferView->getDebugName() + "] does not have compute queue flag");
		}
	}
	#endif

	auto buffers = resources.data();
	auto barriers = barrierBuffers.data();
	auto bufferCount = (uint32)resources.size();
	uint32 barrierCount = 0;

	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		for (uint32 i = 0; i < bufferCount; i++)
		{
			if (!buffers[i])
				continue;

			auto bufferView = graphicsAPI->bufferPool.get(buffers[i]);
			GARDEN_ASSERT(bufferView->getBinarySize() > 0); // Note: not deallocated.

			if (VulkanCommandBuffer::isDifferentState(BufferExt::getBarrierState(**bufferView), newState))
				barriers[barrierCount++] = buffers[i];
		}
	}
	else abort();

	BufferBarrierCommand command;
	command.newState = newState;
	command.bufferCount = barrierCount;
	command.buffers = barriers;
	graphicsAPI->currentCommandBuffer->addCommand(command);
}