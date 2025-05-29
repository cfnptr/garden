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
 * @brief Graphics resource device address pool. 
 */
class AddressPool final
{
	vector<uint64> allocations;
	vector<uint32> freeAllocs;
	DescriptorSet::Buffers addressBuffers = {};
	uint32 inFlightCount = 0;
	uint32 inFlightIndex = 0;
	uint32 capacity = 0;
	uint32 flushCount = 0;
public:
	/**
	 * @brief Creates a new device address pool instance.
	 * @param inFlightCount total in-flight frame count
	 */
	AddressPool(uint32 inFlightCount);

	/**
	 * @brief Returns device address pool allocations.
	 */
	const vector<uint64>& getAllocations() const noexcept { return allocations; }
	/**
	 * @brief Returns pool device address buffers.
	 */
	const DescriptorSet::Buffers& getAddressBuffers() const noexcept { return addressBuffers; }

	/**
	 * @brief Allocates a new resource index in the device address pool.
	 *
	 * @param deviceAddress target buffer resource device address
	 * @param[out] isExpanded pointer to the is expanded value
	 */
	uint32 allocate(uint64 deviceAddress = 0, bool* isExpanded = nullptr);
	/**
	 * @brief Updates resource in the device address pool.
	 *
	 * @param allocation target allocated buffer resource index
	 * @param newDeviceAddress new buffer resource device address
	 */
	void update(uint32 allocation, uint64 newDeviceAddress);
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
};

} // namespace garden::graphics