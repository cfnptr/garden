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
 * @brief User interface transform rendering functions. (UI, GUI)
 */

#pragma once
#include "garden/animate.hpp"

namespace garden
{

/**
 * @brief User interface element position relative point on the screen. (UI)
 */
enum class UiAnchor : uint8
{
	Center,      /**< Aligns interface element to the center of the screen. */
	Left,        /**< Aligns interface element to the left side of the screen. */
	Right,       /**< Aligns interface element to the right side of the screen. */
	Bottom,      /**< Aligns interface element to the bottom side of the screen. */
	Top,         /**< Aligns interface element to the top side of the screen. */
	LeftBottom,  /**< Aligns interface element to the left bottom corner of the screen. */
	LeftTop,     /**< Aligns interface element to the left top corner of the screen. */
	RightBottom, /**< Aligns interface element to the right bottom corner of the screen. */
	RightTop,    /**< Aligns interface element to the right top corner of the screen. */
	Background,  /**< Aligns interface element to the full screen. */
	Count        /**< User interface alignment anchor type count. */
};

/**
 * @brief User interface element anchor names.
 */
constexpr const char* uiAnchorNames[(psize)UiAnchor::Count] =
{
	"Center", "Left", "Right", "Bottom", "Top", "LeftBottom", "LeftTop", "RightBottom", "RightTop", "Background"
};
/**
 * @brief Returns UI element anchor name string.
 * @param uiAnchor target UI anchor type
 */
static string_view toString(UiAnchor uiAnchor) noexcept
{
	GARDEN_ASSERT(uiAnchor < UiAnchor::Count);
	return uiAnchorNames[(psize)uiAnchor];
}
/**
 * @brief Returns UI element anchor from name string.
 *
 * @param name target UI anchor name string
 * @param[out] uiAnchor UI anchor type
 */
static bool toUiAnchor(string_view name, UiAnchor& uiAnchor) noexcept
{
	if (name == "Center") { uiAnchor = UiAnchor::Center; return true; }
	if (name == "Left") { uiAnchor = UiAnchor::Left; return true; }
	if (name == "Right") { uiAnchor = UiAnchor::Right; return true; }
	if (name == "Bottom") { uiAnchor = UiAnchor::Bottom; return true; }
	if (name == "Top") { uiAnchor = UiAnchor::Top; return true; }
	if (name == "LeftBottom") { uiAnchor = UiAnchor::LeftBottom; return true; }
	if (name == "LeftTop") { uiAnchor = UiAnchor::LeftTop; return true; }
	if (name == "RightBottom") { uiAnchor = UiAnchor::RightBottom; return true; }
	if (name == "RightTop") { uiAnchor = UiAnchor::RightTop; return true; }
	if (name == "Background") { uiAnchor = UiAnchor::Background; return true; }
	return false;
}

/***********************************************************************************************************************
 * @brief User interface element transformation data container. (UI)
 */
struct UiTransformComponent final : public Component
{
	float3 position = float3::zero;
	float3 scale = float3::one;
	UiAnchor anchor = UiAnchor::Center;
	quat rotation = quat::identity;
};

/**
 * @brief User interface element transform animation frame container. (UI)
 */
struct UiTransformFrame final : public AnimationFrame
{
	uint8 animatePosition : 1;
	uint8 animateScale : 1;
	uint8 animateRotation : 1;
	uint8 animateAnchor : 1;
	UiAnchor anchor = UiAnchor::Center;
	f32x4 position = f32x4::zero;
	f32x4 scale = f32x4::one;
	quat rotation = quat::identity;

	UiTransformFrame() : animatePosition(false), animateScale(false), 
		animateRotation(false), animateAnchor(false) { }

	bool hasAnimation() final
	{
		return animatePosition || animateScale || animateRotation || animateAnchor;
	}
};

/***********************************************************************************************************************
 * @brief User interface element transformation system. (UI, GUI)
 */
class UiTransformSystem final : public CompAnimSystem<UiTransformComponent, UiTransformFrame, false, false>,
	public Singleton<UiTransformSystem>, public ISerializable
{
	/**
	 * @brief Creates a new user interface element transformation system instance. (UI, GUI)
	 * @param setSingleton set system singleton instance
	 */
	UiTransformSystem(bool setSingleton = true);
	/**
	 * @brief Destroys user interface element transformation system instance. (UI, GUI)
	 */
	~UiTransformSystem() final;

	void update();

	string_view getComponentName() const final;
	
	void serialize(ISerializer& serializer, const View<Component> component) final;
	void deserialize(IDeserializer& deserializer, View<Component> component) final;

	void serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame) final;
	void deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame) final;
	void animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t) final;
	friend class ecsm::Manager;
public:
	float uiScale = 1.0f;  /**< User interface scaling factor. (UI) */

	/**
	 * @brief Calculates size in user interface coordinates.
	 */
	float2 calcUiSize() const;
};

} // namespace garden