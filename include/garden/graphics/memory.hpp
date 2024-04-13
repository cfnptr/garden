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

/***********************************************************************************************************************
 * @file
 * @brief Common graphics memory functions.
 */

#pragma once
#include "garden/graphics/resource.hpp"

namespace garden::graphics
{

class MemoryExt;

/***********************************************************************************************************************
 * @brief Graphics memory base class. (buffer, image)
 * 
 * @details
 * The GPU (device) or CPU (host) memory that is used to store 
 * the data needed for rendering and computation tasks.
 */
class Memory : public Resource
{
public:
	/**
	 * @brief Graphics GPU memory access type.
	 */
	enum class Access : uint8
	{
		None,            /**< No CPU read/write access, GPU only memory. */
		SequentialWrite, /**< Sequential data write only from a CPU side. */
		RandomReadWrite, /**< Random data read/write from a CPU side. */
		Count            /**< Graphics memory access type count. */
	};
	/**
	 * @brief Graphics memory preferred usage.
	 */
	enum class Usage : uint8
	{
		Auto,      /**< Automatically select the best memory type for specific resource. */
		PreferGPU, /**< Prefer memory allocated on a GPU side. */
		PreferCPU, /**< Prefer memory allocated on a CPU side. */
		Count      /**< Graphics memory preferred usage count. */
	};
	/**
	 * @brief Graphics memory allocation strategy.
	 */
	enum class Strategy : uint8
	{
		Default, /**< Balanced speed/size memory allocation strategy. */
		Size,    /**< Search for smallest possible memory allocation place. */
		Speed,   /**< Allocate memory as fast as possible, sacrificing the size. */
		Count    /**< Graphics memory allocation strategy count. */
	};
protected:
	void* allocation = nullptr;
	uint64 binarySize = 0;
	uint64 version = 0;
	Access access = {};
	Usage usage = {};
	Strategy strategy = {};

	// Note: Use GraphicsSystem to create, destroy and access memory resources.

	Memory() = default;
	Memory(uint64 binarySize, Access access, Usage usage, Strategy strategy, uint64 version)
	{
		this->binarySize = binarySize;
		this->version = version;
		this->access = access;
		this->usage = usage;
		this->strategy = strategy;
	}

	friend class MemoryExt;
public:
	/*******************************************************************************************************************
	 * @brief Returns resource allocated memory size in bytes.
	 * @note The real allocated memory block size on GPU can differ.
	 */
	uint64 getBinarySize() const noexcept { return binarySize; }
	/**
	 * @brief Returns resource memory access type.
	 * @details Describes how GPU memory will be accessed from a CPU side.
	 */
	Access getMemoryAccess() const noexcept { return access; }
	/**
	 * @brief Returns resource memory preferred usage.
	 * @details Describes preferred memory allocation place, CPU or GPU.
	 */
	Usage getMemoryUsage() const noexcept { return usage; }
	/**
	 * @brief Returns resource memory allocation strategy.
	 * @details Describes allocation strategy, prefer speed or size.
	 */
	Strategy getMemoryStrategy() const noexcept { return strategy; }
};

/**
 * @brief Memory access name strings.
 */
static const string_view memoryAccessNames[(psize)Memory::Access::Count] =
{
	"None", "SequentialWrite", "RandomReadWrite"
};
/**
 * @brief Memory preferred usage name strings.
 */
static const string_view memoryUsageNames[(psize)Memory::Usage::Count] =
{
	"Auto", "PreferGPU", "PreferCPU"
};
/**
 * @brief Memory allocation strategy name strings.
 */
static const string_view memoryStrategyNames[(psize)Memory::Strategy::Count] =
{
	"Default", "Size", "Speed"
};

/**
 * @brief Returns memory access name string.
 * @param memoryAccess target memory access type
 */
static string_view toString(Memory::Access memoryAccess) noexcept
{
	GARDEN_ASSERT((uint8)memoryAccess < (uint8)Memory::Access::Count);
	return memoryAccessNames[(psize)memoryAccess];
}
/**
 * @brief Returns memory preferred usage name string.
 * @param memoryUsage target memory preferred usage type
 */
static string_view toString(Memory::Usage memoryUsage) noexcept
{
	GARDEN_ASSERT((uint8)memoryUsage < (uint8)Memory::Usage::Count);
	return memoryUsageNames[(psize)memoryUsage];
}
/**
 * @brief Returns memory allocation strategy name string.
 * @param memoryStrategy target memory allocation strategy type
 */
static string_view toString(Memory::Strategy memoryStrategy) noexcept
{
	GARDEN_ASSERT((uint8)memoryStrategy < (uint8)Memory::Strategy::Count);
	return memoryStrategyNames[(psize)memoryStrategy];
}

/***********************************************************************************************************************
 * @brief Graphics memory resource extension mechanism.
 * @warning Use only if you know what you are doing!
 */
class MemoryExt final
{
public:
	/**
	 * @brief Returns memory native allocation.
	 * @warning In most cases you should use @ref Memory functions.
	 * @param[in] memory target memory instance
	 */
	static void*& getAllocation(Memory& memory) noexcept { return memory.allocation; }
	/**
	 * @brief Returns memory allocation size in bytes.
	 * @warning In most cases you should use @ref Memory functions.
	 * @param[in] memory target memory instance
	 */
	static uint64& getBinarySize(Memory& memory) noexcept { return memory.binarySize; }
	/**
	 * @brief Returns memory instance version.
	 * @warning In most cases you should use @ref Memory functions.
	 * @param[in] memory target memory instance
	 */
	static uint64& getVersion(Memory& memory) noexcept { return memory.version; }
	/**
	 * @brief Returns memory access type.
	 * @warning In most cases you should use @ref Memory functions.
	 * @param[in] memory target memory instance
	 */
	static Memory::Access& getAccess(Memory& memory) noexcept { return memory.access; }
	/**
	 * @brief Returns memory preferred usage.
	 * @warning In most cases you should use @ref Memory functions.
	 * @param[in] memory target memory instance
	 */
	static Memory::Usage& getUsage(Memory& memory) noexcept { return memory.usage; }
	/**
	 * @brief Returns memory allocation strategy.
	 * @warning In most cases you should use @ref Memory functions.
	 * @param[in] memory target memory instance
	 */
	static Memory::Strategy& getStrategy(Memory& memory) noexcept { return memory.strategy; }
};

} // namespace garden::graphics