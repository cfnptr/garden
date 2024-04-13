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

#include "garden/hash.hpp"
#include "garden/base64.hpp"

#define XXH_INLINE_ALL
#define XXH_STATIC_LINKING_ONLY
#if GARDEN_DEBUG
#define XXH_DEBUGLEVEL 1
#endif
#include "xxhash.h"

using namespace garden;

//*********************************************************************************************************************
Hash128::Hash128(const void* data, psize size, Hash128::State state)
{
	if (state)
	{
		auto hashState = (XXH3_state_t*)state;
		if (XXH3_128bits_reset(hashState) == XXH_ERROR)
			throw runtime_error("Failed to reset xxHash state");
		if (XXH3_128bits_update(hashState, data, size) == XXH_ERROR)
			throw runtime_error("Failed to update xxHash state");
		auto hashResult = XXH3_128bits_digest(hashState);
		this->low64 = hashResult.low64; this->high64 = hashResult.high64;
	}
	else
	{
		auto hashResult = XXH3_128bits(data, size);
		this->low64 = hashResult.low64; this->high64 = hashResult.high64;
	}
}
	
string Hash128::toString() const
{
	psize length = 0;
	auto base64 = base64_encode((const uint8*)this, sizeof(Hash128), &length);
	string value((char*)base64, length - 1);
	free(base64);
	return value;
}

//*********************************************************************************************************************
Hash128::State Hash128::createState()
{
	return XXH3_createState();
}
void Hash128::destroyState(Hash128::State state)
{
	GARDEN_ASSERT(state);
	XXH3_freeState((XXH3_state_t*)state);
}