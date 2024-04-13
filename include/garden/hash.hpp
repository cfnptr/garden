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

/**********************************************************************************************************************
 * @file
 * @brief Common hashing functions.
 */

#pragma once
#include "garden/defines.hpp"
#include <cstring>

namespace garden
{

using namespace std;
using namespace math;

/**
 * @brief An 128bit hash container. (non-cryptographic)
 * 
 * @details
 * Hash is a function that converts an input (or 'key') into a fixed-size set of bytes. 
 * This output set has a fixed length, regardless of the size of the input.
 */
struct Hash128
{
	typedef void* State;

	uint64 low64 = 0;
	uint64 high64 = 0;

	/**
	 * @brief Creates a new hash container from a low and high parts. (non-cryptographic)
	 */
	Hash128(uint64 low64 = 0, uint64 high64 = 0) noexcept
	{
		this->low64 = low64; this->high64 = high64;
	}
	/**
	 * @brief Creates a new hash of the binary data. (non-cryptographic)
	 * 
	 * @param[in] data target binary data to hash
	 * @param size data buffer size in bytes
	 * @param[in] state hash state (or null)
	 */
	Hash128(const void* data, psize size, State state = nullptr);

	/**
	 * @brief Creates a new hash of the vector data. (non-cryptographic)
	 * 
	 * @param[in] data target data to hash
	 * @param[in] state hash state (or null)
	 * @tparam T vector data type 
	 */
	template<typename T>
	Hash128(const vector<T>& data, State state = nullptr) :
		Hash128(data.data(), data.size() * sizeof(T), state) { }
	/**
	 * @brief Creates a new hash of the array data. (non-cryptographic)
	 * 
	 * @param[in] data target data to hash
	 * @param[in] state hash state (or null)
	 */
	template<typename T, psize S>
	Hash128(const array<T, S>& data, State state = nullptr) :
		Hash128(data.data(), data.size() * sizeof(T), state) { }

	bool operator==(const Hash128& h) const noexcept { 
		return low64 == h.low64 && high64 == h.high64; }
	bool operator!=(const Hash128& h) const noexcept {
		return low64 != h.low64 || high64 != h.high64; }
	bool operator<(const Hash128& h) const noexcept { 
		return memcmp(this, &h, sizeof(uint64) * 2) < 0; }
	
	/**
	 * @brief Returns Base64 encoded hash string.
	 * @details See the https://en.wikipedia.org/wiki/Base64
	 */
	string toString() const;

	/**
	 * @brief Allocates a new hash state. (non-cryptographic)
	 * @details You can reuse the same state to improve hashing speed.
	 * @return The new hash state instance.
	 */
	static State createState();
	/**
	 * @brief Deallocates hash state.
	 * @param[in] state target hash state
	 */
	static void destroyState(State state);

	// TODO: add sequential data hashing using state.
};

} // namespace garden