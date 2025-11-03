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
 * @brief User interface input rendering functions. (UI, GUI)
 */

#pragma once
#include "garden/animate.hpp"

namespace garden
{

class UiInputSystem;

/**
 * @brief User interface input element data container. (UI)
 */
struct UiInputComponent final : public Component
{
protected:
	uint16 _alignment = 0;
	bool enabled = true;

	friend class garden::UiInputSystem;
public:
	string onChange = "";      /**< On UI input change event. */
	string animationPath = ""; /**< UI input state animation path. */

	/**
	 * @brief Returns true if UI input is enabled.
	 */
	bool isEnabled() const noexcept { return enabled; }
	/**
	 * @brief Sets UI input enabled state.
	 * @param state target input state
	 */
	void setEnabled(bool state);
};

/**
 * @brief User interface input element animation frame container. (UI)
 */
struct UiInputFrame final : public AnimationFrame
{
	uint8 animateIsEnabled : 1;
	uint8 animateOnChange : 1;
	uint8 animateAnimationPath : 1;
	uint8 isEnabled : 1;
	uint16 _alignment = 0;
	string onChange = "";
	string animationPath = "";

	UiInputFrame() : animateIsEnabled(false), animateOnChange(false), 
		animateAnimationPath(false), isEnabled(true) { }
	bool hasAnimation() final { return animateIsEnabled || animateOnChange || animateAnimationPath; }
};

/***********************************************************************************************************************
 * @brief User interface input element system. (UI, GUI)
 */
class UiInputSystem final : public CompAnimSystem<UiInputComponent, UiInputFrame, false, false>,
	public Singleton<UiInputSystem>, public ISerializable
{
	ID<Entity> activeInput = {};

	/**
	 * @brief Creates a new user interface input element system instance. (UI, GUI)
	 * @param setSingleton set system singleton instance
	 */
	UiInputSystem(bool setSingleton = true);
	/**
	 * @brief Destroys user interface input element system instance. (UI, GUI)
	 */
	~UiInputSystem() final;

	void uiInputEnter();
	void uiInputExit();
	void uiInputStay();
	void update();

	string_view getComponentName() const final;
	
	void serialize(ISerializer& serializer, const View<Component> component) final;
	void deserialize(IDeserializer& deserializer, View<Component> component) final;

	void serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame) final;
	void deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame) final;
	void animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t) final;

	friend class ecsm::Manager;
};

} // namespace garden