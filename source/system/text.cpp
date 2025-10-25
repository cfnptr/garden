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

#include "garden/system/text.hpp"
#include "garden/system/graphics.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/log.hpp"
#include "garden/profiler.hpp"

#include "ft2build.h"
#include FT_FREETYPE_H

using namespace garden;

TextSystem::TextSystem(bool setSingleton) : Singleton(setSingleton)
{
	FT_Library ftLibrary = nullptr;
	auto result = FT_Init_FreeType(&ftLibrary);
	if (result != 0)
		throw GardenError("Failed to initialize FreeType. (error: " + string(FT_Error_String(result)) + ")");
	this->ftLibrary = ftLibrary;
}
TextSystem::~TextSystem()
{
	auto result = FT_Done_FreeType((FT_Library)ftLibrary);
	GARDEN_ASSERT_MSG(!result, "Failed to deinitialize FreeType");

	unsetSingleton();
}

//**********************************************************************************************************************
void TextSystem::update()
{
	SET_CPU_ZONE_SCOPED("Texts Update");

	texts.dispose();
	fontAtlases.dispose();
	fonts.dispose();
}

static void prepareGlyphs(u32string_view chars, FontAtlas::GlyphMap& glyphs)
{
	Glyph glyph;
	for (auto value : chars)
	{
		if (value == '\n') continue;
		if (value == '\t') value = ' ';
		glyph.value = value;
		glyphs.emplace(value, glyph);
	}
}
static bool fillFontAtlas(const LinearPool<Font>& fontPool, 
	const vector<ID<Font>>& fonts, FontAtlas::GlyphMap& glyphs, uint8_t* pixels, 
	uint32 fontSize, uint32 glyphLength, uint32 pixelLength)
{
	for (auto font : fonts)
	{
		auto fontView = fontPool.get(font);
		auto result = FT_Set_Pixel_Sizes((FT_Face)fontView->face, 0, (FT_UInt)fontSize);
		if (result != 0)
		{
			GARDEN_LOG_DEBUG("Failed to set FreeType pixel sizes. (error: " + string(FT_Error_String(result)) + ")");
			return false;
		}
	}

	auto mainFace = (FT_Face)fontPool.get(fonts[0])->face;
	uint32 index = 0;

	for (auto i = glyphs.begin(); i != glyphs.end(); i++)
	{
		Glyph glyph; glyph.value = i->second.value;
		auto charIndex = FT_Get_Char_Index(mainFace, glyph.value);

		auto charFace = mainFace;
		if (charIndex == 0 && glyph.value != '\0')
		{
			for (auto i = fonts.begin() + 1; i != fonts.end(); i++)
			{
				auto face = (FT_Face)fontPool.get(*i)->face;
				charIndex = FT_Get_Char_Index(face, glyph.value);
				if (charIndex == 0)
					continue;
				charFace = face;
				break;
			}
		}

		auto result = FT_Load_Glyph(charFace, charIndex, FT_LOAD_RENDER);
		if (result != 0)
		{
			GARDEN_LOG_DEBUG("Failed to load FreeType glyph. (error: " + string(FT_Error_String(result)) + ")");
			return false;
		}

		const auto glyphSlot = charFace->glyph; auto baseWidth = glyphSlot->bitmap.width;
		auto glyphWidth = min(baseWidth, fontSize), glyphHeight = min(glyphSlot->bitmap.rows, fontSize);
		glyph.advance = ((float)glyphSlot->advance.x * (1.0f / 64.0f)) / fontSize;

		if (glyphWidth * glyphHeight == 0)
		{
			glyph.value = UINT32_MAX;
		}
		else
		{
			auto glyphPosY = index / glyphLength, glyphPosX = (uint32)(index - (size_t)glyphPosY * glyphLength);
			auto pixelPosX = glyphPosX * fontSize, pixelPosY = glyphPosY * fontSize;
			glyph.position.x = (float)glyphSlot->bitmap_left / fontSize;
			glyph.position.y = ((float)glyphSlot->bitmap_top - glyphHeight) / fontSize;
			glyph.position.z = glyph.position.x + ((float)glyphWidth / fontSize);
			glyph.position.w = glyph.position.y + ((float)glyphHeight / fontSize);
			glyph.texCoords.x = (float)pixelPosX / pixelLength;
			glyph.texCoords.y = (float)pixelPosY / pixelLength;
			glyph.texCoords.z = glyph.texCoords.x + ((float)glyphWidth / pixelLength);
			glyph.texCoords.w = glyph.texCoords.y + ((float)glyphHeight / pixelLength);

			const auto bitmap = glyphSlot->bitmap.buffer;
			for (uint32 y = 0; y < glyphHeight; y++)
			{
				for (uint32 x = 0; x < glyphWidth; x++)
					pixels[((y + pixelPosY) * pixelLength + x + pixelPosX) * 4] = bitmap[x + y * baseWidth];
			}
		}

		i.value() = glyph; index++;
	}

	return true;
}

//**********************************************************************************************************************
ID<FontAtlas> TextSystem::createFontAtlas(u32string_view chars, uint32 fontSize, vector<vector<ID<Font>>>&& fonts)
{
	GARDEN_ASSERT(fontSize > 0);
	GARDEN_ASSERT(fonts.size() == 1 || fonts.size() == 4);
	GARDEN_ASSERT(!fonts[0].empty());

	auto isFontVariable = fonts.size() == 1;
	auto defaultFontView = this->fonts.get(fonts[0][0]);
	auto defaultFace = (FT_Face)defaultFontView->face;

	if (isFontVariable && !FT_HAS_MULTIPLE_MASTERS(defaultFace))
	{
		GARDEN_LOG_DEBUG("Failed to create font atlas, provided font is not variable.");
		return {};
	}

	auto result = FT_Set_Pixel_Sizes(defaultFace, 0, (FT_UInt)fontSize);
	if (result != 0)
	{
		GARDEN_LOG_DEBUG("Failed to set FreeType pixel sizes. (error: " + string(FT_Error_String(result)) + ")");
		return {};
	}
	auto newLineAdvance = ((float)defaultFace->size->metrics.height * (1.0f / 64.0f)) / fontSize;

	FontAtlas::GlyphMap regularGlyphs; prepareGlyphs(chars, regularGlyphs);
	if (regularGlyphs.empty())
		return {};
	
	const auto format = Image::Format::UnormR8G8B8A8;
	const auto usage = Image::Usage::TransferDst | Image::Usage::Sampled; 
	auto glyphLength = (uint32)ceil(sqrt((double)regularGlyphs.size())), pixelLength = glyphLength * fontSize;
	if (!Image::isSupported(Image::Type::Texture2D, format, usage, uint3(glyphLength, glyphLength, 1)))
		return {};

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto binarySize = (size_t)pixelLength * pixelLength * 4;
	auto stagingBuffer = graphicsSystem->createBuffer(Buffer::Usage::TransferSrc, 
		Buffer::CpuAccess::RandomReadWrite, binarySize, Buffer::Location::Auto, Buffer::Strategy::Speed);
	SET_RESOURCE_DEBUG_NAME(stagingBuffer, "buffer.staging.fontAtlas");

	auto stagingView = graphicsSystem->get(stagingBuffer);
	auto pixels = (uint8*)stagingView->getMap(); memset(pixels, 0, binarySize);
	auto boldGlyphs = regularGlyphs, italicGlyphs = regularGlyphs, boldItalicGlyphs = regularGlyphs;

	vector<ID<Font>>* regularFonts, *boldFonts, *italicFonts, *boldItalicFonts;
	if (isFontVariable)
	{
		regularFonts = boldFonts = italicFonts = boldItalicFonts = &fonts[0];
	}

	if (!fillFontAtlas(this->fonts, *regularFonts, regularGlyphs, pixels, fontSize, glyphLength, pixelLength) ||
		!fillFontAtlas(this->fonts, *boldFonts, boldGlyphs, pixels + 1, fontSize, glyphLength, pixelLength) ||
		!fillFontAtlas(this->fonts, *italicFonts, italicGlyphs, pixels + 2, fontSize, glyphLength, pixelLength) ||
		!fillFontAtlas(this->fonts, *boldItalicFonts, boldItalicGlyphs, pixels + 3, fontSize, glyphLength, pixelLength))
	{
		graphicsSystem->destroy(stagingBuffer);
		return {};
	}

	auto image = graphicsSystem->createImage(format, usage, { { nullptr } }, uint2(pixelLength), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.fontAtlas" + to_string(*image));

	// TODO: record commands.

	auto fontAtlas = fontAtlases.create();
	auto fontAtlasView = fontAtlases.get(fontAtlas);
	fontAtlasView->fonts = std::move(fonts);
	fontAtlasView->glyphs.resize(4);
	fontAtlasView->glyphs[0] = std::move(regularGlyphs);
	fontAtlasView->glyphs[1] = std::move(boldGlyphs);
	fontAtlasView->glyphs[2] = std::move(italicGlyphs);
	fontAtlasView->glyphs[3] = std::move(boldItalicGlyphs);
	fontAtlasView->image = image;
	fontAtlasView->fontSize = fontSize;
	fontAtlasView->newLineAdvance = newLineAdvance;
	return fontAtlas;
}
void TextSystem::destroy(ID<FontAtlas> fontAtlas)
{
	if (!fontAtlas)
		return;

	auto fontAtlasView = fontAtlases.get(fontAtlas);
	GraphicsSystem::Instance::get()->destroy(fontAtlasView->image);
	fontAtlases.destroy(fontAtlas);
}

ID<Text> TextSystem::createText()
{
	return {}; // TODO:
}