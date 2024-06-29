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
 * @brief Common animation functions.
 */

#pragma once
#include "garden/serialize.hpp"

namespace garden
{

using namespace ecsm;

struct AnimationFrame
{
	virtual ~AnimationFrame() { }
};

using Animatables = map<System*, ID<AnimationFrame>>;

struct Animation final
{
	map<uint32, Animatables> keyframes;
	float frameRate = 30.0f;
	bool loop = true;
private:
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;

	bool destroy();
	friend class LinearPool<Animation>;
};

class IAnimatable
{
public:
	virtual void serializeAnimation(ISerializer& serializer, ID<AnimationFrame> frame) = 0;
	virtual ID<AnimationFrame> deserializeAnimation(IDeserializer& deserializer) = 0;
	virtual void destroyAnimation(ID<AnimationFrame> frame) = 0;
};

} // namespace garden