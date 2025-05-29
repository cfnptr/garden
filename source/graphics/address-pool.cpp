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

#include "garden/graphics/address-pool.hpp"
#include "garden/graphics/api.hpp"

using namespace garden;
using namespace garden::graphics;

static DescriptorSet::Buffers createAddressBuffers(uint32 capacity, uint32 inFlightCount)
{
	auto graphicsAPI = GraphicsAPI::get();
	DescriptorSet::Buffers addressBuffers(inFlightCount);

	for (auto& addressBuffer : addressBuffers)
	{
		auto buffer = graphicsAPI->bufferPool.create(Buffer::Usage::Storage, Buffer::CpuAccess::SequentialWrite, 
			Buffer::Location::Auto, Buffer::Strategy::Size, sizeof(uint64) * capacity, 0);
		addressBuffer.resize(1); addressBuffer[0] = buffer;
	}

	return addressBuffers;
}
static void destroyAddressBuffers(const DescriptorSet::Buffers& addressBuffers)
{
	auto graphicsAPI = GraphicsAPI::get();
	for (const auto& buffers : addressBuffers)
	{
		for (auto buffer : buffers)
			graphicsAPI->bufferPool.destroy(buffer);
	}
}

AddressPool::AddressPool(uint32 inFlightCount)
{
	this->inFlightCount = inFlightCount;
}

//**********************************************************************************************************************
uint32 AddressPool::allocate(uint64 deviceAddress, bool* isExpanded)
{
	uint32 allocation;
	if (freeAllocs.empty())
	{
		allocation = (uint32)allocations.size();
		allocations.push_back(deviceAddress);

		if (capacity == 0)
		{
			capacity = 16;
			allocations.reserve(capacity);
			addressBuffers = createAddressBuffers(capacity, inFlightCount);
			if (isExpanded) *isExpanded = true;
		}
		else if (allocation == capacity)
		{
			capacity *= 2;
			allocations.reserve(capacity);
			destroyAddressBuffers(addressBuffers);
			addressBuffers = createAddressBuffers(capacity, inFlightCount);
			if (isExpanded) *isExpanded = true;
		}
		else
		{
			if (isExpanded) *isExpanded = false;
		}
	}
	else
	{
		allocation = freeAllocs.back();
		freeAllocs.pop_back();
		allocations[allocation] = deviceAddress;
		if (isExpanded) *isExpanded = false;
	}
	
	flushCount = 0;
	return allocation;
}
void AddressPool::update(uint32 allocation, uint64 newDeviceAddress)
{
	GARDEN_ASSERT(newDeviceAddress);
	GARDEN_ASSERT(allocation < allocations.size());
	allocations[allocation] = newDeviceAddress;
	flushCount = 0;
}
void AddressPool::free(uint32 allocation)
{
	if (allocation == UINT32_MAX)
		return;

	#if GARDEN_DEBUG
	GARDEN_ASSERT(allocation < allocations.size());
	for (auto freeAlloc : freeAllocs)
		GARDEN_ASSERT(allocation != freeAlloc); // Already destroyed.
	#endif

	freeAllocs.push_back(allocation);
}

void AddressPool::flush()
{
	if (flushCount >= inFlightCount)
		return;

	auto buffer = addressBuffers[inFlightIndex][0];
	auto bufferView = GraphicsAPI::get()->bufferPool.get(buffer);
	memcpy(bufferView->getMap(), allocations.data(), allocations.size() * sizeof(uint64));
	bufferView->flush();
	flushCount++;
}
void AddressPool::nextFrame()
{
	inFlightIndex = (inFlightIndex + 1) % inFlightCount;
}

void AddressPool::destroy()
{
	destroyAddressBuffers(addressBuffers);
	allocations.clear();
	freeAllocs.clear();
	addressBuffers.clear();
}