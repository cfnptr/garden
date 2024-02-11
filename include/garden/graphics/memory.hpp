//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#pragma once
#include "garden/graphics/resource.hpp"

#include <cstdint>
#include <cstddef>

namespace garden::graphics
{

class MemoryExt;

//--------------------------------------------------------------------------------------------------
class Memory : public Resource
{
public:
	enum class Access : uint8
	{
		None, SequentialWrite, RandomReadWrite, Count
	};
	enum class Usage : uint8
	{
		Auto, PreferGPU, PreferCPU, Count
	};
	enum class Strategy : uint8 // Allocation strategy
	{
		Default, Size, Speed, Count
	};
protected:
	void* allocation = nullptr;
	uint64 binarySize = 0;
	uint64 version = 0;
	Access access = {};
	Usage usage = {};
	Strategy strategy = {};

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
	uint64 getBinarySize() const noexcept { return binarySize; }
	Access getMemoryAccess() const noexcept { return access; }
	Usage getMemoryUsage() const noexcept { return usage; }
	Strategy getMemoryStrategy() const noexcept { return strategy; }
};

//--------------------------------------------------------------------------------------------------
static const string_view memoryAccessNames[(psize)Memory::Usage::Count] =
{
	"None", "SequentialWrite", "RandomReadWrite"
};
static const string_view memoryUsageNames[(psize)Memory::Usage::Count] =
{
	"Auto", "PreferGPU", "PreferCPU"
};

static string_view toString(Memory::Access memoryAccess)
{
	GARDEN_ASSERT((uint8)memoryAccess < (uint8)Memory::Access::Count);
	return memoryAccessNames[(psize)memoryAccess];
}
static string_view toString(Memory::Usage memoryUsage)
{
	GARDEN_ASSERT((uint8)memoryUsage < (uint8)Memory::Usage::Count);
	return memoryUsageNames[(psize)memoryUsage];
}

//--------------------------------------------------------------------------------------------------
class MemoryExt final
{
public:
	static void*& getAllocation(Memory& memory) noexcept { return memory.allocation; }
	static uint64& getBinarySize(Memory& memory) noexcept { return memory.binarySize; }
	static uint64& getVersion(Memory& memory) noexcept { return memory.version; }
	static Memory::Usage& getUsage(Memory& memory) noexcept { return memory.usage; }
};

} // namespace garden::graphics