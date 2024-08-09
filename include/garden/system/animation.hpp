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
 * @brief Object animations system.
 */

// TODO: add bezier curves support and also lerped transitions between different animations.

#pragma once
#include "garden/animate.hpp"
#include "garden/system/thread.hpp"

namespace garden
{

using namespace ecsm;
class AnimationSystem;

/***********************************************************************************************************************
 * @brief Object animations container.
 */
struct AnimationComponent final : public Component
{
	string active;               /**< Active animation path */
	float frame = 0.0f;          /**< Current animation frame */
	bool isPlaying = true;       /**< Is animation playing */
	bool randomizeStart = false; /**< Set random frame on copy/deserialization */
private:
	uint16 _alignment = 0;
	map<string, Ref<Animation>> animations;

	bool destroy();

	friend class AnimationSystem;
	friend class LinearPool<AnimationComponent>;
public:
	/**
	 * @brief Returns animations map.
	 */
	const map<string, Ref<Animation>>& getAnimations() const noexcept { return animations; }

	/**
	 * @brief Returns active animation loop state.
	 * @return True if active animation is not empty and found.
	 */
	bool getActiveLooped(bool& isLooped) const;

	/**
	 * @brief Adds a new animation to the map.
	 * 
	 * @param[in] path target animation path
	 * @param[in] animation target animation instance
	 */
	auto emplaceAnimation(string&& path, Ref<Animation>&& animation)
	{
		GARDEN_ASSERT(!path.empty());
		GARDEN_ASSERT(animation);
		return animations.emplace(std::move(path), std::move(animation));
	}

	/**
	 * @brief Removes animation from the map.
	 * @param[in] path target animation path
	 */
	psize eraseAnimation(const string& path) noexcept { return animations.erase(path); }
	/**
	 * @brief Removes animation from the map.
	 * @param i target animation iterator
	 */
	auto eraseAnimation(map<string, Ref<Animation>>::const_iterator i) noexcept { return animations.erase(i); }

	/**
	 * @brief Clears animations map.
	 */
	void clearAnimations() noexcept { animations.clear(); }
};

/***********************************************************************************************************************
 * @brief Handles object properties animation.
 */
class AnimationSystem final : public System, public ISerializable
{
	ThreadSystem* threadSystem = nullptr;
	LinearPool<AnimationComponent> components;
	LinearPool<Animation> animations;
	mt19937 randomGenerator;
	bool animateAsync = false;

	static AnimationSystem* instance;

	/**
	 * @brief Creates a new animation system instance.
	 * @param useAsync multithreaded components animation
	 */
	AnimationSystem(bool animateAsync = true);
	/**
	 * @brief Destroy animation system instance.
	 */
	~AnimationSystem() final;

	void init();
	void postDeinit();
	void update();

	ID<Component> createComponent(ID<Entity> entity) final;
	void destroyComponent(ID<Component> instance) final;
	void copyComponent(View<Component> source, View<Component> destination) final;
	const string& getComponentName() const final;
	type_index getComponentType() const final;
	View<Component> getComponent(ID<Component> instance) final;
	void disposeComponents() final;

	void serialize(ISerializer& serializer, const View<Component> component) final;
	void deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component) final;
	
	friend class ecsm::Manager;
public:
	/**
	 * @brief Does system animate components asynchronously. (From multiple threads)
	 */
	bool isAnimateAsync() const noexcept { return animateAsync; }
	/**
	 * @brief Returns animation component pool.
	 */
	const LinearPool<AnimationComponent>& getComponents() const noexcept { return components; }
	/**
	 * @brief Returns animation pool.
	 */
	const LinearPool<Animation>& getAnimations() const noexcept { return animations; }

	/**
	 * @brief Creates a new animation instance.
	 * @note Expected to use the resource system to load animations.
	 */
	ID<Animation> createAnimation() { return animations.create(); }
	/**
	 * @brief Returns animation view.
	 * @param animation target animation instance
	 */
	View<Animation> get(ID<Animation> animation) { return animations.get(animation); }
	/**
	 * @brief Returns animation view.
	 * @param animation target animation instance
	 */
	View<Animation> get(const Ref<Animation>& animation) { return animations.get(animation); }
	/**
	 * @brief Destroys animation instance.
	 * @param animation target animation instance or null
	 */
	void destroy(ID<Animation> animation) { animations.destroy(animation); }

	/**
	 * @brief Returns true if entity has animation component.
	 * @param entity target entity with component
	 * @note This function is faster than the Manager one.
	 */
	bool has(ID<Entity> entity) const;
	/**
	 * @brief Returns entity animation component view.
	 * @param entity target entity with component
	 * @note This function is faster than the Manager one.
	 */
	View<AnimationComponent> get(ID<Entity> entity) const;
	/**
	 * @brief Returns entity animation component view if exist.
	 * @param entity target entity with component
	 * @note This function is faster than the Manager one.
	 */
	View<AnimationComponent> tryGet(ID<Entity> entity) const;

	/**
	 * @brief Returns animation system instance.
	 */
	static AnimationSystem* get() noexcept
	{
		GARDEN_ASSERT(instance); // System is not created.
		return instance;
	}
};

} // namespace garden