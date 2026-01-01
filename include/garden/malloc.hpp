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

/***********************************************************************************************************************
 * @file
 * @brief Memory allocation functions.
 */

#include "math/types.hpp"
#include "garden/error.hpp"

namespace garden
{

using namespace std;
using namespace math;

/***************************************************************************************************************
 * @brief Allocates memory blocks.
 * @tparam T target element type
 * @param elementCount number of elements
 * @throw GardenError if failed to allocate
 */
template<typename T>
static T* malloc(psize elementCount)
{
	auto memoryBlock = ::malloc(elementCount * sizeof(T));
	if (!memoryBlock)
	{
		throw GardenError("Failed to allocate memory block. ("
			"size: " + to_string(elementCount * sizeof(T)) + ")");
	}
	return (T*)memoryBlock;
}
/**
 * @brief Allocates an array in memory with elements initialized to 0.
 * @tparam T target element type
 * @param elementCount number of elements
 * @throw GardenError if failed to allocate
 */
template<typename T>
static T* calloc(psize elementCount)
{
	auto memoryBlock = ::calloc(elementCount, sizeof(T));
	if (!memoryBlock)
	{
		throw GardenError("Failed to allocate memory block. ("
			"size: " + to_string(elementCount * sizeof(T)) + ")");
	}
	return (T*)memoryBlock;
}
/**
 * @brief Reallocates memory blocks.
 * @tparam T target element type
 * @param oldMemoryBlock old allocated memory block
 * @param elementCount number of elements
 * @throw GardenError if failed to reallocate
 */
template<typename T>
static T* realloc(T* oldMemoryBlock, psize elementCount)
{
	auto newMemoryBlock = ::realloc(oldMemoryBlock, elementCount * sizeof(T));
	if (!newMemoryBlock)
	{
		throw GardenError("Failed to reallocate memory block. ("
			"size: " + to_string(elementCount * sizeof(T)) + ")");
	}
	return (T*)newMemoryBlock;
}

/**
 * @brief Aligns size to the specified alignment.
 * 
 * @tparam T target size type
 * @param size size in bytes
 * @param alignment alignment in bytes
 */
template<typename T = psize>
static constexpr T alignSize(T size, T alignment) noexcept
{
	return (size + (alignment - 1)) & ~(alignment - 1);
}
/**
 * @brief Returns true if specified size is aligned.
 * 
 * @tparam T target size type
 * @param size size in bytes
 * @param alignment alignment in bytes
 */
template <typename T = psize>
static constexpr bool isSizeAligned(T size, T alignment) noexcept
{
	return size & (alignment - 1) == 0;
}

} // namespace garden