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

/***********************************************************************************************************************
 * @file
 * @brief Common animation functions.
 */

#pragma once
#include "garden/serialize.hpp"
#include <map>

namespace garden
{

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
	float coeff = 1.0f;          /**< Interpolation function coefficient. (Depends on function type) */
	AnimationFunc funcType = {}; /**< Animation frame interpolation function type. */

	/**
	 * @brief Destroys animation frame data.
	 */
	virtual ~AnimationFrame() { }
	/**
	 * @brief Returns true if frame has any animated properties.
	 */
	virtual bool hasAnimation() { return false; }
};

/**
 * @brief Animatable system properties container. 
 */
using Animatables = tsl::robin_map<System*, ID<AnimationFrame>>;

/***********************************************************************************************************************
 * @brief Base animatable system interface.
 */
class IAnimatable
{
public:
	/**
	 * @brief Creates a new system animation frame instance.
	 */
	virtual ID<AnimationFrame> createAnimation() = 0;
	/**
	 * @brief Destroys system animation frame instance.
	 * @param instance target system animation frame instance
	 */
	virtual void destroyAnimation(ID<AnimationFrame> instance) = 0;
	/**
	 * @brief Resets system animation frame data.
	 * @param frame target system animation frame view
	 * @param full reset all animation frame data
	 */
	virtual void resetAnimation(View<AnimationFrame> frame, bool full) = 0;
	/**
	 * @brief Returns system animation frame view.
	 * @param instance target system animation frame instance
	 */
	virtual View<AnimationFrame> getAnimation(ID<AnimationFrame> instance) = 0;

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
	virtual void deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame) = 0;

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
};

/***********************************************************************************************************************
 * @brief Animation keyframes container.
 */
struct Animation final
{
public:
	using Keyframes = map<int32, Animatables>;
private:
	Keyframes keyframes;

	/**
	 * @brief Destroys animation keyframes.
	 * @details It destroys all contained system animation frames.
	 */
	bool destroy();

	friend class LinearPool<Animation>;
public:
	float frameRate = 30.0f;        /**< Animation frame rate per second. */
	bool isLooped = true;           /**< Is animation played infinitely. */

	/**
	 * @brief Returns animation keyframes map.
	 */
	const Keyframes& getKeyframes() const noexcept { return keyframes; }

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
			GARDEN_ASSERT_MSG(dynamic_cast<IAnimatable*>(pair.first), "Not an IAnimatable system");
			GARDEN_ASSERT_MSG(pair.second, "Animation frame is null");
		}
		auto firstKeyframe = keyframes.begin();
		if (firstKeyframe != keyframes.end())
		{
			for (const auto& keyframePair : firstKeyframe->second)
				GARDEN_ASSERT(animatables.find(keyframePair.first) != animatables.end());
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
	auto eraseKeyframe(Keyframes::const_iterator i) { return keyframes.erase(i); }

	/**
	 * @brief Removes all keyframes from the animation.
	 * @warning It does not destroys keyframe system frames!
	 */
	void clearKeyframes() noexcept { keyframes.clear(); }

	/**
	 * @brief Destroys keyframe system animation frames.
	 * @warning Invalidates keyframes map!
	 */
	static void destroyKeyframes(const Keyframes& keyframes);
};

/***********************************************************************************************************************
 * @brief Base system class with components and animation frames.
 * @details See the @ref System.
 *
 * @tparam C type of the system component
 * @tparam F type of the system animation frame
 *
 * @tparam DestroyComponents system should call destroy() function of the components
 * @tparam DestroyAnimationFrames system should call destroy() function of the animation frames
 */
template<class C = Component, class F = AnimationFrame, 
	bool DestroyComponents = true, bool DestroyAnimationFrames = true>
class CompAnimSystem : public ComponentSystem<C, DestroyComponents>, public IAnimatable
{
public:
	typedef F AnimationFrameType; /**< Type of the system animation frame. */
	using AnimFramePool = LinearPool<F, DestroyAnimationFrames>; /**< System animation frame pool type. */
protected:
	AnimFramePool animationFrames; /**< System animation frame pool. */

	/**
	 * @brief Creates a new system animation frame instance.
	 */
	ID<AnimationFrame> createAnimation() override
	{
		return ID<AnimationFrame>(animationFrames.create());
	}
	/**
	 * @brief Destroys system animation frame instance.
	 * @details You should use @ref AnimationSystem to destroy animation frames.
	 */
	void destroyAnimation(ID<AnimationFrame> instance) override
	{
		auto frame = animationFrames.get(ID<F>(instance));
		resetAnimation(View<AnimationFrame>(frame), false);
		animationFrames.destroy(ID<F>(instance));
	}
	/**
	 * @brief Resets system animation frame data.
	 * @details You should use @ref AnimationSystem to destroy animation frames.
	 */
	void resetAnimation(View<AnimationFrame> frame, bool full) override
	{
		if (full)
			**View<F>(frame) = F();
	}
public:
	/**
	 * @brief Returns system animation frame pool.
	 */
	const AnimFramePool& getAnimationFrames() const noexcept { return animationFrames; }

	/**
	 * @brief Returns system animation frame view.
	 * @param instance target system animation frame instance
	 */
	View<AnimationFrame> getAnimation(ID<AnimationFrame> instance) override
	{
		return View<AnimationFrame>(animationFrames.get(ID<F>(instance)));
	}
	/**
	 * @brief Actually destroys system components and animation frames.
	 * @details Items not destroyed immediately, only after the dispose call.
	 */
	void disposeComponents() override
	{
		ComponentSystem<C, DestroyComponents>::disposeComponents();
		animationFrames.dispose();
	}
};

} // namespace garden