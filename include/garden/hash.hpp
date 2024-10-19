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
 * @brief Common hashing functions.
 */

#pragma once
#include "garden/defines.hpp"

#include <vector>
#include <cstring>

namespace garden
{

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
protected:
	static State stateInstance;
public:
	uint64 low64 = 0;
	uint64 high64 = 0;

	/**
	 * @brief Creates a new hash container from a low and high parts. (non-cryptographic)
	 */
	constexpr Hash128(uint64 low64 = 0, uint64 high64 = 0) noexcept : low64(low64), high64(high64) { }
	/**
	 * @brief Creates a new hash of the binary data. (non-cryptographic)
	 * 
	 * @param[in] data target binary data to hash
	 * @param size data buffer size in bytes
	 * @param[in] state hash state (or null)
	 */
	Hash128(const void* data, psize size, State state = nullptr);
	/**
	 * @brief Creates a new hash from the base64 encoded string. (non-cryptographic)
	 * @param base64 target base64 encoded string
	 */
	Hash128(string_view base64);

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
	 * @tparam T array data type 
	 * @tparam S array element count 
	 */
	template<typename T, psize S>
	Hash128(const array<T, S>& data, State state = nullptr) :
		Hash128(data.data(), S * sizeof(T), state) { }

	bool operator==(const Hash128& h) const noexcept { 
		return low64 == h.low64 && high64 == h.high64; }
	bool operator!=(const Hash128& h) const noexcept {
		return low64 != h.low64 || high64 != h.high64; }
	bool operator<(const Hash128& h) const noexcept { 
		return memcmp(this, &h, sizeof(uint64) * 2) < 0; }

	/**
	 * @brief Returns true if hash is not all zeros.
	 */
	explicit operator bool() const noexcept { return low64 | high64; }


	/******************************************************************************************************************
	 * @brief Returns hash Base64 encoded string.
	 * @details See the https://en.wikipedia.org/wiki/Base64
	 */
	string toBase64() const noexcept;
	/**
	 * @brief Returns hash Base64 encoded string.
	 * @details See the https://en.wikipedia.org/wiki/Base64
	 */
	void toBase64(string& base64) const noexcept;
	/**
	 * @brief Decodes hash from the Base64 string if valid.
	 * @details See the https://en.wikipedia.org/wiki/Base64
	 */
	bool fromBase64(string_view base64) noexcept;

	/**
	 * @brief Generates a new random hash. (non-cryptographic)
	 * @details It uses mt19937 generator. (pseudo-random)
	 */
	static Hash128 generateRandom(uint64 seed) noexcept;

	/******************************************************************************************************************
	 * @brief Allocates a new hash state. (non-cryptographic)
	 * @details You can reuse the same state to improve hashing speed.
	 */
	static State createState();
	/**
	 * @brief Deallocates hash state.
	 * @param[in] state target hash state
	 */
	static void destroyState(State state) noexcept;

	/**
	 * @brief Resets hash state to begin a new hash session.
	 * @param state target hash state
	 */
	static void resetState(State state);
	/**
	 * @brief Consumes a block of data to hash state.
	 * 
	 * @param state hash state to use
	 * @param[in] data target binary data to hash
	 * @param size data buffer size in bytes
	 */
	static void updateState(State state, const void* data, psize size);
	/**
	 * @brief Retrieves the finalized hash from state. (non-cryptographic)
	 * @note This call will not change the state.
	 * @param state target hash state
	 */
	static Hash128 digestState(State state) noexcept;

	/**
	 * @brief Consumes a block of vector data to hash state.
	 *
	 * @param state hash state to use
	 * @param[in] data target data to hash
	 * @tparam T vector data type
	 */
	template<typename T>
	static void updateState(State state, const vector<T>& data) {
		updateState(state, data.data(), data.size() * sizeof(T)); }
	/**
	 * @brief Consumes a block of array data to hash state.
	 *
	 * @param state hash state to use
	 * @param[in] data target data to hash
	 * @tparam T array data type 
	 * @tparam S array element count 
	 */
	template<typename T, psize S>
	static void updateState(State state, const array<T, S>& data) {
		updateState(state, data.data(), S * sizeof(T)); }

	/**
	 * @brief Returns singleton hash state instance.
	 * @details Creates a new one on first call.
	 */
	static State getState();
};

} // namespace garden