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

/***********************************************************************************************************************
 * @file
 * @brief Graphics resource device address pool functions.
 */

// TODO: add some king of BarrierResources command to synchronize buffer reference resources.

#pragma once
#include "garden/graphics/descriptor-set.hpp"

namespace garden::graphics
{

/**
 * @brief Graphics buffer resource device address pool. 
 */
class AddressPool final
{
	vector<ID<Buffer>> resources;
	vector<uint64> deviceAddresses;
	vector<ID<Buffer>> barrierBuffers;
	vector<uint32> freeAllocs;
	DescriptorSet::Buffers addressBuffers = {};
	uint32 inFlightCount = 0;
	uint32 inFlightIndex = 0;
	uint32 capacity = 0;
	uint32 flushCount = 0;
	Buffer::Usage addressBufferUsage = {};
public:
	#if GARDEN_DEBUG || GARDEN_EDITOR
	string debugName = "unnamed";
	#endif

	/**
	 * @brief Creates a new device address pool instance.
	 *
	 * @param inFlightCount total in-flight frame count
	 * @param addressBufferUsage additional address buffer usage flags
	 */
	AddressPool(uint32 inFlightCount, Buffer::Usage addressBufferUsage = {});

	/**
	 * @brief Returns device address pool resources.
	 */
	const vector<ID<Buffer>>& getResources() const noexcept { return resources; }
	/**
	 * @brief Returns device address pool buffer addresses.
	 */
	const vector<uint64>& getDeviceAddresses() const noexcept { return deviceAddresses; }
	/**
	 * @brief Returns pool device address buffers.
	 */
	const DescriptorSet::Buffers& getAddressBuffers() const noexcept { return addressBuffers; }

	/**
	 * @brief Allocates a new resource index in the device address pool.
	 * @param buffer target buffer resource or null
	 */
	uint32 allocate(ID<Buffer> buffer = {});
	/**
	 * @brief Updates resource in the device address pool.
	 *
	 * @param allocation target allocated buffer resource index
	 * @param newBuffer new buffer resource
	 */
	void update(uint32 allocation, ID<Buffer> newBuffer);
	/**
	 * @brief Frees device address pool resource allocation.
	 * @param allocation target allocated buffer resource index
	 */
	void free(uint32 allocation);

	/**
	 * @brief Flushes current in-flight device address buffer.
	 */
	void flush();
	/**
	 * @brief Update in-flight frame counter.
	 */
	void nextFrame();

	/**
	 * @brief Destroys device address pool buffers.
	 */
	void destroy();

	//******************************************************************************************************************
	// Render commands
	//******************************************************************************************************************

	/**
	 * @brief Adds buffer memory barriers.
	 * @warning Address pool buffers are not synchronized on the GPU automatically!
	 * @param newState new buffer barrier state
	 */
	void addBufferBarriers(Buffer::BarrierState newState);
};

} // namespace garden::graphics