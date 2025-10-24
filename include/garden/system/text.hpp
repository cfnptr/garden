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
 * @brief Entity animation functions.
 */

#pragma once
#include "garden/font.hpp"
#include "garden/graphics/descriptor-set.hpp"
#include "ecsm.hpp"

namespace garden
{

using namespace ecsm;
using namespace garden::graphics;

class ResourceSystem;

/**
 * @brief Text glyph data container.
 */
struct Glyph final
{
	f32x4 position;
	f32x4 texCoords;
	float advance;
	uint32 value;
	bool isVisible;
};

/**
 * @brief Font texture atlas container.
 */
struct FontAtlas
{
	vector<vector<Font>> fonts;
	tsl::robin_map<uint32, Glyph> glyphs;
	ID<Image> image = {};
	ID<DescriptorSet> descriptorSet = {};
	uint32_t fontSize = 0;
	float newLineAdvance = 0.0f;
	bool isGenerated = false;
};

/**
 * @brief Text data container.
 */
struct Text final
{
	u32string string;
	float2 size = float2::zero;
	ID<FontAtlas> fontAtlas = {};
	Color color = Color::white;
	bool isBold = false;
	bool isItalic = false;
	bool useTags = false;
	bool isConstant = false;

	bool destroy();
};

/***********************************************************************************************************************
 * @brief Handles text mesh generation, usage.
 */
class TextSystem final : public System, public Singleton<TextSystem>
{
public:
	using FontPool = LinearPool<Font>;
	using TextPool = LinearPool<Text>;
private:
	FontPool fonts;
	TextPool texts;
	void* ftLibrary = nullptr;

	/**
	 * @brief Creates a new text system instance.
	 * @param setSingleton set system singleton instance
	 */
	TextSystem(bool setSingleton = true);
	/**
	 * @brief Destroys text system instance.
	 */
	~TextSystem() final;

	void update();

	friend class ecsm::Manager;
	friend class garden::ResourceSystem;
public:
	/**
	 * @brief Returns font pool.
	 */
	const FontPool& getFonts() const noexcept { return fonts; }
	/**
	 * @brief Returns text pool.
	 */
	const TextPool& getTexts() const noexcept { return texts; }

	/**
	 * @brief Creates a new text instance.
	 */
	ID<Text> createText();

	/**
	 * @brief Returns text view.
	 * @param text target text instance
	 */
	View<Text> get(ID<Text> text) { return texts.get(text); }
	/**
	 * @brief Returns text view.
	 * @param text target text instance
	 */
	View<Text> get(const Ref<Text>& text) { return texts.get(text); }

	/**
	 * @brief Destroys text instance.
	 * @param text target text instance or null
	 */
	void destroy(ID<Text> text) { texts.destroy(text); }
	/**
	 * @brief Destroys shared text instance.
	 * @param text target text reference or null
	 */
	void destroy(const Ref<Text>& text)
	{
		if (text.isLastRef())
			texts.destroy(ID<Text>(text));
	}
};

} // namespace garden