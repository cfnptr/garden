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
#include "garden/system/text.hpp"
#include "garden/animate.hpp"

namespace garden
{

class UiLabelSystem;

/**
 * @brief User interface label element data container. (UI)
 */
struct UiLabelComponent final : public Component
{
private:
	ID<Text> text = {};
	friend class garden::UiLabelSystem;
public:
	#if GARDEN_DEBUG || GARDEN_EDITOR
	vector<fs::path> fontPaths; /**< Text font paths. */
	#endif

	string value = "";                 /**> UI label text string. */
	Text::Properties propterties = {}; /**< UI label text properties. */
	uint32 fontSize = 16;              /**< Text font size in pixels.*/

	#if GARDEN_DEBUG || GARDEN_EDITOR
	bool loadNoto = true; /**< Also load supporting noto fonts. */
	#endif
	
	/**
	 * @brief Returns UI label text instance.
	 */
	ID<Text> getText() const noexcept { return text; }

	/**
	 * @brief Regenerates text internal data.
	 * @return True on success, otherwise false.
	 * @param shrink reduce internal memory usage
	 */
	bool updateText(bool shrink = false);
};

/**
 * @brief User interface label element animation frame container. (UI)
 */
struct UiLabelFrame final : public AnimationFrame
{
	#if GARDEN_DEBUG || GARDEN_EDITOR
	bool loadNoto = true;
	vector<fs::path> fontPaths;
	#endif

	string value = "";
	Text::Properties propterties = {};
	uint32 fontSize = 16;
	ID<Text> text = {};

	bool hasAnimation() final { return true; }
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
	void deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame) final;
	void animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t) final;
	void resetAnimation(View<AnimationFrame> frame, bool full) override;
	friend class ecsm::Manager;
};

} // namespace garden