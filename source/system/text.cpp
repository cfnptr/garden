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
#include "freetype/ftmm.h"

using namespace garden;

bool Text::update(u32string_view value, const FontArray& fonts, Properties properties, bool shrink)
{
	GARDEN_ASSERT(!value.empty());

	return false;
}

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

static constexpr float fixedToFloat(FT_Fixed fixed) noexcept { return (float)fixed / 65536.0f; }
static constexpr FT_Fixed floatToFixed(float f) { return (FT_Fixed)(f * 65536.0f + 0.5f); }

static bool fillFontAtlas(const LinearPool<Font>& fontPool, 
	FT_Library ftLibrary, const vector<Ref<Font>>& fonts, FontAtlas::GlyphMap& glyphs, 
	uint8_t* pixels, uint32 fontSize, uint32 glyphLength, uint32 pixelLength, uint8 fontIndex)
{
	for (const auto& font : fonts)
	{
		auto fontFace = (FT_Face)fontPool.get(font)->face;
		auto result = FT_Set_Pixel_Sizes(fontFace, 0, (FT_UInt)fontSize);
		if (result != 0)
		{
			GARDEN_LOG_DEBUG("Failed to set FreeType font pixel sizes. ("
				"error: " + string(FT_Error_String(result)) + ")");
			return false;
		}

		if (FT_HAS_MULTIPLE_MASTERS(fontFace))
		{
			FT_MM_Var* master = NULL;
			result = FT_Get_MM_Var(fontFace, &master);
			if (result != 0)
			{
				GARDEN_LOG_DEBUG("Failed to get FreeType font variation. ("
					"error: " + string(FT_Error_String(result)) + ")");
				return false;
			}

			auto axes = master->axis;
			vector<FT_Fixed> coords(master->num_axis);
			auto isItalic = fontIndex == 2 || fontIndex == 3;
			auto weight = fontIndex == 1 || fontIndex == 3 ? 700.0f : 400.0f;
			// TODO: allow to pass regular and bold weights. Also to pass italic angle.

			for (FT_UInt i = 0; i < master->num_axis; i++)
			{
				auto tag = axes[i].tag;
				if (tag == (FT_ULong)('w' << 24 | 'g' << 16 | 'h' << 8 | 't'))
				{
					weight = max(weight, fixedToFloat(axes[i].minimum));
					weight = min(weight, fixedToFloat(axes[i].maximum));
					coords[i] = floatToFixed(weight);
				}
				else if (tag == (FT_ULong)('i' << 24 | 't' << 16 | 'a' << 8 | 'l') ||
					tag == (FT_ULong)('s' << 24 | 'l' << 16 | 'n' << 8 | 't'))
				{
					coords[i] = isItalic ? axes[i].maximum : axes[i].def;
				}
				else
				{
					coords[i] = axes[i].def;
				}
			}

			result = FT_Set_Var_Design_Coordinates(fontFace, master->num_axis, coords.data());
			FT_Done_MM_Var(ftLibrary, master);
			if (result != 0)
			{
				GARDEN_LOG_DEBUG("Failed to set FreeType font design coordinates. ("
					"error: " + string(FT_Error_String(result)) + ")");
				return false;
			}
		}
	}

	pixels += fontIndex;
	auto mainFace = (FT_Face)fontPool.get(fonts[0])->face;
	uint32 index = 0;

	for (auto i = glyphs.begin(); i != glyphs.end(); i++)
	{
		Glyph glyph; glyph.value = i->second.value;
		auto charIndex = FT_Get_Char_Index(mainFace, (FT_ULong)glyph.value);

		auto charFace = mainFace;
		if (charIndex == 0 && glyph.value != '\0')
		{
			for (auto i = fonts.begin() + 1; i != fonts.end(); i++)
			{
				auto face = (FT_Face)fontPool.get(*i)->face;
				charIndex = FT_Get_Char_Index(face, (FT_ULong)glyph.value);
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
ID<FontAtlas> TextSystem::createFontAtlas(u32string_view chars, 
	uint32 fontSize, FontArray&& fonts, Image::Usage imageUsage)
{
	GARDEN_ASSERT(!chars.empty());
	GARDEN_ASSERT(fontSize > 0);
	GARDEN_ASSERT(fonts.size() == 4);

	SET_CPU_ZONE_SCOPED("Font Atlas Create");

	for (const auto& variants : fonts)
	{
		if (!variants.empty())
			continue;
		GARDEN_LOG_DEBUG("Failed to create font atlas, missing fonts.");
		return {};
	}

	auto defaultFontView = this->fonts.get(fonts[0][0]);
	auto defaultFace = (FT_Face)defaultFontView->face;

	auto result = FT_Set_Pixel_Sizes(defaultFace, 0, (FT_UInt)fontSize);
	if (result != 0)
	{
		GARDEN_LOG_DEBUG("Failed to set FreeType font pixel sizes. ("
			"error: " + string(FT_Error_String(result)) + ")");
		return {};
	}
	auto newLineAdvance = ((float)defaultFace->size->metrics.height * (1.0f / 64.0f)) / fontSize;

	FontAtlas::GlyphMap regularGlyphs; prepareGlyphs(chars, regularGlyphs);
	if (regularGlyphs.empty())
	{
		GARDEN_LOG_DEBUG("Failed to create font atlas, no visible glyphs.");
		return {};
	}
	
	constexpr auto format = Image::Format::UnormR8G8B8A8;
	auto glyphLength = (uint32)ceil(sqrt((double)regularGlyphs.size())), pixelLength = glyphLength * fontSize;
	if (!Image::isSupported(Image::Type::Texture2D, format, imageUsage, uint3(glyphLength, glyphLength, 1)))
	{
		GARDEN_LOG_DEBUG("Failed to create font atlas, resulting image is not supported.");
		return {};
	}

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto binarySize = (size_t)pixelLength * pixelLength * 4;
	auto stagingBuffer = graphicsSystem->createBuffer(Buffer::Usage::TransferSrc, 
		Buffer::CpuAccess::RandomReadWrite, binarySize, Buffer::Location::Auto, Buffer::Strategy::Speed);
	SET_RESOURCE_DEBUG_NAME(stagingBuffer, "buffer.staging.fontAtlas");

	auto stagingView = graphicsSystem->get(stagingBuffer);
	#if GARDEN_DEBUG // Hack: skips queue ownership asserts.
	BufferExt::getUsage(**stagingView) |= Buffer::Usage::TransferQ | Buffer::Usage::ComputeQ;
	#endif

	auto ft = (FT_Library)ftLibrary;
	auto pixels = (uint8*)stagingView->getMap(); memset(pixels, 0, binarySize);
	auto boldGlyphs = regularGlyphs, italicGlyphs = regularGlyphs, boldItalicGlyphs = regularGlyphs;

	if (!fillFontAtlas(this->fonts, ft, fonts[0], regularGlyphs, pixels, fontSize, glyphLength, pixelLength, 0) || 
		!fillFontAtlas(this->fonts, ft, fonts[1], boldGlyphs, pixels, fontSize, glyphLength, pixelLength, 1) || 
		!fillFontAtlas(this->fonts, ft, fonts[2], italicGlyphs, pixels, fontSize, glyphLength, pixelLength, 2) || 
		!fillFontAtlas(this->fonts, ft, fonts[3], boldItalicGlyphs, pixels, fontSize, glyphLength, pixelLength, 3))
	{
		graphicsSystem->destroy(stagingBuffer);
		return {};
	}
	stagingView->flush();

	auto image = graphicsSystem->createImage(format, imageUsage, 
		{ { nullptr } }, uint2(pixelLength), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.fontAtlas" + to_string(*image));

	auto stopRecording = graphicsSystem->tryStartRecording(CommandBufferType::TransferOnly);
	Image::copy(stagingBuffer, image);
	
	if (stopRecording)
		graphicsSystem->stopRecording();
	graphicsSystem->destroy(stagingBuffer);

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
	ResourceSystem::Instance::get()->destroyShared(fontAtlasView->fonts);
	fontAtlases.destroy(fontAtlas);
}

//**********************************************************************************************************************
ID<Text> TextSystem::createText(u32string_view value, const Ref<FontAtlas>& fontAtlas, Text::Properties properties)
{
	GARDEN_ASSERT(!value.empty());
	GARDEN_ASSERT(fontAtlas);


	
	return {}; // TODO:
}