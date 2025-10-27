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

namespace garden
{

using namespace ecsm;
using namespace garden::graphics;

class TextSystem;
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
	using GlyphMap = tsl::robin_map<uint32, Glyph>;
private:
	FontArray fonts;
	vector<GlyphMap> glyphs;
	ID<Image> image = {};
	uint32 fontSize = 0;
	float newLineAdvance = 0.0f;

	friend class garden::TextSystem;
public:
	/**
	 * @brief Returns font texture atlas font array.
	 */
	const FontArray& getFonts() const noexcept { return fonts; }
	/**
	 * @brief Returns font texture atlas glyph map.
	 */
	const vector<GlyphMap>& getGlyphs() const noexcept { return glyphs; }
	/**
	 * @brief Returns font texture atlas image.
	 */
	const ID<Image>& getImage() const noexcept { return image; }
	/**
	 * @brief Returns font texture atlas font size in pixels.
	 */
	uint32 getFontSize() const noexcept { return fontSize; }
	/**
	 * @brief Returns font texture atlas new line advance
	 */
	float getNewLineAdvance() const noexcept { return newLineAdvance; }
};

/**
 * @brief Text data container.
 */
struct Text final
{
public:
	/**
	 * @brief Text formating properties container.
	 */
	struct Properties final
	{
		Color color;              /**< Text sRGB color. */
		bool isBold = false;      /**< Is text bold. (Increased weight)  */
		bool isItalic = false;    /**< Is text italic. (Oblique, tilted) */
		bool useHtmlTags = false; /**< Process HTML tags when generating text. */
		Properties() : color(Color::white) { }
	};
private:
	u32string value = {};
	Ref<FontAtlas> fontAtlas = {};
	float2 size = float2::zero;
	Properties properties = {};
	bool dynamic = false;

	friend class garden::TextSystem;
public:
	/**
	 * @brief Return text string value.
	 */
	const u32string& getValue() const noexcept { return value; }
	/**
	 * @brief Return text font texture atlas.
	 */
	const Ref<FontAtlas>& getFontAtlas() const noexcept { return fontAtlas; }
	/**
	 * @brief Returns text size.
	 */
	float2 getSize() const noexcept { return size; }
	/**
	 * @brief Returns text sproperties.
	 */
	Properties getProperties() const noexcept { return properties; }
	/**
	 * @brief Does text can be updated.
	 */
	bool isDynamic() const noexcept { return dynamic; }

	/**
	 * @brief Regenerates text data.
	 * @return True on success, otherwise false.
	 *
	 * @param value target text string value
	 * @param[in] fonts new font array or null
	 * @param properties text formating properties
	 * @param shrink reduce internal memory usage
	 */
	bool update(u32string_view value, const FontArray& fonts = {}, Properties properties = {}, bool shrink = false);
	/**
	 * @brief Regenerates text data.
	 * @return True on success, otherwise false.
	 *
	 * @param value target text string value
	 * @param[in] fonts new font array or null
	 * @param properties text formating properties
	 * @param shrink reduce internal memory usage
	 */
	bool update(string_view value, const FontArray& fonts = {}, Properties properties = {}, bool shrink = false)
	{
		u32string utf32;
		if (UTF::utf8toUtf32(value, utf32) != 0)
			return {};
		return update(utf32, fonts, properties, shrink);
	}
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
	 * @param chars font atlas chars to bake
	 * @param fontSize font size in pixels
	 * @param[in] fonts font array type[variant[font]]
	 * @param imageUsage atlas texture usage flags
	 */
	ID<FontAtlas> createFontAtlas(u32string_view chars, uint32 fontSize, FontArray&& fonts,
		Image::Usage imageUsage = Image::Usage::TransferDst | Image::Usage::TransferQ | Image::Usage::Sampled);
	/**
	 * @brief Creates a new ASCII font texture atlas instance.
	 * @return Font atlas instance on success, otherwise null.
	 * 
	 * @param fontSize font size in pixels
	 * @param[in] fonts font array size[variant[]]
	 * @param imageUsage atlas texture usage flags
	 */
	ID<FontAtlas> createAsciiFontAtlas(uint32 fontSize, FontArray&& fonts, Image::Usage imageUsage = 
		Image::Usage::TransferDst | Image::Usage::TransferQ | Image::Usage::Sampled)
	{
		return createFontAtlas(UTF::printableAscii32, fontSize, std::move(fonts), imageUsage);
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
	 * @return Text instance on succes, otherwise null.
	 *
	 * @param value target text string value
	 * @param[in] fontAtlas font texture atlas
	 * @param properties text formating properties
	 */
	ID<Text> createText(u32string_view value, const Ref<FontAtlas>& fontAtlas, Text::Properties properties = {});
	/**
	 * @brief Creates a new text instance.
	 * @return Text instance on succes, otherwise null.
	 *
	 * @param value target text string value
	 * @param[in] fontAtlas font texture atlas
	 * @param properties text formating properties
	 */
	ID<Text> createText(string_view value, const Ref<FontAtlas>& fontAtlas, Text::Properties properties = {})
	{
		u32string utf32;
		if (UTF::utf8toUtf32(value, utf32) != 0)
			return {};
		return createText(utf32, fontAtlas, properties);
	}

	/**
	 * @brief Creates a new text instance.
	 * @return Text instance on succes, otherwise null.
	 *
	 * @param value target text string value
	 * @param[in] fonts text font array
	 * @param fontSize font size in pixels
	 * @param properties text formating properties
	 */
	ID<Text> createText(u32string_view value, FontArray&& fonts, uint32 fontSize, Text::Properties properties = {})
	{
		auto fontAtlas = Ref<FontAtlas>(createFontAtlas(value, fontSize, std::move(fonts)));
		if (!fontAtlas)
			return {};
		return createText(value, fontAtlas, properties);
	}
	/**
	 * @brief Creates a new text instance.
	 * @return Text instance on succes, otherwise null.
	 *
	 * @param value target text string value
	 * @param[in] fonts text font array
	 * @param fontSize font size in pixels
	 * @param properties text formating properties
	 */
	ID<Text> createText(string_view value, FontArray&& fonts, uint32 fontSize, Text::Properties properties = {})
	{
		u32string utf32;
		if (UTF::utf8toUtf32(value, utf32) != 0)
			return {};
		return createText(utf32, std::move(fonts), fontSize, properties);
	}

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