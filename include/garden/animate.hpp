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

/**
 * @brief Animation frame interpolation function.
 */
enum class AnimationFunc : uint8
{
	Linear, Pow, Gain, Count
};

/**
 * @brief Base animation frame data container.
 */
struct AnimationFrame
{
	/**
	 * @brief Interpolation function coefficient. (Depends on function type)
	 */
	float coeff = 1.0f;
	/**
	 * @brief Animation frame interpolation function type.
	 */
	AnimationFunc funcType = {};

	/**
	 * @brief Destroys animation frame data.
	 */
	virtual ~AnimationFrame() { }
};

/**
 * @brief Animatable system properties container. 
 */
using Animatables = map<System*, ID<AnimationFrame>>;

/***********************************************************************************************************************
 * @brief Base animatable system interface.
 */
class IAnimatable
{
public:
	/**
	 * @brief Serializes system animation frame data.
	 * 
	 * @param[in,out] serializer target serializer instance
	 * @param frame system animation frame view
	 */
	virtual void serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame) = 0;
	/**
	 * @brief Deserializes system animation frame data.
	 *
	 * @param[in,out] deserializer target deserializer instance
	 * @param frame system animation frame view
	 */
	virtual ID<AnimationFrame> deserializeAnimation(IDeserializer& deserializer) = 0;
	/**
	 * @brief Returns system animation frame view.
	 * @param frame target system animation frame instance
	 */
	virtual View<AnimationFrame> getAnimation(ID<AnimationFrame> frame) = 0;
	/**
	 * @brief Asynchronously animates system component data. (From multiple threads)
	 * @details Component data are interpolated between two frames.
	 * 
	 * @param component target system component view
	 * @param a source animation frame data view
	 * @param b destination frame data view
	 * @param t time coefficient (from 0.0 to 1.0)
	 */
	virtual void animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t) = 0;
	/**
	 * @brief Destroys system animation frame instance.
	 * @param frame target system animation frame instance
	 */
	virtual void destroyAnimation(ID<AnimationFrame> frame) = 0;
};

/**
 * @brief Animation keyframes container.
 */
struct Animation final
{
private:
	map<int32, Animatables> keyframes;
public:
	float frameRate = 30.0f; /**< Animation frame rate per second. */
	bool isLooped = true;    /**< Is animation played infinitely. */
private:
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;

	/**
	 * @brief Destroys animation keyframes.
	 * @details It destroys all contained system animation frames.
	 */
	bool destroy();

	friend class LinearPool<Animation>;
public:
	/**
	 * @brief Returns animation keyframes map.
	 */
	const map<int32, Animatables>& getKeyframes() const noexcept { return keyframes; }

	/**
	 * @brief Adds keyframe to the animation.
	 * 
	 * @param index target keyframe index
	 * @param[in] animatables animation frames container
	 */
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
				GARDEN_ASSERT(animatables.find(firstPair.first) != animatables.end());
		}
		#endif
		return keyframes.emplace(index, std::move(animatables));
	}

	/**
	 * @brief Removes keyframe from the animation.
	 * @warning It does not destroys keyframe system frames!
	 * @param index target keyframe index
	 */
	psize eraseKeyframe(int32 index) { return keyframes.erase(index); }
	/**
	 * @brief Removes keyframe from the animation.
	 * @warning It does not destroys keyframe system frames!
	 * @param index target keyframe iterator
	 */
	auto eraseKeyframe(map<int32, Animatables>::const_iterator i) { return keyframes.erase(i); }
	/**
	 * @brief Removes all keyframes from the animation.
	 * @warning It does not destroys keyframe system frames!
	 */
	void clearKeyframes() noexcept { keyframes.clear(); }

	/**
	 * @brief Destroys keyframe system animation frames.
	 * @warning Invalidates keyframes map!
	 */
	static void destroyKeyframes(const map<int32, Animatables>& keyframes);
};

} // namespace garden