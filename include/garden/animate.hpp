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
#include "garden/defines.hpp"
#include "garden/serialize.hpp"

namespace garden
{

using namespace ecsm;

enum class AnimationFunc : uint8
{
	Linear, Pow, Gain, Count
};

struct AnimationFrame
{
	float coeff = 0.0f;
	AnimationFunc funType = {}; // TODO: read these values in the animation system, add getAnimation function.

	virtual ~AnimationFrame() { }
};

using Animatables = map<System*, ID<AnimationFrame>>;

class IAnimatable
{
public:
	virtual void serializeAnimation(ISerializer& serializer, ID<AnimationFrame> frame) = 0;
	virtual ID<AnimationFrame> deserializeAnimation(IDeserializer& deserializer) = 0;
	virtual void animateAsync(ID<Entity> entity, ID<AnimationFrame> a, ID<AnimationFrame> b, float t) = 0;
	virtual void destroyAnimation(ID<AnimationFrame> frame) = 0;
};

struct Animation final
{
private:
	map<int32, Animatables> keyframes;
public:
	float frameRate = 30.0f;
	bool loop = true;
private:
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;

	bool destroy();
	friend class LinearPool<Animation>;
public:
	const map<int32, Animatables>& getKeyframes() const noexcept { return keyframes; }

	auto emplaceKeyframe(int32 index, Animatables&& animatables)
	{
		#if GARDEN_DEBUG
		GARDEN_ASSERT(!animatables.empty());
		for (const auto& pair : animatables)
		{
			GARDEN_ASSERT(dynamic_cast<IAnimatable*>(pair.first));
			GARDEN_ASSERT(pair.second);
		}
		auto firstKeyframe = keyframes.begin();
		if (firstKeyframe != keyframes.end())
		{
			for (const auto& firstPair : firstKeyframe->second)
			{
				GARDEN_ASSERT(animatables.find(firstPair.first) != animatables.end());
			}
		}
		#endif
		return keyframes.emplace(index, std::move(animatables));
	}

	psize eraseKeyframe(int32 index) { return keyframes.erase(index); }
	auto eraseKeyframe(map<int32, Animatables>::const_iterator i) { return keyframes.erase(i); }
	void clearKeyframes() noexcept { keyframes.clear(); }
};

} // namespace garden