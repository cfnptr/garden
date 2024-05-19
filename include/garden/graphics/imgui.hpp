// Copyright 2022-2024 Nikita Fediuchin. All rights reserved.
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
 * @brief ImGui extensions.
 */

#pragma once
#include "garden/defines.hpp"

#if GARDEN_EDITOR
#include "math/color.hpp"
#include "garden/graphics/vulkan.hpp"

#define IMGUI_ENABLE_FREETYPE
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

namespace ImGui
{

using namespace math;

/**
 * @brief Enum class combo box. 
 * 
 * @tparam T target enum type
 * @param[in] label combo box label
 * @param[in,out] currentItem selected item
 * @param[in] items all available item list
 */
template<typename T = int>
static bool Combo(const char* label, T& currentItem, const char* items)
{
	auto item = (int)currentItem;
	auto result = ImGui::Combo(label, &item, items);
	currentItem = (T)item;
	return result;
}

static bool ColorEdit4(const char* label, Color& color, ImGuiColorEditFlags flags = 0)
{
	auto floatColor = (float4)color;
	auto result = ImGui::ColorEdit4(label, (float*)&floatColor, flags);
	color = (Color)floatColor;
	return result;
}
static bool ColorEdit3(const char* label, Color& color, ImGuiColorEditFlags flags = 0)
{
	auto floatColor = (float3)color;
	auto result = ImGui::ColorEdit3(label, (float*)&floatColor, flags);
	color = (Color)floatColor;
	return result;
}

}; // ImGui

namespace garden
{

static bool findCaseInsensitive(const string& haystack, const string& needle)
{
	auto it = search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), [](char a, char b)
	{
		return toupper(a) == toupper(b);
	});
	return (it != haystack.end());
}
static bool find(const string& haystack, const string& needle, bool caseSensitive)
{
	if (caseSensitive)
		return haystack.find(needle) != string::npos;
	return findCaseInsensitive(haystack, needle);
}

}; // garden
#endif