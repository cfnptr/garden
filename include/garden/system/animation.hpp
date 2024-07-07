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
 * @brief Object animation system.
 */

// TODO: add bezier curves support and also lerped transitions between different animations.

#pragma once
#include "garden/animate.hpp"
#include "garden/system/thread.hpp"

namespace garden
{

using namespace ecsm;
class AnimationSystem;

//**********************************************************************************************************************
struct AnimationComponent final : public Component
{
private:
	map<string, Ref<Animation>> animations;
public:
	string active;
	float frame = 0.0f;
	bool isPlaying = true;
private:
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	friend class AnimationSystem;
public:
	const map<string, Ref<Animation>>& getAnimations() const noexcept { return animations; }

	auto emplaceAnimation(string&& path, Ref<Animation>&& animation)
	{
		GARDEN_ASSERT(!path.empty());
		GARDEN_ASSERT(animation);
		return animations.emplace(std::move(path), std::move(animation));
	}

	psize eraseAnimation(const string& path) noexcept { return animations.erase(path); }
	auto eraseAnimation(map<string, Ref<Animation>>::const_iterator i) noexcept { return animations.erase(i); }
	void clearAnimations() noexcept { animations.clear(); }
};

//**********************************************************************************************************************
class AnimationSystem final : public System, public ISerializable
{
	ThreadSystem* threadSystem = nullptr;
	LinearPool<AnimationComponent, false> components;
	LinearPool<Animation> animations;
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
	void update();

	ID<Component> createComponent(ID<Entity> entity) final;
	void destroyComponent(ID<Component> instance) final;
	void copyComponent(View<Component> source, View<Component> destination) final;
	const string& getComponentName() const final;
	type_index getComponentType() const final;
	View<Component> getComponent(ID<Component> instance) final;
	void disposeComponents() final;

	void serialize(ISerializer& serializer, ID<Entity> entity, View<Component> component) final;
	void deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component) final;
	
	friend class ecsm::Manager;
public:
	bool isAnimateAsync() const noexcept { return animateAsync; }
	ID<Animation> createAnimation() { return animations.create(); }
	View<Animation> get(ID<Animation> animation) { return animations.get(animation); }
	void destroy(ID<Animation> animation) { animations.destroy(animation); }

	static void tryDestroyResources(View<AnimationComponent> animationComponent);

	/**
	 * @brief Returns animation system instance.
	 */
	static AnimationSystem* getInstance() noexcept
	{
		GARDEN_ASSERT(instance); // System is not created.
		return instance;
	}
};

} // namespace garden