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
 * @brief User interface scissor rendering functions. (UI, GUI)
 */

#pragma once
#include "garden/animate.hpp"

namespace garden
{

/**
 * @brief User interface element scissor data container. (UI)
 */
struct UiScissorComponent final : public Component
{
	float2 offset = float2::zero; /**< UI scissor zone offset. */
	float2 scale = float2::one;   /**< UI scissor zone scale. */
	bool useItsels = false;       /**< Use scissor on this entity itself. */
};

/**
 * @brief User interface element scissor animation frame container. (UI)
 */
struct UiScissorFrame final : public AnimationFrame
{
	uint8 _alignment = 0;
	bool animateOffset = false;
	bool animateScale = false;
	float2 offset = float2::zero;
	float2 scale = float2::one;

	bool hasAnimation() final { return animateOffset || animateScale; }
};

/***********************************************************************************************************************
 * @brief User interface element scissor system. (UI, GUI)
 */
class UiScissorSystem final : public CompAnimSystem<UiScissorComponent, UiScissorFrame, false, false>,
	public Singleton<UiScissorSystem>, public ISerializable
{
	float2 cursorPosition = float2::zero;

	/**
	 * @brief Creates a new user interface element scissor system instance. (UI, GUI)
	 * @param setSingleton set system singleton instance
	 */
	UiScissorSystem(bool setSingleton = true);
	/**
	 * @brief Destroys user interface element scissor system instance. (UI, GUI)
	 */
	~UiScissorSystem() final;

	void update();

	string_view getComponentName() const final;
	
	void serialize(ISerializer& serializer, const View<Component> component) final;
	void deserialize(IDeserializer& deserializer, View<Component> component) final;

	void serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame) final;
	void deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame) final;
	void animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t) final;

	friend class ecsm::Manager;
public:
	/**
	 * @brief Caculates UI element rendering scissor.
	 */
	int4 calcScissor(ID<Entity> entity) const noexcept;
};

} // namespace garden