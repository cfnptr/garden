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
 * @brief Common immediate GUI functions. (ImGui)
 */

#pragma once
#include "garden/defines.hpp"
#include "math/color.hpp"

#define IMGUI_ENABLE_FREETYPE
#define IMGUI_DISABLE_DEFAULT_FONT
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
static bool Combo(const char* label, T* currentItem, const char* items)
{
	GARDEN_ASSERT(currentItem);
	auto item = (int)*currentItem;
	auto result = ImGui::Combo(label, &item, items);
	*currentItem = (T)item;
	return result;
}

static bool SliderFloat4(const char* label, f32x4* value, float min,
	float max, const char* format = "%.3f", ImGuiSliderFlags flags = 0)
{
	return ImGui::SliderFloat2(label, (float*)value, min, max, format, flags);
}
static bool SliderFloat3(const char* label, f32x4* value, float min,
	float max, const char* format = "%.3f", ImGuiSliderFlags flags = 0)
{
	return ImGui::SliderFloat3(label, (float*)value, min, max, format, flags);
}
static bool SliderFloat2(const char* label, f32x4* value, float min,
	float max, const char* format = "%.3f", ImGuiSliderFlags flags = 0)
{
	return ImGui::SliderFloat2(label, (float*)value, min, max, format, flags);
}
static bool SliderFloat2(const char* label, float2* value, float min,
	float max, const char* format = "%.3f", ImGuiSliderFlags flags = 0)
{
	return ImGui::SliderFloat2(label, (float*)value, min, max, format, flags);
}

static bool DragFloat4(const char* label, f32x4* value, float speed = 1.0f, float min = 0.0f,
	float max = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = 0)
{
	return ImGui::DragFloat4(label, (float*)value, speed, min, max, format, flags);
}
static bool DragFloat3(const char* label, f32x4* value, float speed = 1.0f, float min = 0.0f,
	float max = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = 0)
{
	return ImGui::DragFloat4(label, (float*)value, speed, min, max, format, flags);
}
static bool DragFloat2(const char* label, f32x4* value, float speed = 1.0f, float min = 0.0f,
	float max = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = 0)
{
	return ImGui::DragFloat2(label, (float*)value, speed, min, max, format, flags);
}
static bool DragFloat2(const char* label, float2* value, float speed = 1.0f, float min = 0.0f,
	float max = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = 0)
{
	return ImGui::DragFloat2(label, (float*)value, speed, min, max, format, flags);
}

static bool SliderInt4(const char* label, i32x4* value, int min,
	int max, const char* format = "%d", ImGuiSliderFlags flags = 0)
{
	return ImGui::SliderInt4(label, (int*)value, min, max, format, flags);
}
static bool SliderInt3(const char* label, i32x4* value, int min,
	int max, const char* format = "%d", ImGuiSliderFlags flags = 0)
{
	return ImGui::SliderInt3(label, (int*)value, min, max, format, flags);
}
static bool SliderInt2(const char* label, i32x4* value, int min,
	int max, const char* format = "%d", ImGuiSliderFlags flags = 0)
{
	return ImGui::SliderInt2(label, (int*)value, min, max, format, flags);
}
static bool SliderInt2(const char* label, int2* value, int min,
	int max, const char* format = "%d", ImGuiSliderFlags flags = 0)
{
	return ImGui::SliderInt2(label, (int*)value, min, max, format, flags);
}

static bool DragInt4(const char* label, i32x4* value, float speed = 1.0f, int min = 0,
	int max = 0, const char* format = "%d", ImGuiSliderFlags flags = 0)
{
	return ImGui::DragInt4(label, (int*)value, speed, min, max, format, flags);
}
static bool DragInt3(const char* label, i32x4* value, float speed = 1.0f, int min = 0,
	int max = 0, const char* format = "%d", ImGuiSliderFlags flags = 0)
{
	return ImGui::DragInt4(label, (int*)value, speed, min, max, format, flags);
}
static bool DragInt2(const char* label, i32x4* value, float speed = 1.0f, int min = 0,
	int max = 0, const char* format = "%d", ImGuiSliderFlags flags = 0)
{
	return ImGui::DragInt2(label, (int*)value, speed, min, max, format, flags);
}
static bool DragInt2(const char* label, int2* value, float speed = 1.0f, int min = 0,
	int max = 0, const char* format = "%d", ImGuiSliderFlags flags = 0)
{
	return ImGui::DragInt2(label, (int*)value, speed, min, max, format, flags);
}

static bool ColorEdit4(const char* label, f32x4* color, ImGuiColorEditFlags flags = 0)
{
	return ImGui::ColorEdit4(label, (float*)color, flags);
}
static bool ColorEdit3(const char* label, f32x4* color, ImGuiColorEditFlags flags = 0)
{
	return ImGui::ColorEdit3(label, (float*)color, flags);
}
static bool ColorEdit4(const char* label, Color* color, ImGuiColorEditFlags flags = 0)
{
	GARDEN_ASSERT(color);
	auto floatColor = (f32x4)*color;
	auto result = ColorEdit4(label, &floatColor, flags);
	*color = (Color)floatColor;
	return result;
}
static bool ColorEdit3(const char* label, Color* color, ImGuiColorEditFlags flags = 0)
{
	GARDEN_ASSERT(color);
	auto floatColor = (f32x4)*color;
	auto result = ColorEdit3(label, &floatColor, flags);
	*color = (Color)floatColor;
	return result;
}

}; // namespace ImGui

namespace garden
{

static bool findCaseInsensitive(const string& haystack, const string& needle) noexcept
{
	auto it = search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), [](char a, char b)
	{
		return toupper(a) == toupper(b);
	});
	return (it != haystack.end());
}
static bool find(const string& haystack, const string& needle, bool caseSensitive) noexcept
{
	if (caseSensitive)
		return haystack.find(needle) != string::npos;
	return findCaseInsensitive(haystack, needle);
}
static bool find(const string& haystack, const string& needle, uint32 id, bool caseSensitive)
{
	auto needleID = (uint32)strtoul(needle.c_str(), nullptr, 10);
	return id == needleID || find(haystack, needle, caseSensitive);
}

}; // namespace garden