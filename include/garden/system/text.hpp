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
#include "garden/utf.hpp"
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
	float4 position;
	float4 texCoords;
	float advance = 0.0f;
	uint32 value = 0;
};

/**
 * @brief Font texture atlas container.
 */
struct FontAtlas final
{
	using FontArray = vector<ID<Font>>;
	using GlyphMap = tsl::robin_map<uint32, Glyph>;

	vector<FontArray> fonts;
	vector<GlyphMap> glyphs;
	ID<Image> image = {};
	uint32_t fontSize = 0;
	float newLineAdvance = 0.0f;
	bool isGenerated = false;
};

/**
 * @brief Text data container.
 */
struct Text final
{
	u32string string = {};
	float2 size = float2::zero;
	Ref<FontAtlas> fontAtlas = {};
	ID<DescriptorSet> descritproSet = {};
	Color color = Color::white;
	bool isBold = false;
	bool isItalic = false;
	bool useTags = false;
	bool isConstant = false;
};

/***********************************************************************************************************************
 * @brief Handles text mesh generation, usage.
 */
class TextSystem final : public System, public Singleton<TextSystem>
{
public:
	using FontPool = LinearPool<Font>;
	using FontAtlasPool = LinearPool<FontAtlas, false>;
	using TextPool = LinearPool<Text, false>;
private:
	FontPool fonts;
	FontAtlasPool fontAtlases;
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
	 * @brief Returns font atlas pool.
	 */
	const FontAtlasPool& getFontAtlases() const noexcept { return fontAtlases; }
	/**
	 * @brief Returns text pool.
	 */
	const TextPool& getTexts() const noexcept { return texts; }

	/**
	 * @brief Creates a new font texture atlas instance.
	 * @return Font atlas instance on success, otherwise null.
	 * 
	 * @param chars atlas chars to bake
	 * @param fontSize font size in pixels
	 * @param[in] fonts font array size[variant[]]
	 */
	ID<FontAtlas> createFontAtlas(u32string_view chars, uint32 fontSize, vector<vector<ID<Font>>>&& fonts);
	/**
	 * @brief Creates a new ASCII font texture atlas instance.
	 * @return Font atlas instance on success, otherwise null.
	 * 
	 * @param fontSize font size in pixels
	 * @param[in] fonts font array size[variant[]]
	 */
	ID<FontAtlas> createAsciiFontAtlas(uint32 fontSize, vector<vector<ID<Font>>>&& fonts)
	{
		return createFontAtlas(printableAscii32, fontSize, std::move(fonts));
	}

	/**
	 * @brief Returns font texture atlas view.
	 * @param fontAtlas target font atlas instance
	 */
	View<FontAtlas> get(ID<FontAtlas> fontAtlas) { return fontAtlases.get(fontAtlas); }
	/**
	 * @brief Returns font texture atlas view.
	 * @param fontAtlas target font atlas instance
	 */
	View<FontAtlas> get(const Ref<FontAtlas>& fontAtlas) { return fontAtlases.get(fontAtlas); }

	/**
	 * @brief Destroys font texture atlas instance.
	 * @param fontAtlas target font atlas instance or null
	 */
	void destroy(ID<FontAtlas> fontAtlas);
	/**
	 * @brief Destroys shared font texture atlas instance.
	 * @param fontAtlas target font atlas reference or null
	 */
	void destroy(const Ref<FontAtlas>& fontAtlas)
	{
		if (fontAtlas.isLastRef())
			destroy(ID<FontAtlas>(fontAtlas));
	}
	
	/*******************************************************************************************************************
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