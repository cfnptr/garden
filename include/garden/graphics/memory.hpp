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
	enum class Usage : uint8
	{
		GpuOnly, CpuOnly, CpuToGpu, GpuToCpu, Count
	};
protected:
	void* allocation = nullptr;
	uint64 binarySize = 0;
	uint64 version = 0;
	Usage usage = {};

	Memory() = default;
	Memory(uint64 binarySize, Usage usage, uint64 version)
	{
		this->binarySize = binarySize;
		this->version = version;
		this->usage = usage;
	}

	friend class MemoryExt;
public:
	uint64 getBinarySize() const noexcept { return binarySize; }
	Usage getMemoryUsage() const noexcept { return usage; }
};

//--------------------------------------------------------------------------------------------------
static const string_view memoryUsageNames[(psize)Memory::Usage::Count] =
{
	"GpuOnly", "CpuOnly", "CpuToGpu", "GpuToCpu"
};

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

} // garden::graphics