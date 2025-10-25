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
 * @brief User interface checkbox rendering functions. (UI, GUI)
 */

#pragma once
#include "garden/animate.hpp"

namespace garden
{

/**
 * @brief User interface checkbox element data container. (UI)
 */
struct UiCheckboxComponent final : public Component
{
	bool isEnabled = true;  /**< Is UI checkbox enabled. */
	bool isChecked = false; /**< Is UI checkbox checked (set). */
};

/**
 * @brief User interface checkbox element animation frame container. (UI)
 */
struct UiCheckboxFrame final : public AnimationFrame
{
	bool animateIsEnabled = false;
	bool animateIsChecked = false;
	bool isEnabled = true;
	bool isChecked = false;

	bool hasAnimation() final { return animateIsEnabled || animateIsChecked; }
};

/***********************************************************************************************************************
 * @brief User interface checkbox element system. (UI, GUI)
 */
class UiCheckboxSystem final : public CompAnimSystem<UiCheckboxComponent, UiCheckboxFrame, false, false>,
	public Singleton<UiCheckboxSystem>, public ISerializable
{
	/**
	 * @brief Creates a new user interface checkbox element system instance. (UI, GUI)
	 * @param setSingleton set system singleton instance
	 */
	UiCheckboxSystem(bool setSingleton = true);
	/**
	 * @brief Destroys user interface checkbox element system instance. (UI, GUI)
	 */
	~UiCheckboxSystem() final;

	void update();

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
};

} // namespace garden