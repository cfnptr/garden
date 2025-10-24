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
 * @brief User interface label rendering functions. (UI, GUI)
 */

#pragma once
#include "garden/animate.hpp"

namespace garden
{

class UiLabelSystem;

/**
 * @brief User interface label element data container. (UI)
 */
struct UiLabelComponent : public Component
{
	string value;
public:
	/**
	 * @brief Returns label text string value.
	 */
	const string& getValue() const noexcept { return value; }
	/**
	 * @brief Sets label text string value.
	 * @param state target label text string
	 */
	void setValue(string_view value);
};

/**
 * @brief User interface label element animation frame container. (UI)
 */
struct UiLabelFrame : public AnimationFrame
{
	bool animateValue = false;
	string value;

	bool hasAnimation() final { return animateValue; }
};

/***********************************************************************************************************************
 * @brief User interface label element system. (UI, GUI)
 */
class UiLabelSystem final : public CompAnimSystem<UiLabelComponent, UiLabelFrame, false, false>,
	public Singleton<UiLabelSystem>, public ISerializable
{
	/**
	 * @brief Creates a new user interface label element system instance. (UI, GUI)
	 * @param setSingleton set system singleton instance
	 */
	UiLabelSystem(bool setSingleton = true);
	/**
	 * @brief Destroys user interface label element system instance. (UI, GUI)
	 */
	~UiLabelSystem() final;

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