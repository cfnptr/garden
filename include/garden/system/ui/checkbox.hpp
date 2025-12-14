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
#include "garden/system/ui/button.hpp"

namespace garden
{

class UiCheckboxSystem;

/**
 * @brief User interface checkbox element data container. (UI)
 */
struct UiCheckboxComponent final : public Component
{
protected:
	bool enabled = true;
	bool checked = false;

	friend class garden::UiCheckboxSystem;
public:
	string onChange = "";      /**< On UI checkbox state change event. */
	string animationPath = ""; /**< UI checkbox state animation path. */

	/**
	 * @brief Returns true if UI checkbox is enabled.
	 */
	bool isEnabled() const noexcept { return enabled; }
	/**
	 * @brief Sets UI checkbox enabled state.
	 * @param state target checkbox state
	 */
	void setEnabled(bool state);

	/**
	 * @brief Returns true if UI checkbox is checked. (set)
	 */
	bool isChecked() const noexcept { return checked; }
	/**
	 * @brief Sets UI checkbox state.
	 * @param state target checkbox state
	 */
	void setChecked(bool state);
};

/**
 * @brief User interface checkbox element animation frame container. (UI)
 */
struct UiCheckboxFrame final : public AnimationFrame
{
	uint8 animateIsEnabled : 1;
	uint8 animateIsChecked : 1;
	uint8 animateOnChange : 1;
	uint8 animateAnimationPath : 1;
	uint8 isEnabled : 1;
	uint8 isChecked : 1;
	uint16 _alignment = 0;
	string onChange = "";
	string animationPath = "";

	UiCheckboxFrame() noexcept : animateIsEnabled(false), animateIsChecked(false), 
		animateOnChange(false), animateAnimationPath(false), isEnabled(true), isChecked(false) { }

	bool hasAnimation() final
	{
		return animateIsEnabled || animateIsChecked || animateOnChange || animateAnimationPath;
	}
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

	void uiCheckboxClick();

	string_view getComponentName() const final;
	
	void serialize(ISerializer& serializer, const View<Component> component) final;
	void deserialize(IDeserializer& deserializer, View<Component> component) final;

	void serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame) final;
	void deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame) final;
	void animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t) final;

	friend class ecsm::Manager;
};

} // namespace garden