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
#include "math/types.hpp"
#include "garden/file-base64.hpp"

#include <string>
#include <cstring>

namespace garden
{

using namespace std;
using namespace math;

//--------------------------------------------------------------------------------------------------
struct Hash128
{
	uint64 low64 = 0;
	uint64 high64 = 0;

	Hash128(uint64 low64 = 0, uint64 high64 = 0) noexcept {
		this->low64 = low64; this->high64 = high64; }
	bool operator==(const Hash128& h) const noexcept { 
		return low64 == h.low64 && high64 == h.high64; }
	bool operator!=(const Hash128& h) const noexcept {
		return low64 != h.low64 || high64 != h.high64; }
	bool operator<(const Hash128& h) const noexcept { 
		return memcmp(this, &h, sizeof(uint64) * 2) < 0; }
	
	string toString() const noexcept
	{
		psize length = 0;
		auto base64 = base64_encode((const uint8*)this, sizeof(Hash128), &length);
		string value((char*)base64, length - 1);
		free(base64);
  		return value;
	}
};

} // garden