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
 * @brief Color model rendering functions.
 */

#pragma once
#include "garden/system/render/model.hpp"
#include "math/simd/vector/float.hpp"

namespace garden
{

/**
 * @brief Color model rendering data container.
 */
struct ColorModelComponent final : public ModelRenderComponent
{
	f32x4 color = f32x4::one; /**< Model HDR color value. */
};
/**
 * @brief Color model animation frame container.
 */
struct ColorModelFrame final : public ModelAnimationFrame
{
	f32x4 color = f32x4::one;
	bool animateColor = false;

	bool hasAnimation() final
	{
		return ModelAnimationFrame::hasAnimation() || animateColor;
	}
};

/**
 * @brief Color model rendering system.
 */
class ColorModelSystem final : public ModelRenderCompSystem<
	ColorModelComponent, ColorModelFrame, false, false>, public Singleton<ColorModelSystem>
{
	/**
	 * @brief Creates a new color model rendering system instance.
	 * @param setSingleton set system singleton instance
	 */
	ColorModelSystem(bool setSingleton = true);
	/**
	 * @brief Destroys color model rendering system instance.
	 */
	~ColorModelSystem() final;

	void copyComponent(View<Component> source, View<Component> destination) final;
	const string& getComponentName() const final;
	MeshRenderType getMeshRenderType() const final;

	void serialize(ISerializer& serializer, const View<Component> component) final;
	void deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component) final;
	void serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame) final;
	ID<AnimationFrame> deserializeAnimation(IDeserializer& deserializer) final;
	void animateAsync(View<Component> component,
		View<AnimationFrame> a, View<AnimationFrame> b, float t) final;

	friend class ecsm::Manager;
};

} // namespace garden