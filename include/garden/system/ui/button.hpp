// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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
 * @brief User interface button rendering functions. (UI, GUI)
 */

#pragma once
#include "garden/animate.hpp"

namespace garden
{

class UiButtonSystem;

/**
 * @brief User interface button element data container. (UI)
 */
struct UiButtonComponent final : public Component
{
protected:
	uint8 _alignment = 0;
	bool enabled = true;

	friend class garden::UiButtonSystem;
public:
	bool noCursorHand = false; /**< Disables cursor change on button hover. */
	string onClick = "";       /**< On UI button click event. */
	string animationPath = ""; /**< UI button state animation path. */

	/**
	 * @brief Returns true if UI button is enabled.
	 */
	bool isEnabled() const noexcept { return enabled; }
	/**
	 * @brief Sets UI button enabled state.
	 * @param state target button state
	 */
	void setEnabled(bool state);
};

/**
 * @brief User interface button element animation frame container. (UI)
 */
struct UiButtonFrame final : public AnimationFrame
{
	uint8 animateIsEnabled : 1;
	uint8 animateNoCursorHand : 1;
	uint8 animateOnClick : 1;
	uint8 animateAnimationPath : 1;
	uint8 isEnabled : 1;
	uint8 noCursorHand : 1;
	uint16 _alignment = 0;
	string onClick = "";
	string animationPath = "";

	UiButtonFrame() noexcept : animateIsEnabled(false), animateOnClick(false), animateAnimationPath(false), 
		animateNoCursorHand(false), isEnabled(true), noCursorHand(false) { }
	
	bool hasAnimation() final
	{
		return animateIsEnabled || animateOnClick || animateAnimationPath || animateNoCursorHand;
	}
};

/***********************************************************************************************************************
 * @brief User interface button element system. (UI, GUI)
 */
class UiButtonSystem final : public CompAnimSystem<UiButtonComponent, UiButtonFrame, false, false>,
	public Singleton<UiButtonSystem>, public ISerializable
{
	ID<Entity> pressedButton = {};

	/**
	 * @brief Creates a new user interface button element system instance. (UI, GUI)
	 * @param setSingleton set system singleton instance
	 */
	UiButtonSystem(bool setSingleton = true);
	/**
	 * @brief Destroys user interface button element system instance. (UI, GUI)
	 */
	~UiButtonSystem() final;

	void uiButtonEnter();
	void uiButtonExit();
	void uiButtonStay();

	string_view getComponentName() const final;
	
	void serialize(ISerializer& serializer, const View<Component> component) final;
	void deserialize(IDeserializer& deserializer, View<Component> component) final;

	void serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame) final;
	void deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame) final;
	void animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t) final;

	friend class ecsm::Manager;
};

} // namespace garden