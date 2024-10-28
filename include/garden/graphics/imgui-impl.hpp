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

#pragma once
#include "garden/graphics/vulkan.hpp"

#if GARDEN_EDITOR
#define IMGUI_ENABLE_FREETYPE
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

namespace garden::graphics
{

class ImGuiData final
{
public:
	inline static vk::RenderPass renderPass = {};
	inline static vector<vk::Framebuffer> framebuffers = {};
};

static void imGuiCheckVkResult(VkResult result)
{
	if (result == VK_SUCCESS)
		return;
		
	auto resultString = vk::to_string(vk::Result(result));
	fprintf(stderr, "IMGUI::VULKAN::ERROR: %s\n", resultString.c_str());
	if (result < 0)
		throw runtime_error("IMGUI::VULKAN::ERROR: " + resultString);
}

static void setImGuiStyle()
{
	auto& style = ImGui::GetStyle();
	style.FrameBorderSize = 1.0f;
	style.TabBorderSize = 1.0f;
	style.IndentSpacing = 8.0f;
	style.ScrollbarSize = 12.0f;
	style.GrabMinSize = 7.0f;
	style.FramePadding = ImVec2(4.0f, 4.0f);
	style.WindowRounding = 2.0f;
	style.ChildRounding = 2.0f;
	style.FrameRounding = 2.0f;
	style.PopupRounding = 2.0f;
	style.ScrollbarRounding = 2.0f;
	style.GrabRounding = 2.0f;
	style.SeparatorTextBorderSize = 2.0f;

	#if GARDEN_OS_MACOS
	style.AntiAliasedFill = false;
	#endif

	auto colors = style.Colors;
	colors[ImGuiCol_Text] = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
	colors[ImGuiCol_TextDisabled] = ImVec4(0.549f, 0.549f, 0.549f, 1.0f);
	colors[ImGuiCol_WindowBg] = ImVec4(0.122f, 0.122f, 0.122f, 0.996f);
	colors[ImGuiCol_PopupBg] = ImVec4(0.122f, 0.122f, 0.122f, 0.996f);
	colors[ImGuiCol_Border] = ImVec4(0.267f, 0.267f, 0.267f, 1.0f);
	colors[ImGuiCol_FrameBg] = ImVec4(0.165f, 0.165f, 0.165f, 1.0f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.259f, 0.267f, 0.267f, 1.0f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.098f, 0.247f, 0.388f, 1.0f);
	colors[ImGuiCol_TitleBg] = ImVec4(0.094f, 0.094f, 0.094f, 0.992f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.122f, 0.122f, 0.122f, 1.0f);
	colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.094f, 0.094f, 0.094f, 0.992f);
	colors[ImGuiCol_MenuBarBg] = ImVec4(0.094f, 0.094f, 0.094f, 0.992f);
	// TODO: scroll bar
	colors[ImGuiCol_CheckMark] = ImVec4(0.0f, 0.471f, 0.831f, 1.0f);
	colors[ImGuiCol_SliderGrab] = ImVec4(0.0f, 0.471f, 0.831f, 1.0f);
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.0f, 0.471f, 0.831f, 1.0f); // TODO:
	colors[ImGuiCol_Button] = ImVec4(0.0f, 0.471f, 0.831f, 1.0f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.024f, 0.435f, 0.757f, 1.0f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.098f, 0.247f, 0.388f, 1.0f);
	colors[ImGuiCol_Header] = ImVec4(0.094f, 0.094f, 0.094f, 1.0f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.259f, 0.267f, 0.267f, 1.0f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.098f, 0.247f, 0.388f, 1.0f);
	colors[ImGuiCol_Separator] = ImVec4(0.251f, 0.251f, 0.251f, 1.0f);
	colors[ImGuiCol_SeparatorHovered] = ImVec4(0.251f, 0.251f, 0.251f, 1.0f); // TODO: 
	colors[ImGuiCol_SeparatorActive] = ImVec4(0.251f, 0.251f, 0.251f, 1.0f); // TODO:
	colors[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.471f, 0.831f, 1.0f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.024f, 0.435f, 0.757f, 1.0f);
	colors[ImGuiCol_ResizeGripActive] = ImVec4(0.098f, 0.247f, 0.388f, 1.0f);
	colors[ImGuiCol_Tab] = ImVec4(0.094f, 0.094f, 0.094f, 1.0f);
	colors[ImGuiCol_TabHovered] = ImVec4(0.024f, 0.435f, 0.757f, 1.0f);
	colors[ImGuiCol_TabActive] = ImVec4(0.024f, 0.435f, 0.757f, 1.0f);
	colors[ImGuiCol_TabUnfocused] = ImVec4(0.094f, 0.094f, 0.094f, 1.0f); // TODO: where it used?
	colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.024f, 0.435f, 0.757f, 1.0f);
	colors[ImGuiCol_PlotHistogram] = ImVec4(0.969f, 0.510f, 0.106f, 1.0f);
	colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.976f, 0.627f, 0.318f, 1.0f);
	colors[ImGuiCol_PlotLines] = ImVec4(0.969f, 0.510f, 0.106f, 1.0f); // TODO:
	colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.976f, 0.627f, 0.318f, 1.0f); // TODO:
	colors[ImGuiCol_TableHeaderBg] = ImVec4(0.094f, 0.094f, 0.094f, 1.0f);
	colors[ImGuiCol_TableBorderStrong] = ImVec4(0.267f, 0.267f, 0.267f, 1.0f);
	colors[ImGuiCol_TableBorderLight] = ImVec4(0.267f, 0.267f, 0.267f, 0.8f);
	colors[ImGuiCol_TextSelectedBg] = ImVec4(0.149f, 0.310f, 0.471f, 1.0f);
	colors[ImGuiCol_NavHighlight] = ImVec4(0.0f, 0.471f, 0.831f, 1.0f);
	colors[ImGuiCol_DragDropTarget] = ImVec4(0.0f, 0.471f, 0.831f, 1.0f); // TODO:
	colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.831f);
	// TODO: others undeclared
}

} // namespace garden::graphics
#endif