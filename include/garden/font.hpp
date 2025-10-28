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
#include "garden/defines.hpp"
#include "ecsm.hpp"

namespace garden
{

using namespace ecsm;

/**
 * @brief Font data container.
 */
struct Font final
{
	vector<void*> faces;
	uint8* data = nullptr;

	bool destroy();
};

/**
 * @brief Array of the font variants. type[variant[font]]
 * @details type - regular, bold, italic, boldItalic.
 */
using FontArray = vector<vector<Ref<Font>>>;

} // namespace garden