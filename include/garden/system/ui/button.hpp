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
	bool enabled = true;
	friend class garden::UiButtonSystem;
public:
	string onClick = ""; /**< On UI button click event. */

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
	bool animateIsEnabled = false;
	bool animateOnClick = false;
	bool isEnabled = true;
	string onClick = "";

	bool hasAnimation() final { return animateIsEnabled || animateOnClick; }
};

/***********************************************************************************************************************
 * @brief User interface button element system. (UI, GUI)
 */
class UiButtonSystem final : public CompAnimSystem<UiButtonComponent, UiButtonFrame, false, false>,
	public Singleton<UiButtonSystem>, public ISerializable
{
public:
	static constexpr uint8 defaultChildIndex = 0;  /**< Default button state child entity index. */
	static constexpr uint8 hoveredChildIndex = 1;  /**< Hovered button state child entity index. */
	static constexpr uint8 activeChildIndex = 2;   /**< Active button state child entity index. */
	static constexpr uint8 disabledChildIndex = 3; /**< Disabled button state child entity index. */
	static constexpr uint8 focusedChildIndex = 4;  /**< Focused button state child entity index. */
	static constexpr uint8 stateChildCount = 5;    /**< Button state child entity count. */
private:
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