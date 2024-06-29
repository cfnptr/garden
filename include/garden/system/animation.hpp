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

#pragma once
#include "garden/defines.hpp"
#include "garden/animate.hpp"

namespace garden
{

using namespace ecsm;

struct AnimationComponent final : public Component
{
	map<string, Ref<Animation>> animations;
	string active;
	uint32 frame = 0;
	bool isPlaying = true;
private:
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
};

class AnimationSystem final : public System, public ISerializable
{
private:
	LinearPool<AnimationComponent, false> components;
	LinearPool<Animation> animations;

	static AnimationSystem* instance;

	/**
	 * @brief Creates a new animation system instance.
	 */
	AnimationSystem();
	/**
	 * @brief Destroy animation system instance.
	 */
	~AnimationSystem() final;

	ID<Component> createComponent(ID<Entity> entity) final;
	void destroyComponent(ID<Component> instance) final;
	void copyComponent(ID<Component> source, ID<Component> destination) final;

	void serialize(ISerializer& serializer, ID<Entity> entity, View<Component> component) final;
	void deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component) final;
	
	friend class ecsm::Manager;
public:
	ID<Animation> createAnimation() { return animations.create(); }
	View<Animation> get(ID<Animation> animation) { return animations.get(animation); }
	void destroy(ID<Animation> animation) { animations.destroy(animation); }

	const string& getComponentName() const final;
	type_index getComponentType() const final;
	View<Component> getComponent(ID<Component> instance) final;
	void disposeComponents() final;

	void tryDestroyResources(View<AnimationComponent> animationComponent);

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