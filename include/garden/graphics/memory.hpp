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
 * @brief Common graphics memory functions.
 */

#pragma once
#include "garden/graphics/common.hpp"
#include "garden/graphics/resource.hpp"
#include "math/flags.hpp"

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
	 * @brief Graphics GPU memory CPU side access.
	 */
	enum class CpuAccess : uint8
	{
		None,            /**< No CPU read/write access, GPU only memory. */
		SequentialWrite, /**< Sequential data write only from a CPU side. */
		RandomReadWrite, /**< Random data read/write from a CPU side. */
		Count            /**< Graphics GPU memory CPU side access type count. */
	};
	/**
	 * @brief Graphics memory preferred location.
	 */
	enum class Location : uint8
	{
		Auto,      /**< Automatically select the best memory location for specific resource. */
		PreferGPU, /**< Prefer memory allocated on a GPU side. */
		PreferCPU, /**< Prefer memory allocated on a CPU side. */
		Count      /**< Graphics memory preferred location count. */
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
	/**
	 * @brief Graphics memory access flags.
	 */
	enum class AccessFlags : uint8
	{
		None = 0x00,        /**< No memory access flags. */
		ShaderRead = 0x01,  /**< Specifies read access to a shader uniform. */
		ShaderWrite = 0x02, /**< Specifies write access to a shader uniform. */
		// TODO: other shader access flags
	};

	/**
	 * @brief Graphics memory barrier state.
	 */
	struct BarrierState
	{
		uint32 access = 0; /**< Memory access flags. (Internal API format) */
		uint32 stage = 0;  /**< Pipeline stages. (Internal API format) */
	};
protected:
	void* allocation = nullptr;
	uint64 binarySize = 0;
	uint64 version = 0;
	CpuAccess cpuAccess = {};
	Location location = {};
	Strategy strategy = {};

	Memory(uint64 binarySize, CpuAccess cpuAccess, Location location, Strategy strategy, uint64 version) noexcept :
		binarySize(binarySize), version(version), cpuAccess(cpuAccess), location(location), strategy(strategy) { }
	friend class MemoryExt;
public:
	/*******************************************************************************************************************
	 * @brief Creates a new empty memory data container.
	 * @note Use @ref GraphicsSystem to create, destroy and access memory resources.
	 */
	Memory() = default;

	/**
	 * @brief Returns resource allocated memory size in bytes.
	 * @note The real allocated memory block size on GPU can differ.
	 */
	uint64 getBinarySize() const noexcept { return binarySize; }
	/**
	 * @brief Returns resource memory CPU side access.
	 * @details Describes how GPU memory will be accessed from a CPU side.
	 */
	CpuAccess getCpuAccess() const noexcept { return cpuAccess; }
	/**
	 * @brief Returns resource memory preferred location.
	 * @details Describes preferred memory allocation place, CPU or GPU.
	 */
	Location getLocation() const noexcept { return location; }
	/**
	 * @brief Returns resource memory allocation strategy.
	 * @details Describes allocation strategy, prefer speed or size.
	 */
	Strategy getStrategy() const noexcept { return strategy; }

	/**
	 * @brief Creates memory barrier state.
	 * @param accessFlags memory access flags
	 */
	static BarrierState toBarrierState(AccessFlags accessFlags, PipelineStage pipelineStages) noexcept;
};

DECLARE_ENUM_CLASS_FLAG_OPERATORS(Memory::AccessFlags)

/***********************************************************************************************************************
 * @brief Returns memory access flag name string.
 * @param accessFlag target memory access flag
 */
static string_view toString(Memory::AccessFlags accessFlag)
{
	if (hasOneFlag(accessFlag, Memory::AccessFlags::ShaderRead)) return "ShaderRead";
	if (hasOneFlag(accessFlag, Memory::AccessFlags::ShaderWrite)) return "ShaderWrite";
	return "None";
}
/**
 * @brief Returns buffer usage name string list.
 * @param accessFlags target memory access flags
 */
static string toStringList(Memory::AccessFlags accessFlags)
{
	string list;
	if (hasAnyFlag(accessFlags, Memory::AccessFlags::ShaderRead)) list += "ShaderRead | ";
	if (hasAnyFlag(accessFlags, Memory::AccessFlags::ShaderWrite)) list += "ShaderWrite | ";
	if (list.length() >= 3) list.resize(list.length() - 3);
	else return "None";
	return list;
}

/***********************************************************************************************************************
 * @brief Memory CPU side access name strings.
 */
constexpr const char* memoryCpuAccessNames[(psize)Memory::CpuAccess::Count] =
{
	"None", "SequentialWrite", "RandomReadWrite"
};
/**
 * @brief Memory preferred location name strings.
 */
constexpr const char* memoryLocationNames[(psize)Memory::Location::Count] =
{
	"Auto", "PreferGPU", "PreferCPU"
};
/**
 * @brief Memory allocation strategy name strings.
 */
constexpr const char* memoryStrategyNames[(psize)Memory::Strategy::Count] =
{
	"Default", "Size", "Speed"
};

/**
 * @brief Returns memory CPU side access name string.
 * @param memoryCpuAccess target memory CPU side access
 */
static string_view toString(Memory::CpuAccess memoryCpuAccess) noexcept
{
	GARDEN_ASSERT(memoryCpuAccess < Memory::CpuAccess::Count);
	return memoryCpuAccessNames[(psize)memoryCpuAccess];
}
/**
 * @brief Returns memory preferred location name string.
 * @param memoryLocation target memory preferred location type
 */
static string_view toString(Memory::Location memoryLocation) noexcept
{
	GARDEN_ASSERT(memoryLocation < Memory::Location::Count);
	return memoryLocationNames[(psize)memoryLocation];
}
/**
 * @brief Returns memory allocation strategy name string.
 * @param memoryStrategy target memory allocation strategy type
 */
static string_view toString(Memory::Strategy memoryStrategy) noexcept
{
	GARDEN_ASSERT(memoryStrategy < Memory::Strategy::Count);
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
	 * @brief Returns memory CPU side access.
	 * @warning In most cases you should use @ref Memory functions.
	 * @param[in] memory target memory instance
	 */
	static Memory::CpuAccess& getCpuAccess(Memory& memory) noexcept { return memory.cpuAccess; }
	/**
	 * @brief Returns memory preferred location.
	 * @warning In most cases you should use @ref Memory functions.
	 * @param[in] memory target memory instance
	 */
	static Memory::Location& getLocation(Memory& memory) noexcept { return memory.location; }
	/**
	 * @brief Returns memory allocation strategy.
	 * @warning In most cases you should use @ref Memory functions.
	 * @param[in] memory target memory instance
	 */
	static Memory::Strategy& getStrategy(Memory& memory) noexcept { return memory.strategy; }
};

} // namespace garden::graphics