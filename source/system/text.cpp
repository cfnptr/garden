// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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
#include "garden/system/thread.hpp"
#include "garden/system/log.hpp"
#include "garden/profiler.hpp"

#include "ft2build.h"
#include FT_FREETYPE_H
#include "freetype/ftmm.h"

using namespace garden;

static void prepareGlyphs(u32string_view chars, FontAtlas::GlyphMap& glyphs)
{
	Glyph glyph; glyph.value = '\0';
	glyphs.emplace('\0', glyph); // Note: tofu symbol.

	for (auto value : chars)
	{
		if (value == '\n') continue;
		if (value == '\t') value = ' ';
		glyph.value = value;
		glyphs.emplace(value, glyph);
	}
}

static constexpr float fixedToFloat(FT_Fixed fixed) noexcept { return (float)fixed / 65536.0f; }
static constexpr FT_Fixed floatToFixed(float f) noexcept { return (FT_Fixed)(f * 65536.0f + 0.5f); }
static uint32 calcGlyphLength(psize glyphCount) noexcept { return (uint32)ceil(sqrt((double)glyphCount)); }

//**********************************************************************************************************************
static bool fillFontAtlas(const LinearPool<Font>& fontPool, FT_Library ftLibrary, const vector<Ref<Font>>& fonts, 
	FontAtlas::GlyphMap& glyphs, uint8* pixels, uint32 fontSize, uint32 glyphLength, uint2 pixelSize, 
	uint8 fontIndex, uint32 itemOffset, uint32 itemCount, uint32 threadIndex)
{
	for (const auto& font : fonts)
	{
		auto fontFace = (FT_Face)fontPool.get(font)->faces.at(threadIndex);
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
				else if (isItalic && (tag == (FT_ULong)('i' << 24 | 't' << 16 | 'a' << 8 | 'l') ||
					tag == (FT_ULong)('s' << 24 | 'l' << 16 | 'n' << 8 | 't')))
				{
					coords[i] = axes[i].maximum;
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

	auto invFontSize = 1.0f / fontSize; auto invPixelSize = float2::one / pixelSize;
	auto mainFace = (FT_Face)fontPool.get(fonts[0])->faces.at(threadIndex);
	auto i = glyphs.begin(); pixels += fontIndex;

	for (uint32 j = 0; j < itemOffset; j++)
		i++; // TODO: solve this somehow, suboptimal!

	for (uint32 index = itemOffset; index < itemCount; index++, i++)
	{
		GARDEN_ASSERT_MSG(i != glyphs.end(), "Detected memory corruption");
		Glyph glyph; glyph.value = i->second.value;
		auto charIndex = FT_Get_Char_Index(mainFace, (FT_ULong)glyph.value);

		auto charFace = mainFace;
		if (charIndex == 0 && glyph.value != '\0')
		{
			for (auto j = fonts.begin() + 1; j != fonts.end(); j++)
			{
				auto face = (FT_Face)fontPool.get(*j)->faces.at(threadIndex);
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
		glyph.advance = ((float)glyphSlot->advance.x * (1.0f / 64.0f)) * invFontSize;

		if (glyphWidth * glyphHeight == 0)
		{
			glyph.value = Glyph::invisible;
		}
		else
		{
			auto glyphPosY = index / glyphLength, glyphPosX = (uint32)(index - (psize)glyphPosY * glyphLength);
			auto pixelPosX = glyphPosX * fontSize, pixelPosY = glyphPosY * fontSize;
			glyph.position.x = (float)glyphSlot->bitmap_left * invFontSize;
			glyph.position.y = ((float)glyphSlot->bitmap_top - glyphHeight) * invFontSize;
			glyph.position.z = glyph.position.x + ((float)glyphWidth * invFontSize);
			glyph.position.w = glyph.position.y + ((float)glyphHeight * invFontSize);
			glyph.texCoords.x = (float)pixelPosX * invPixelSize.x;
			glyph.texCoords.y = (float)pixelPosY * invPixelSize.y;
			glyph.texCoords.z = glyph.texCoords.x + ((float)glyphWidth * invPixelSize.x);
			glyph.texCoords.w = glyph.texCoords.y + ((float)glyphHeight * invPixelSize.y);

			const auto bitmap = glyphSlot->bitmap.buffer;
			for (uint32 y = 0; y < glyphHeight; y++)
			{
				for (uint32 x = 0; x < glyphWidth; x++)
					pixels[((y + pixelPosY) * pixelSize.x + x + pixelPosX) * 4] = bitmap[x + y * baseWidth];
			}
		}

		i.value() = glyph;
	}

	return true;
}
static bool fillFontAtlas(const LinearPool<Font>& fontPool, FT_Library ftLibrary, const FontArray& fonts, 
	vector<FontAtlas::GlyphMap>& glyphs, uint8* pixels, uint32 fontSize, uint32 glyphLength, uint2 pixelSize)
{
	auto threadSystem = ThreadSystem::Instance::tryGet();
	if (threadSystem)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		atomic_int64_t result = 0;

		for (uint8 i = 0; i < 4; i++)
		{
			const auto& fontArray = fonts[i]; auto& glyphMap = glyphs[i];
			threadPool.addItems([&fontPool, ftLibrary, &fontArray, &glyphMap, pixels, fontSize, 
				glyphLength, pixelSize, i, &result](const ThreadPool::Task& task)
			{
				if (fillFontAtlas(fontPool, ftLibrary, fontArray, glyphMap, pixels, fontSize, glyphLength, 
					pixelSize, i, task.getItemOffset(), task.getItemCount(), task.getThreadIndex()))
				{
					result += task.getItemCount() - task.getItemOffset();
				}
			},
			glyphMap.size());
		}

		threadPool.wait();
		return result == glyphs[0].size() * 4;
	}

	auto glyphCount = glyphs[0].size();
	for (uint8 i = 0; i < 4; i++)
	{
		if (!fillFontAtlas(fontPool, ftLibrary, fonts[i], glyphs[i], pixels, 
			fontSize, glyphLength, pixelSize, i, 0, glyphCount, 0))
		{
			return false;
		}
	}
	return true;
}

//**********************************************************************************************************************
bool FontAtlas::update(u32string_view chars, uint32 fontSize, Image::Usage imageUsage, bool shrink)
{
	GARDEN_ASSERT(!chars.empty());
	GARDEN_ASSERT(fontSize > 0);

	SET_CPU_ZONE_SCOPED("Font Atlas Update");

	auto textSystem = TextSystem::Instance::get();
	auto defaultFace = (FT_Face)textSystem->fonts.get(fonts[0][0])->faces[0];

	auto result = FT_Set_Pixel_Sizes(defaultFace, 0, (FT_UInt)fontSize);
	if (result != 0)
	{
		GARDEN_LOG_DEBUG("Failed to set FreeType font pixel sizes. ("
			"error: " + string(FT_Error_String(result)) + ")");
		return false;
	}
	auto newLineAdvance = ((float)defaultFace->size->metrics.height * (1.0f / 64.0f)) / fontSize;

	vector<GlyphMap> glyphs(4); prepareGlyphs(chars, glyphs[0]);
	if (glyphs[0].empty())
	{
		GARDEN_LOG_DEBUG("Failed to create font atlas, no visible glyphs.");
		return false;
	}
	
	constexpr auto imageFormat = Image::Format::UnormR8G8B8A8;
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto glyphLength = calcGlyphLength(glyphs[0].size());
	auto newPixelSize = uint2(glyphLength * fontSize, (uint32)
		ceil((double)glyphs[0].size() / glyphLength) * fontSize); 
	auto currPixelSize = uint2::zero;

	if (image && !shrink)
	{
		auto imageView = graphicsSystem->get(image);
		if (imageView->isReady()) // Note: it may be asyn transfering right now.
			currPixelSize = (uint2)imageView->getSize();
	}

	if (areAnyTrue(newPixelSize > currPixelSize) && !Image::isSupported(
		Image::Type::Texture2D, imageFormat, imageUsage, uint3(newPixelSize, 1)))
	{
		GARDEN_LOG_DEBUG("Failed to create font atlas, resulting image is not supported.");
		return false;
	}
	
	auto pixelSize = max(currPixelSize, newPixelSize);
	auto binarySize = (uint64)pixelSize.x * newPixelSize.y * 4;
	auto stagingBuffer = graphicsSystem->createStagingBuffer(Buffer::CpuAccess::RandomReadWrite, binarySize);
	SET_RESOURCE_DEBUG_NAME(stagingBuffer, "buffer.staging.fontAtlas" + to_string(*stagingBuffer));

	auto stagingView = graphicsSystem->get(stagingBuffer);
	auto pixels = (uint8*)stagingView->getMap(); memset(pixels, 0, binarySize);
	glyphs[1] = glyphs[0]; glyphs[2] = glyphs[0]; glyphs[3] = glyphs[0];

	if (!fillFontAtlas(textSystem->fonts, (FT_Library)textSystem->ftLibrary, 
		fonts, glyphs, pixels, fontSize, glyphLength, pixelSize))
	{
		graphicsSystem->destroy(stagingBuffer);
		return false;
	}
	stagingView->flush();

	if (areAnyTrue(newPixelSize > currPixelSize))
	{
		graphicsSystem->destroy(image);
		image = graphicsSystem->createImage(imageFormat, imageUsage, 
			{ { nullptr } }, newPixelSize, Image::Strategy::Size);
		SET_RESOURCE_DEBUG_NAME(image, "image.fontAtlas" + to_string(*image));
		pixelSize = newPixelSize; // Note: needed for Y size difference!
	}

	Image::CopyBufferRegion copyRegion;
	copyRegion.imageExtent = uint3(pixelSize.x, newPixelSize.y, 1);
	Image::copy(stagingBuffer, image, copyRegion);
	graphicsSystem->destroy(stagingBuffer);

	this->glyphs = std::move(glyphs);
	this->fontSize = fontSize;
	this->newLineAdvance = newLineAdvance;
	return true;
}

//**********************************************************************************************************************
static void updateTextNewLine(Text::Instance* instances, uint32 fontSize, float newLineAdvance, float2& instanceOffset, 
	uint32 instanceIndex, uint32& lastNewLineIndex, float2& textSize, Text::Alignment alignment) noexcept
{
	float offset;
	switch (alignment)
	{
	case Text::Alignment::Center: offset = floorf(instanceOffset.x * -0.5f * fontSize) / fontSize; break;
	case Text::Alignment::Right: offset = -instanceOffset.x; break;
	case Text::Alignment::Bottom: offset = floorf(instanceOffset.x * -0.5f * fontSize) / fontSize; break;
	case Text::Alignment::Top: offset = floorf(instanceOffset.x * -0.5f * fontSize) / fontSize; break;
	case Text::Alignment::RightBottom: offset = -instanceOffset.x; break;
	case Text::Alignment::RightTop: offset = -instanceOffset.x; break;

	case Text::Alignment::Left:
	case Text::Alignment::LeftBottom:
	case Text::Alignment::LeftTop:
		offset = 0.0f;
		break;
	default: abort();
	}

	if (offset != 0.0f)
	{
		auto offset4 = float4(offset, 0.0f, offset, 0.0f);
		for (uint32 i = lastNewLineIndex; i < instanceIndex; i++)
			instances[i].position += offset4;
	}

	lastNewLineIndex = instanceIndex;
	textSize.x = max(textSize.x, instanceOffset.x);
	instanceOffset.y -= newLineAdvance;
	instanceOffset.x = 0.0f;
}
static bool fillTextInstances(u32string_view value, Text::Properties properties, 
	OptView<FontAtlas> fontAtlasView, Text::Instance* instances, uint32& instanceCount, float2& textSize)
{
	auto chars = value.data(); const auto& glyphArray = fontAtlasView->getGlyphs();
	auto fontSize = fontAtlasView->getFontSize(); auto newLineAdvance = fontAtlasView->getNewLineAdvance();
	auto color = Color::white; auto isBold = properties.isBold, isItalic = properties.isItalic;
	auto size = float2::zero, instanceOffset = float2(0.0f, newLineAdvance * 0.5f);
	instanceOffset.y = -floorf(instanceOffset.y * fontSize) / fontSize;
	uint32 instanceIndex = 0, lastNewLineIndex = 0;

	const FontAtlas::GlyphMap* glyphs; uint32 atlasIndex;
	if (isBold && isItalic) { atlasIndex = 3; glyphs = &glyphArray[atlasIndex]; }
	else if (isItalic) { atlasIndex = 2; glyphs = &glyphArray[atlasIndex]; }
	else if (isBold) { atlasIndex = 1; glyphs = &glyphArray[atlasIndex]; }
	else { atlasIndex = 0; glyphs = &glyphArray[atlasIndex]; }

	for (psize i = 0; i < value.length(); i++)
	{
		auto c = chars[i];
		if (c == '\n')
		{
			updateTextNewLine(instances, fontSize, newLineAdvance, instanceOffset, 
				instanceIndex, lastNewLineIndex, size, properties.alignment);
			continue;
		}
		else if (c == '\t')
		{
			auto result = glyphs->find(U' ');
			if (result == glyphs->end())
				return false;
			instanceOffset.x += result->second.advance * 4.0f; // TODO: allow to adjust tab spacing size.
			continue;
		}
		else if (c == '<' && properties.useTags)
		{
			if (i + 2 < value.length() && chars[i + 2] == '>')
			{
				auto tag = chars[i + 1];
				if (tag == 'b')
				{
					if (isItalic) { atlasIndex = 3; glyphs = &glyphArray[atlasIndex]; }
					else { atlasIndex = 1; glyphs = &glyphArray[atlasIndex]; }
					isBold = true; i += 2; continue;
				}
				else if (tag == 'i')
				{
					if (isBold) { atlasIndex = 3; glyphs = &glyphArray[atlasIndex]; }
					else { atlasIndex = 2; glyphs = &glyphArray[atlasIndex]; }
					isItalic = true; i += 2; continue;
				}
				else if (tag == 'n')
				{
					updateTextNewLine(instances, fontSize, newLineAdvance, instanceOffset, 
						instanceIndex, lastNewLineIndex, size, properties.alignment);
					i += 2; continue;
				}
			}
			else if (i + 3 < value.length() && chars[i + 1] == '/' && chars[i + 3] == '>')
			{
				auto tag = chars[i + 2];
				if (tag == 'b')
				{
					if (isItalic) { atlasIndex = 2; glyphs = &glyphArray[atlasIndex]; }
					else { atlasIndex = 0; glyphs = &glyphArray[atlasIndex]; }
					isBold = false; i += 3; continue;
				}
				else if (tag == 'i')
				{
					if (isBold) { atlasIndex = 1; glyphs = &glyphArray[atlasIndex]; }
					else { atlasIndex = 0; glyphs = &glyphArray[atlasIndex]; }
					isItalic = false; i += 3; continue;
				}
				else if (tag == '#')
				{
					color = Color::white;
					i += 3; continue;
				}
			}
			else if (i + 8 < value.length() && chars[i + 1] == '#' && chars[i + 8] == '>')
			{
				color = Color(u32string_view(chars + i + 2, 6)); i+= 8;
			}
			else if (i + 10 < value.length() && chars[i + 1] == '#' && chars[i + 10] == '>')
			{
				color = Color(u32string_view(chars + i + 2, 8)); i += 10;
			}
			// TODO: more tags.
		}

		auto result = glyphs->find(c);
		if (result == glyphs->end())
		{
			result = glyphs->find(U'\0');
			if (result == glyphs->end())
				return false;
		}

		if (instanceOffset.x + result->second.advance > properties.maxAdvanceX)
		{
			updateTextNewLine(instances, fontSize, newLineAdvance, instanceOffset, 
				instanceIndex, lastNewLineIndex, size, properties.alignment);
		}

		if (result->second.value != Glyph::invisible)
		{
			Text::Instance instance;
			instance.position = result->second.position + 
				float4(instanceOffset, instanceOffset);
			instance.texCoords = result->second.texCoords;
			instance.atlasIndex = atlasIndex;
			instance.srgbColor = color;
			instances[instanceIndex++] = instance;
		}
		instanceOffset.x += result->second.advance;
	}

	size.x = max(size.x, instanceOffset.x);
	size.y = -instanceOffset.y;

	float offset; float4 offset4;
	switch (properties.alignment)
	{
	case Text::Alignment::Center:
		offset = floorf(instanceOffset.x * -0.5f * fontSize) / fontSize;
		offset4 = float4(offset, 0.0f, offset, 0.0f);
		for (uint32 i = lastNewLineIndex; i < instanceIndex; i++)
			instances[i].position += offset4;

		offset = floorf(size.y * 0.5f * fontSize) / fontSize;
		offset4 = float4(0.0f, offset, 0.0f, offset);
		for (uint32 i = 0; i < instanceIndex; i++)
			instances[i].position += offset4;
		break;
	case Text::Alignment::Left:
		offset = floorf(size.y * 0.5f * fontSize) / fontSize;
		offset4 = float4(0.0f, offset, 0.0f, offset);
		for (uint32 i = 0; i < instanceIndex; i++)
			instances[i].position += offset4;
		break;
	case Text::Alignment::Right:
		offset = -instanceOffset.x;
		offset4 = float4(offset, 0.0f, offset, 0.0f);
		for (uint32 i = lastNewLineIndex; i < instanceIndex; i++)
			instances[i].position += offset4;

		offset = floorf(size.y * 0.5f * fontSize) / fontSize;
		offset4 = float4(0.0f, offset, 0.0f, offset);
		for (uint32 i = 0; i < instanceIndex; i++)
			instances[i].position += offset4;
		break;
	case Text::Alignment::Bottom:
		offset = floorf(instanceOffset.x * -0.5f * fontSize) / fontSize;
		offset4 = float4(offset, 0.0f, offset, 0.0f);
		for (uint32 i = lastNewLineIndex; i < instanceIndex; i++)
			instances[i].position += offset4;

		offset = size.y;
		offset4 = float4(0.0f, offset, 0.0f, offset);
		for (uint32 i = 0; i < instanceIndex; i++)
			instances[i].position += offset4;
		break;
	case Text::Alignment::Top:
		offset = floorf(instanceOffset.x * -0.5f * fontSize) / fontSize;
		offset4 = float4(offset, 0.0f, offset, 0.0f);
		for (uint32 i = lastNewLineIndex; i < instanceIndex; i++)
			instances[i].position += offset4;
		break;
	case Text::Alignment::LeftBottom:
		offset = size.y;
		offset4 = float4(0.0f, offset, 0.0f, offset);
		for (uint32 i = 0; i < instanceIndex; i++)
			instances[i].position += offset4;
		break;
	case Text::Alignment::RightBottom:
		offset = -instanceOffset.x;
		offset4 = float4(offset, 0.0f, offset, 0.0f);
		for (uint32 i = lastNewLineIndex; i < instanceIndex; i++)
			instances[i].position += offset4;

		offset = size.y;
		offset4 = float4(0.0f, offset, 0.0f, offset);
		for (uint32 i = 0; i < instanceIndex; i++)
			instances[i].position += offset4;
		break;
	case Text::Alignment::RightTop:
		offset = -instanceOffset.x;
		offset4 = float4(offset, 0.0f, offset, 0.0f);
		for (uint32 i = lastNewLineIndex; i < instanceIndex; i++)
			instances[i].position += offset4;
		break;
	case Text::Alignment::LeftTop: break;
	default: abort();
	}

	instanceCount = instanceIndex;
	textSize = float2(size.x, size.y + newLineAdvance * 0.25f);
	return true;
}

//**********************************************************************************************************************
bool Text::isReady() const noexcept
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto instanceBufferView = graphicsSystem->get(instanceBuffer);
	if (!instanceBufferView->isReady())
		return false;

	auto fontAtlasView = TextSystem::Instance::get()->get(fontAtlas);
	auto imageView = graphicsSystem->get(fontAtlasView->getImage());
	if (!imageView->isReady())
		return false;

	return true;
}

bool Text::update(u32string_view value, uint32 fontSize, Properties properties, 
	const FontArray& fonts, Image::Usage atlasUsage, Buffer::Usage instanceUsage, bool shrink)
{
	GARDEN_ASSERT(!value.empty());
	GARDEN_ASSERT(fontSize > 0);

	SET_CPU_ZONE_SCOPED("Text Update");
	auto textSystem = TextSystem::Instance::get();

	ID<FontAtlas> newFontAtlas = {}; OptView<FontAtlas> fontAtlasView = {};
	if (atlasShared || shrink || !fontAtlas)
	{
		auto fontArray = fonts;
		newFontAtlas = textSystem->createFontAtlas(value, std::move(fontArray), fontSize, atlasUsage);
		if (!newFontAtlas)
			return false;
		fontAtlasView = OptView<FontAtlas>(textSystem->get(newFontAtlas));
	}
	else
	{
		fontAtlasView = OptView<FontAtlas>(textSystem->get(fontAtlas));
		if (!fontAtlasView->update(value, fontSize, atlasUsage, shrink))
			return false;
	}

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto newBinarySize = (uint64)value.size() * sizeof(Text::Instance);
	auto stagingBuffer = graphicsSystem->createStagingBuffer(Buffer::CpuAccess::RandomReadWrite, newBinarySize);
	SET_RESOURCE_DEBUG_NAME(stagingBuffer, "buffer.staging.textInstances" + to_string(*stagingBuffer));

	auto stagingView = graphicsSystem->get(stagingBuffer);
	auto instances = (Text::Instance*)stagingView->getMap();

	uint32 instanceCount; float2 textSize;
	if (!fillTextInstances(value, properties, fontAtlasView, instances, instanceCount, textSize))
	{
		graphicsSystem->destroy(stagingBuffer);
		textSystem->destroy(newFontAtlas);
		return false;
	}
	stagingView->flush();

	if (newFontAtlas)
	{
		textSystem->destroy(fontAtlas);
		fontAtlas = Ref<FontAtlas>(newFontAtlas);
	}

	uint64 currBinarySize = 0;
	if (instanceBuffer && !shrink)
	{
		auto bufferView = graphicsSystem->get(instanceBuffer);
		if (bufferView->isReady()) // Note: it may be asyn transfering right now.
			currBinarySize = bufferView->getBinarySize();
	}

	if (newBinarySize > currBinarySize)
	{
		graphicsSystem->destroy(instanceBuffer);
		instanceBuffer = graphicsSystem->createBuffer(instanceUsage, Buffer::CpuAccess::None, 
			newBinarySize, Buffer::Location::Auto, Buffer::Strategy::Size);
		SET_RESOURCE_DEBUG_NAME(instanceBuffer, "buffer.storage.text" + to_string(*instanceBuffer));
	}
	
	Buffer::copy(stagingBuffer, instanceBuffer);
	graphicsSystem->destroy(stagingBuffer);

	this->instanceCount = instanceCount;
	this->size = textSize;
	this->properties = properties;
	return true;
}

//**********************************************************************************************************************
float2 Text::calcCaretAdvance(u32string_view value, psize charIndex)
{
	if (value.empty())
		return float2::zero;

	GARDEN_ASSERT(charIndex < value.length());
	auto fontAtlasView = TextSystem::Instance::get()->get(fontAtlas);
	auto chars = value.data(); const auto& glyphArray = fontAtlasView->getGlyphs();
	auto fontSize = fontAtlasView->getFontSize(); auto newLineAdvance = fontAtlasView->getNewLineAdvance();
	auto isBold = properties.isBold, isItalic = properties.isItalic;
	auto advance = float2::zero; auto lineSizeX = 0.0f;

	const FontAtlas::GlyphMap* glyphs; uint32 atlasIndex;
	if (isBold && isItalic) { atlasIndex = 3; glyphs = &glyphArray[atlasIndex]; }
	else if (isItalic) { atlasIndex = 2; glyphs = &glyphArray[atlasIndex]; }
	else if (isBold) { atlasIndex = 1; glyphs = &glyphArray[atlasIndex]; }
	else { atlasIndex = 0; glyphs = &glyphArray[atlasIndex]; }

	for (psize i = 0; i < value.length(); i++)
	{
		auto c = chars[i];
		if (c == '\n')
		{
			if (i >= charIndex)
				break;
			advance.y -= newLineAdvance; advance.x = lineSizeX = 0.0f;
			continue;
		}
		else if (c == '\t')
		{
			auto result = glyphs->find(U' ');
			if (result == glyphs->end())
				return float2::minusOne;
			advance.x += result->second.advance * 4.0f; // TODO: allow to adjust tab spacing size.
			continue;
		}
		else if (c == '<' && properties.useTags)
		{
			if (i + 2 < value.length() && chars[i + 2] == '>')
			{
				auto tag = chars[i + 1];
				if (tag == 'b')
				{
					if (isItalic) { atlasIndex = 3; glyphs = &glyphArray[atlasIndex]; }
					else { atlasIndex = 1; glyphs = &glyphArray[atlasIndex]; }
					isBold = true; i += 2; continue;
				}
				else if (tag == 'i')
				{
					if (isBold) { atlasIndex = 3; glyphs = &glyphArray[atlasIndex]; }
					else { atlasIndex = 2; glyphs = &glyphArray[atlasIndex]; }
					isItalic = true; i += 2; continue;
				}
			}
			else if (i + 3 < value.length() && chars[i + 1] == '/' && chars[i + 3] == '>')
			{
				auto tag = chars[i + 2];
				if (tag == 'b')
				{
					if (isItalic) { atlasIndex = 2; glyphs = &glyphArray[atlasIndex]; }
					else { atlasIndex = 0; glyphs = &glyphArray[atlasIndex]; }
					isBold = false; i += 3; continue;
				}
				else if (tag == 'i')
				{
					if (isBold) { atlasIndex = 1; glyphs = &glyphArray[atlasIndex]; }
					else { atlasIndex = 0; glyphs = &glyphArray[atlasIndex]; }
					isItalic = false; i += 3; continue;
				}
				else if (tag == '#')
				{
					i += 3; continue;
				}
			}
			else if (i + 8 < value.length() && chars[i + 1] == '#' && chars[i + 8] == '>')
			{
				i += 8; continue;
			}
			else if (i + 10 < value.length() && chars[i + 1] == '#' && chars[i + 10] == '>')
			{
				i += 10; continue;
			}
		}

		auto result = glyphs->find(c);
		if (result == glyphs->end())
		{
			result = glyphs->find(U'\0');
			if (result == glyphs->end())
				return float2::minusOne;
		}

		if (i < charIndex)
			advance.x += result->second.advance;
		lineSizeX += result->second.advance;
	}

	switch (properties.alignment)
	{
	default:
		abort();
	case Text::Alignment::Center:
		advance.x += lineSizeX * -0.5f;
		advance.y += (size.y - (newLineAdvance * 0.5f + newLineAdvance * 0.25f)) * 0.5f;
		break;
	case Text::Alignment::Left:
		advance.y += (size.y - (newLineAdvance * 0.5f + newLineAdvance * 0.25f)) * 0.5f;
		break;
	case Text::Alignment::Right:
		advance.x += -lineSizeX;
		advance.y += (size.y - (newLineAdvance * 0.5f + newLineAdvance * 0.25f)) * 0.5f;
		break;
	case Text::Alignment::Bottom:
		advance.x += lineSizeX * -0.5f;
		advance.y += size.y - newLineAdvance * 0.5f;
		break;
	case Text::Alignment::Top:
		advance.x += lineSizeX * -0.5f;
		advance.y += newLineAdvance * -0.25f;
		break;
	case Text::Alignment::LeftBottom:
		advance.y += size.y - newLineAdvance * 0.5f;
		break;
	case Text::Alignment::LeftTop:
		advance.y += newLineAdvance * -0.25f;
		break;
	case Text::Alignment::RightBottom:
		advance.x += -lineSizeX;
		advance.y += size.y - newLineAdvance * 0.5f;
		break;
	case Text::Alignment::RightTop:
		advance.x += -lineSizeX;
		advance.y += newLineAdvance * -0.25f;
		break;
	}

	return advance;
}

psize Text::calcCaretIndex(u32string_view value, float2 caretAdvance)
{
	auto bestDistance = INFINITY;
	psize index = 0;

	// TODO: too heavy, use better solution!
	for (psize i = 0; i < value.length(); i++)
	{
		auto checkAdvance = calcCaretAdvance(value, i);
		if (checkAdvance == float2::minusOne)
			return SIZE_MAX;

		auto distance = distanceSq(caretAdvance, checkAdvance);
		if (distance < bestDistance)
		{
			bestDistance = distance; index = i;
		}
	}
	return index;
}

//**********************************************************************************************************************
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
	auto manager = Manager::Instance::get();
	texts.clear(manager->isRunning);
	fontAtlases.clear(manager->isRunning);
	fonts.clear(manager->isRunning);

	if (manager->isRunning)
	{
		auto result = FT_Done_FreeType((FT_Library)ftLibrary);
		GARDEN_ASSERT_MSG(!result, "Failed to deinitialize FreeType");
	}

	unsetSingleton();
}

void TextSystem::update()
{
	SET_CPU_ZONE_SCOPED("Texts Update");

	texts.dispose();
	fontAtlases.dispose();
	fonts.dispose();
}

//**********************************************************************************************************************
ID<FontAtlas> TextSystem::createFontAtlas(u32string_view chars, 
	FontArray&& fonts, uint32 fontSize, Image::Usage imageUsage)
{
	GARDEN_ASSERT(!chars.empty());
	GARDEN_ASSERT(fonts.size() == 4);
	GARDEN_ASSERT(fontSize > 0);

	for (const auto& variants : fonts)
	{
		if (!variants.empty())
			continue;
		GARDEN_LOG_DEBUG("Failed to create font atlas, missing fonts.");
		return {};
	}

	auto fontAtlas = fontAtlases.create();
	auto fontAtlasView = fontAtlases.get(fontAtlas);
	fontAtlasView->fonts = std::move(fonts);

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto stopRecording = graphicsSystem->tryStartRecording(CommandBufferType::TransferOnly);

	if (!fontAtlasView->update(chars, fontSize))
	{
		if (stopRecording)
			graphicsSystem->stopRecording();
		fontAtlases.destroy(fontAtlas);
		return {};
	}
	
	if (stopRecording)
		graphicsSystem->stopRecording();
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
ID<Text> TextSystem::createText(u32string_view value, const Ref<FontAtlas>& fontAtlas, 
	Text::Properties properties, bool isAtlasShared)
{
	GARDEN_ASSERT(!value.empty());
	GARDEN_ASSERT(fontAtlas);

	auto text = texts.create();
	auto textView = texts.get(text);
	textView->fontAtlas = fontAtlas;
	textView->atlasShared = isAtlasShared;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto stopRecording = graphicsSystem->tryStartRecording(CommandBufferType::TransferOnly);

	auto fontAtlasView = fontAtlases.get(fontAtlas);
	if (!textView->update(value, fontAtlasView->getFontSize(), properties))
	{
		if (stopRecording)
			graphicsSystem->stopRecording();
		texts.destroy(text);
		return {};
	}

	if (stopRecording)
		graphicsSystem->stopRecording();
	return text;
}
void TextSystem::destroy(ID<Text> text)
{
	if (!text)
		return;

	auto textView = texts.get(text);
	GraphicsSystem::Instance::get()->destroy(textView->instanceBuffer);
	destroy(textView->fontAtlas);
	texts.destroy(text);
}