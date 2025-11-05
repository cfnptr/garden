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
	bool enabled = true;
	bool textBad = false;
	uint8 _alignment = 0;
	psize caretIndex = 0;

	friend class garden::UiInputSystem;
public:
	u32string text = U"";           /**< UI input text string. */
	u32string placeholder = U"";    /**< UI input placeholder string. (Filler) */
	u32string prefix = U"";         /**< UI input prefix string. [string = prefix + text] */
	string onChange = "";           /**< On UI input change event. */
	string animationPath = "";      /**< UI input state animation path. */
	f32x4 textColor = f32x4::one;   /**< UI input text sRGB color. */
	f32x4 placeholderColor = f32x4(0.5f, 0.5f, 0.5f, 1.0f); /**< UI input placeholder sRGB color. */

	/**
	 * @brief Returns true if UI input is enabled.
	 */
	bool isEnabled() const noexcept { return enabled; }
	/**
	 * @brief Sets UI input enabled state.
	 * @param state target input state
	 */
	void setEnabled(bool state);

	/**
	 * @brief Returns true if UI input text is bad. (Invalid)
	 */
	bool isTextBad() const noexcept { return textBad; }
	/**
	 * @brief Sets UI input text bad state. (Invalid)
	 * @param state target input text state
	 */
	void setTextBad(bool state);

	/**
	 * @brief Updates UI input field label text.
	 * @return True on success, otherwise false.
	 * @param shrink reduce internal memory usage
	 */
	bool updateText(bool shrink = false);

	/**
	 * @brief Updates UI input caret (cursor).
	 * @return True on success, otherwise false.
	 * @param charIndex target text char index or SIZE_MAX
	 */
	bool updateCaret(psize charIndex = SIZE_MAX);
	/**
	 * @brief Hides UI input caret (cursor).
	 * @return True on success, otherwise false.
	 */
	bool hideCaret();

	/**
	 * @brief Returns UI input caret text char index.
	 */
	psize getCaretIndex() const noexcept { return caretIndex; }
};

/**
 * @brief User interface input element animation frame container. (UI)
 */
struct UiInputFrame final : public AnimationFrame
{
	bool animateIsEnabled = false;
	bool isEnabled = true;

	bool hasAnimation() final { return animateIsEnabled; }
};

/***********************************************************************************************************************
 * @brief User interface input element system. (UI, GUI)
 */
class UiInputSystem final : public CompAnimSystem<UiInputComponent, UiInputFrame, false, false>,
	public Singleton<UiInputSystem>, public ISerializable
{
	ID<Entity> activeInput = {};
	double repeatTime = 0.0;

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

	void updateActive();
	void update();

	string_view getComponentName() const final;
	
	void serialize(ISerializer& serializer, const View<Component> component) final;
	void deserialize(IDeserializer& deserializer, View<Component> component) final;

	void serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame) final;
	void deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame) final;
	void animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t) final;

	friend class ecsm::Manager;
public:
	float repeatDelay = 0.5f; /**< Time until starting to repeat pressed keys in seconds. */
	float repeatSpeed = 0.05f; /**< Pressed keys repeat delay speed in seconds. */

	/**
	 * @brief Returns active UI input instance.
	 */
	ID<Entity> getActiveInput() const noexcept { return activeInput; }
};

} // namespace garden