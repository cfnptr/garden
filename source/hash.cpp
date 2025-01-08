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

#include "garden/hash.hpp"
#include "garden/base64.hpp"
#include <random>

#define XXH_INLINE_ALL
#define XXH_STATIC_LINKING_ONLY
#if GARDEN_DEBUG
#define XXH_DEBUGLEVEL 1
#endif
#include "xxhash.h"

using namespace garden;

//**********************************************************************************************************************
Hash128::State Hash128::stateInstance = nullptr;

Hash128::Hash128(const void* data, psize size, Hash128::State state)
{
	if (state)
	{
		auto hashState = (XXH3_state_t*)state;
		if (XXH3_128bits_reset(hashState) == XXH_ERROR)
			throw GardenError("Failed to reset xxHash state");
		if (XXH3_128bits_update(hashState, data, size) == XXH_ERROR)
			throw GardenError("Failed to update xxHash state");
		auto hashResult = XXH3_128bits_digest(hashState);
		this->low64 = hashResult.low64; this->high64 = hashResult.high64;
	}
	else
	{
		auto hashResult = XXH3_128bits(data, size);
		this->low64 = hashResult.low64; this->high64 = hashResult.high64;
	}
}
Hash128::Hash128(string_view base64)
{
	if (modp_b64_decode_len(base64.size()) != sizeof(Hash128))
		throw GardenError("Invalid Hash128 base64 string length");
	auto result = modp_b64_decode((char*)this, base64.data(), 
		base64.size(), ModpDecodePolicy::kForgiving);
	if (result == MODP_B64_ERROR)
		throw GardenError("Invalid Hash128 base64 string");
}
	
string Hash128::toBase64() const noexcept
{
	string base64;
	encodeBase64(base64, this, sizeof(Hash128));
	base64.resize(base64.length() - 2);
	return base64;
}
void Hash128::toBase64(string& base64) const noexcept
{
	encodeBase64(base64, this, sizeof(Hash128));
	base64.resize(base64.length() - 2);
}
bool Hash128::fromBase64(string_view base64) noexcept
{
	if (base64.size() + 2 != modp_b64_encode_data_len(sizeof(Hash128)))
		return false;
	return decodeBase64(this, base64, ModpDecodePolicy::kForgiving);
}

Hash128 Hash128::generateRandom(uint64 seed) noexcept
{
	mt19937_64 mt(seed);
	Hash128 hash;
	hash.low64 = mt();
	hash.high64 = mt();
	return hash;
}

//**********************************************************************************************************************
Hash128::State Hash128::createState()
{
	auto state = XXH3_createState();
	if (!state)
		throw GardenError("Failed to create xxHash state");
	if (XXH3_128bits_reset((XXH3_state_t*)state) == XXH_ERROR)
		throw GardenError("Failed to reset xxHash state");
	return state;
}
void Hash128::destroyState(Hash128::State state) noexcept
{
	GARDEN_ASSERT(state);
	XXH3_freeState((XXH3_state_t*)state);
}

void Hash128::resetState(State state)
{
	GARDEN_ASSERT(state);
	if (XXH3_128bits_reset((XXH3_state_t*)state) == XXH_ERROR)
		throw GardenError("Failed to reset xxHash state");
}
void Hash128::updateState(State state, const void* data, psize size)
{
	GARDEN_ASSERT(state);
	if (XXH3_128bits_update((XXH3_state_t*)state, data, size) == XXH_ERROR)
		throw GardenError("Failed to update xxHash state");
}
Hash128 Hash128::digestState(State state) noexcept
{
	GARDEN_ASSERT(state);
	auto result = XXH3_128bits_digest((XXH3_state_t*)state);
	return Hash128(result.low64, result.high64);
}
Hash128::State Hash128::getState()
{
	if (!stateInstance)
		stateInstance = createState();
	return stateInstance;
}