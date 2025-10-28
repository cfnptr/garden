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

#include "garden/font.hpp"

#include "ft2build.h"
#include FT_FREETYPE_H

using namespace garden;

bool Font::destroy()
{
	if (faces.empty())
		return true;

	for (auto face : faces)
	{
		auto result = FT_Done_Face((FT_Face)face);
		GARDEN_ASSERT_MSG(!result, "Failed to destroy FreeType font");
	}
	
	delete[] data;
	return true;
}