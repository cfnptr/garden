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
 * @brief User interface trigger rendering functions. (UI, GUI)
 */

#pragma once
#include "garden/animate.hpp"

namespace garden
{

/**
 * @brief User interface element trigger data container. (UI)
 */
struct UiTriggerComponent final : public Component
{
	float2 scale = float2::one; /**< UI trigger zone scale. */
	string onEnter = "";        /**< On UI trigger cursor enter event. */
	string onExit = "";         /**< On UI trigger cursor exit event. */
	string onStay = "";         /**< On UI trigger cursor stay event. */
};

/**
 * @brief User interface element trigger animation frame container. (UI)
 */
struct UiTriggerFrame final : public AnimationFrame
{
	bool animateScale = false;
	float2 scale = float2::one;

	bool hasAnimation() final { return animateScale; }
};

/***********************************************************************************************************************
 * @brief User interface element trigger system. (UI, GUI)
 */
class UiTriggerSystem final : public CompAnimSystem<UiTriggerComponent, UiTriggerFrame, false, false>,
	public Singleton<UiTriggerSystem>, public ISerializable
{
	vector<pair<ID<Entity>, float>> newElements;
	ID<Entity> currElement = {};

	/**
	 * @brief Creates a new user interface element trigger system instance. (UI, GUI)
	 * @param setSingleton set system singleton instance
	 */
	UiTriggerSystem(bool setSingleton = true);
	/**
	 * @brief Destroys user interface element trigger system instance. (UI, GUI)
	 */
	~UiTriggerSystem() final;

	void update();

	void destroyComponent(ID<Component> instance) final;
	void resetComponent(View<Component> component, bool full) final;
	void copyComponent(View<Component> source, View<Component> destination) final;
	string_view getComponentName() const final;
	
	void serialize(ISerializer& serializer, const View<Component> component) final;
	void deserialize(IDeserializer& deserializer, View<Component> component) final;

	void serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame) final;
	ID<AnimationFrame> deserializeAnimation(IDeserializer& deserializer) final;
	void animateAsync(View<Component> component,
		View<AnimationFrame> a, View<AnimationFrame> b, float t) final;
	friend class ecsm::Manager;
public:
	/**
	 * @brief Returns current hovered with cursor UI element.
	 */
	ID<Entity> getHovered() const noexcept { return currElement; }
};

} // namespace garden