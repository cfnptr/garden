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
#include "garden/system/transform.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/thread.hpp"
#include "garden/system/input.hpp"
#include "garden/profiler.hpp"

#include "ft2build.h"
#include FT_FREETYPE_H

using namespace garden;

bool Text::destroy()
{
	// TODO:
	return true;
}

TextSystem::TextSystem(bool setSingleton) : Singleton(setSingleton)
{
	FT_Library ftLibrary = nullptr;
	auto error = FT_Init_FreeType(&ftLibrary);
	if (error)
		throw GardenError("Failed to initialize FreeType. (error: " + string(FT_Error_String(error)) + ")");
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
	fonts.dispose();
}

static void bakeStringGlyphs(u32string_view string, tsl::robin_map<uint32, Glyph>& glyphs)
{
	Glyph glyph;
	for (auto value : string)
	{
		if (value == '\n') continue;
		if (value == '\t') value = ' ';
		glyph.value = value;
		glyphs.emplace(value, glyph);
	}
}

ID<Text> TextSystem::createText()
{
	return {}; // TODO:
}