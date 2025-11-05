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
#include "garden/graphics/image.hpp"

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
	static constexpr uint32 invisible = UINT32_MAX;

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
	using GlyphMap = tsl::robin_map<uint32, Glyph>; /**< Font atlas glyph map. */

	/**
	 * @brief Default font atlas texture usage flags.
	 */
	static constexpr Image::Usage defaultImageFlags = 
		Image::Usage::TransferDst | Image::Usage::TransferQ | Image::Usage::Sampled;
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
	 * @brief Returns font texture atlas new line advance in glyph space.
	 */
	float getNewLineAdvance() const noexcept { return newLineAdvance; }

	/**
	 * @brief Regenerates font atlas texture.
	 * @return True on success, otherwise false.
	 * 
	 * @param value target text string value
	 * @param fontSize font size in pixels
	 * @param imageUsage atlas texture usage flags
	 * @param shrink reduce internal memory usage
	 */
	bool update(u32string_view chars, uint32 fontSize, 
		Image::Usage imageUsage = defaultImageFlags, bool shrink = false);
};

/***********************************************************************************************************************
 * @brief Text data container.
 */
struct Text final
{
public:
	/**
	* @brief Text alignment (anchor) types.
	*/
	enum class Alignment : uint8
	{
		Center,      /**< Aligns text to the center. */
		Left,        /**< Aligns text to the left side. */
		Right,       /**< Aligns text to the right side. */
		Bottom,      /**< Aligns text to the bottom side. */
		Top,         /**< Aligns text to the top side. */
		LeftBottom,  /**< Aligns text to the left bottom corner. */
		LeftTop,     /**< Aligns text to the left top corner. */
		RightBottom, /**< Aligns text to the right bottom corner. */
		RightTop,    /**< Aligns text to the right top corner. */
		Count        /**< Text alignment type count. */
	};
	/**
	 * @brief Text formatting properties container.
	 */
	struct Properties final
	{
		float maxAdvanceX;        /**< Maximum text width in glyph space. */
		Alignment alignment = {}; /**< Text alignment type. (Anchor) */
		bool isBold = false;      /**< Is text bold. (Increased weight)  */
		bool isItalic = false;    /**< Is text italic. (Oblique, tilted) */
		bool useTags = false;     /**< Process HTML-like tags when generating text. */
		Properties() : maxAdvanceX(INFINITY) { }
	};
	/**
	 * @brief Text quad rendering instance data.
	 */
	struct Instance final
	{
		float4 position;
		float4 texCoords;
		uint32 atlasIndex = 0;
		uint32 color = 0;
		uint32 _alignment0 = 0;
		uint32 _alignment1 = 0;
	};

	/**
	 * @brief Default text quad instance buffer usage flags.
	 */
	static constexpr Buffer::Usage defaultBufferFlags = 
		Buffer::Usage::TransferDst | Buffer::Usage::TransferQ | Buffer::Usage::Storage;
private:
	Ref<FontAtlas> fontAtlas = {};
	ID<Buffer> instanceBuffer = {};
	uint32 instanceCount = 0;
	float2 size = float2::zero;
	Properties properties = {};
	bool atlasShared = false;

	friend class garden::TextSystem;
public:
	/**
	 * @brief Returns text font texture atlas.
	 */
	const Ref<FontAtlas>& getFontAtlas() const noexcept { return fontAtlas; }
	/**
	 * @brief Returns text quad instance buffer.
	 */
	ID<Buffer> getInstanceBuffer() const noexcept { return instanceBuffer; }
	/**
	 * @brief Returns text quad instance count.
	 */
	uint32 getInstanceCount() const noexcept { return instanceCount; }
	/**
	 * @brief Returns text size in glyph space.
	 */
	float2 getSize() const noexcept { return size; }
	/**
	 * @brief Returns text formatting properties.
	 */
	Properties getProperties() const noexcept { return properties; }
	/**
	 * @brief Is font texture atlas shared between texts.
	 */
	bool isAtlasShared() const noexcept { return atlasShared; }
	/**
	 * @brief Is text fully ready for graphics rendering.
	 * @details Graphics resource is loaded and transferred.
	 */
	bool isReady() const noexcept;

	/**
	 * @brief Regenerates text data.
	 * @return True on success, otherwise false.
	 *
	 * @param value target text string value
	 * @param fontSize font size in pixels
	 * @param properties text formatting properties
	 * @param[in] fonts font array type[variant[font]]
	 * @param atlasUsage atlas texture usage flags
	 * @param instanceUsage instance buffer usage flags
	 * @param shrink reduce internal memory usage
	 */
	bool update(u32string_view value, uint32 fontSize, Properties properties = {}, 
		const FontArray& fonts = {}, Image::Usage atlasUsage = FontAtlas::defaultImageFlags, 
		Buffer::Usage instanceUsage = defaultBufferFlags, bool shrink = false);
	/**
	 * @brief Regenerates text data.
	 * @return True on success, otherwise false.
	 *
	 * @param value target text string value
	 * @param fontSize font size in pixels
	 * @param properties text formatting properties
	 * @param[in] fonts font array type[variant[font]]
	 * @param atlasUsage atlas texture usage flags
	 * @param instanceUsage instance buffer usage flags
	 * @param shrink reduce internal memory usage
	 */
	bool update(string_view value, uint32 fontSize, Properties properties = {}, 
		const FontArray& fonts = {}, Image::Usage atlasUsage = FontAtlas::defaultImageFlags, 
		Buffer::Usage instanceUsage = defaultBufferFlags, bool shrink = false)
	{
		u32string utf32;
		if (UTF::utf8toUtf32(value, utf32) != 0)
			return {};
		return update(utf32, fontSize, properties, fonts, atlasUsage, instanceUsage, shrink);
	}

	/**
	 * @brief Calculates text caret (cursor) advance in glyph space.
	 * @return Caret advance on success, otherwise float2::minusOne.
	 *
	 * @param value text string value
	 * @param charIndex target text char index
	 */
	float2 calcCaretAdvance(u32string_view value, psize charIndex);
	/**
	 * @brief Calculates text caret (cursor) index.
	 * @return Caret char index on success, otherwise SIZE_MAX.
	 *
	 * @param value text string value
	 * @param caretAdvance target text caret advance in glyph space
	 */
	psize calcCaretIndex(u32string_view value, float2 caretAdvance);

	// TODO: Add isDynamic mode, in which we can update font atlas and text instance buffer each frame.
	//       In this mode there should be persistent mapped staging buffer for the atlas and instance buffer.
};

/***********************************************************************************************************************
 * @brief Text alignment type names.
 */
constexpr const char* textAlignmentNames[(psize)Text::Alignment::Count] =
{
	"Center", "Left", "Right", "Bottom", "Top", "LeftBottom", "LeftTop", "RightBottom", "RightTop"
};
/**
 * @brief Returns text alignment type name string.
 * @param textAlignment target text alignment type
 */
static string_view toString(Text::Alignment textAlignment) noexcept
{
	GARDEN_ASSERT(textAlignment < Text::Alignment::Count);
	return textAlignmentNames[(psize)textAlignment];
}
/**
 * @brief Returns text alignment type from name string.
 *
 * @param name target text alignment name string
 * @param[out] textAlignment text alignment type
 */
static bool toTextAlignment(string_view name, Text::Alignment& textAlignment) noexcept
{
	if (name == "Center") { textAlignment = Text::Alignment::Center; return true; }
	if (name == "Left") { textAlignment = Text::Alignment::Left; return true; }
	if (name == "Right") { textAlignment = Text::Alignment::Right; return true; }
	if (name == "Bottom") { textAlignment = Text::Alignment::Bottom; return true; }
	if (name == "Top") { textAlignment = Text::Alignment::Top; return true; }
	if (name == "LeftBottom") { textAlignment = Text::Alignment::LeftBottom; return true; }
	if (name == "LeftTop") { textAlignment = Text::Alignment::LeftTop; return true; }
	if (name == "RightBottom") { textAlignment = Text::Alignment::RightBottom; return true; }
	if (name == "RightTop") { textAlignment = Text::Alignment::RightTop; return true; }
	return false;
}

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
	Ref<FontAtlas> asciiFontAtlas = {};

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

	friend class garden::FontAtlas;
	friend class garden::ResourceSystem;
	friend class ecsm::Manager;
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

	/*******************************************************************************************************************
	 * @brief Creates a new font texture atlas instance.
	 * @return Font atlas instance on success, otherwise null.
	 * 
	 * @param chars font atlas chars to bake
	 * @param[in] fonts font array type[variant[font]]
	 * @param fontSize font size in pixels
	 * @param imageUsage atlas texture usage flags
	 */
	ID<FontAtlas> createFontAtlas(u32string_view chars, FontArray&& fonts, 
		uint32 fontSize, Image::Usage imageUsage = FontAtlas::defaultImageFlags);
	/**
	 * @brief Creates a new font texture atlas instance.
	 * @return Font atlas instance on success, otherwise null.
	 * 
	 * @param chars font atlas chars to bake
	 * @param[in] fonts font array type[variant[font]]
	 * @param fontSize font size in pixels
	 * @param imageUsage atlas texture usage flags
	 */
	ID<FontAtlas> createFontAtlas(string_view chars, FontArray&& fonts, 
		uint32 fontSize, Image::Usage imageUsage = FontAtlas::defaultImageFlags)
	{
		u32string utf32;
		if (UTF::utf8toUtf32(chars, utf32) != 0)
			return {};
		return createFontAtlas(utf32, std::move(fonts), fontSize, imageUsage);
	}

	/**
	 * @brief Creates a new ASCII font texture atlas instance.
	 * @return Font atlas instance on success, otherwise null.
	 * 
	 * @param[in] fonts font array size[variant[]]
	 * @param fontSize font size in pixels
	 * @param imageUsage atlas texture usage flags
	 */
	ID<FontAtlas> createAsciiFontAtlas(FontArray&& fonts, uint32 fontSize, 
		Image::Usage imageUsage = FontAtlas::defaultImageFlags)
	{
		return createFontAtlas(UTF::printableAscii32, std::move(fonts), fontSize, imageUsage);
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
	 * @param properties text formatting properties
	 * @param isAtlasShared is font atlas shared between texts
	 */
	ID<Text> createText(u32string_view value, const Ref<FontAtlas>& fontAtlas, 
		Text::Properties properties = {}, bool isAtlasShared = false);
	/**
	 * @brief Creates a new text instance.
	 * @return Text instance on succes, otherwise null.
	 *
	 * @param value target text string value
	 * @param[in] fontAtlas font texture atlas
	 * @param properties text formatting properties
	 * @param isAtlasShared is font atlas shared between texts
	 */
	ID<Text> createText(string_view value, const Ref<FontAtlas>& fontAtlas, 
		Text::Properties properties = {}, bool isAtlasShared = false)
	{
		u32string utf32;
		if (UTF::utf8toUtf32(value, utf32) != 0)
			return {};
		return createText(utf32, fontAtlas, properties, isAtlasShared);
	}

	/**
	 * @brief Creates a new text instance.
	 * @return Text instance on succes, otherwise null.
	 *
	 * @param value target text string value
	 * @param[in] fonts text font array
	 * @param fontSize font size in pixels
	 * @param properties text formatting properties
	 * @param imageUsage atlas texture usage flags
	 */
	ID<Text> createText(u32string_view value, FontArray&& fonts, uint32 fontSize, 
		Text::Properties properties = {}, Image::Usage imageUsage = FontAtlas::defaultImageFlags)
	{
		auto fontAtlas = Ref<FontAtlas>(createFontAtlas(value, std::move(fonts), fontSize, imageUsage));
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
	 * @param properties text formatting properties
	 * @param imageUsage atlas texture usage flags
	 */
	ID<Text> createText(string_view value, FontArray&& fonts, uint32 fontSize, 
		Text::Properties properties = {}, Image::Usage imageUsage = FontAtlas::defaultImageFlags)
	{
		u32string utf32;
		if (UTF::utf8toUtf32(value, utf32) != 0)
			return {};
		return createText(utf32, std::move(fonts), fontSize, properties, imageUsage);
	}

	/*******************************************************************************************************************
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
	void destroy(ID<Text> text);
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