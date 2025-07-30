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

#include "garden/system/render/imgui.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/render/forward.hpp"
#include "garden/system/resource.hpp"
#include "garden/graphics/api.hpp"
#include "garden/graphics/glfw.hpp" // Note: Defined before ImGUI
#include "garden/graphics/imgui.hpp"
#include "garden/profiler.hpp"

#ifdef _WIN32
#undef APIENTRY
#ifndef GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include <GLFW/glfw3native.h>   // for glfwGetWin32Window()
#endif
#ifdef __APPLE__
#ifndef GLFW_EXPOSE_NATIVE_COCOA
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3native.h>   // for glfwGetCocoaWindow()
#endif

using namespace garden;

static void setImGuiStyle()
{
	auto& style = ImGui::GetStyle();
	style.IndentSpacing = 8.0f;
	style.ScrollbarSize = 12.0f;
	style.GrabMinSize = 7.0f;
	style.TabBarBorderSize = 2.0f;
	style.FramePadding = ImVec2(4.0f, 4.0f);
	style.FrameBorderSize = 1.0f;
	style.WindowRounding = 3.0f;
	style.ChildRounding = 3.0f;
	style.FrameRounding = 3.0f;
	style.PopupRounding = 3.0f;
	style.ScrollbarRounding = 24.0f;
	style.GrabRounding = 2.0f;
	style.TabRounding = 5.0f;
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
	colors[ImGuiCol_PlotHistogram] = ImVec4(0.42f, 0.6f, 0.33, 1.0f);
	colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.976f, 0.627f, 0.318f, 1.0f);
	colors[ImGuiCol_PlotLines] = ImVec4(0.42f, 0.6f, 0.33, 1.0f); // TODO:
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

static ID<GraphicsPipeline> createPipeline()
{
	ID<Framebuffer> framebuffer;
	if (DeferredRenderSystem::Instance::has())
		framebuffer = DeferredRenderSystem::Instance::get()->getUiFramebuffer();
	else
		framebuffer = ForwardRenderSystem::Instance::get()->getColorFramebuffer();
	ResourceSystem::GraphicsOptions options;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("imgui", framebuffer, options);
}
static ID<Sampler> createLinearSampler()
{
	Sampler::State state;
	state.setFilter(Sampler::Filter::Linear);
	auto sampler = GraphicsSystem::Instance::get()->createSampler(state);
	SET_RESOURCE_DEBUG_NAME(sampler, "sampler.imgui.linear");
	return sampler;
}
static ID<Sampler> createNearestSampler()
{
	auto sampler = GraphicsSystem::Instance::get()->createSampler({});
	SET_RESOURCE_DEBUG_NAME(sampler, "sampler.imgui.nearest");
	return sampler;
}

static DescriptorSet::Uniforms getUniforms(ID<ImageView> texture)
{
	return { { "tex", DescriptorSet::Uniform(texture) } };
}
static DescriptorSet::Samplers getSamplers(ID<Sampler> sampler)
{
	return { { "tex", sampler } };
}

static void createBuffers(vector<ID<Buffer>>& buffers, uint64 bufferSize, Buffer::Usage usage)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto inFlightCount = graphicsSystem->getInFlightCount();
	buffers.resize(inFlightCount);

	for (uint32 i = 0; i < inFlightCount; i++)
	{
		auto buffer = graphicsSystem->createBuffer(usage, Buffer::CpuAccess::SequentialWrite,
			bufferSize, Buffer::Location::Auto, Buffer::Strategy::Size);
		SET_RESOURCE_DEBUG_NAME(buffer, "buffer.imgui." + string(toString(usage)) + to_string(i));
		buffers[i] = buffer;
	}
}

//**********************************************************************************************************************
ImGuiRenderSystem::ImGuiRenderSystem(bool setSingleton, 
	const fs::path& fontPath) : Singleton(setSingleton), fontPath(fontPath)
{
	ECSM_SUBSCRIBE_TO_EVENT("PreInit", ImGuiRenderSystem::preInit);
	ECSM_SUBSCRIBE_TO_EVENT("PostInit", ImGuiRenderSystem::postInit);
	ECSM_SUBSCRIBE_TO_EVENT("PostDeinit", ImGuiRenderSystem::postDeinit);
	ECSM_SUBSCRIBE_TO_EVENT("Update", ImGuiRenderSystem::update);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	setImGuiStyle();
}
ImGuiRenderSystem::~ImGuiRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ImGui::DestroyContext();

		ECSM_UNSUBSCRIBE_FROM_EVENT("PreInit", ImGuiRenderSystem::preInit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("PostInit", ImGuiRenderSystem::postInit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("PostDeinit", ImGuiRenderSystem::postDeinit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Update", ImGuiRenderSystem::update);
	}
	unsetSingleton();
}

#ifdef _WIN32
static WNDPROC prevWndProc = nullptr;

// Note: GLFW doesn't allow to distinguish Mouse vs TouchScreen vs Pen.
// Add support for Win32 (based on imgui_impl_win32), because we rely on _TouchScreen info to trickle inputs differently.
static ImGuiMouseSource getMouseSourceFromMessageExtraInfo()
{
	LPARAM extra_info = ::GetMessageExtraInfo();
	if ((extra_info & 0xFFFFFF80) == 0xFF515700)
		return ImGuiMouseSource_Pen;
	if ((extra_info & 0xFFFFFF80) == 0xFF515780)
		return ImGuiMouseSource_TouchScreen;
	return ImGuiMouseSource_Mouse;
}
static LRESULT CALLBACK imGuiWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_MOUSEMOVE: case WM_NCMOUSEMOVE:
	case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_LBUTTONUP:
	case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: case WM_RBUTTONUP:
	case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: case WM_MBUTTONUP:
	case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: case WM_XBUTTONUP:
		ImGui::GetIO().AddMouseSourceEvent(getMouseSourceFromMessageExtraInfo());
		break;
	}
	return ::CallWindowProcW(prevWndProc, hWnd, msg, wParam, lParam);
}
#endif

//**********************************************************************************************************************
void ImGuiRenderSystem::preInit()
{
	ECSM_SUBSCRIBE_TO_EVENT("Input", ImGuiRenderSystem::input);
	ECSM_SUBSCRIBE_TO_EVENT("Render", ImGuiRenderSystem::render);

	auto& io = ImGui::GetIO();
	auto graphicsAPI = GraphicsAPI::get();
	auto inputSystem = InputSystem::Instance::get();
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto windowSize = inputSystem->getWindowSize();
	auto pixelRatio = (float2)graphicsSystem->getFramebufferSize() / windowSize;
	io.DisplaySize = ImVec2(windowSize.x, windowSize.y);
	io.FontGlobalScale = 1.0f / std::max(pixelRatio.x, pixelRatio.y);
	io.DisplayFramebufferScale = ImVec2(pixelRatio.x, pixelRatio.y);
	// TODO: dynamically detect when system scale is changed or moved to another monitor and recreate fonts.

	io.BackendPlatformName = "Garden ImGui";
	io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
	io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;

	auto& platformIO = ImGui::GetPlatformIO();
	platformIO.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text)
	{
		InputSystem::Instance::get()->setClipboard(text);
	};
	platformIO.Platform_GetClipboardTextFn = [](ImGuiContext*)
	{
		auto inputSystem = InputSystem::Instance::get();
		return inputSystem->getClipboard().empty() ? nullptr : inputSystem->getClipboard().c_str();
	};

    auto mainViewport = ImGui::GetMainViewport();
	mainViewport->PlatformHandle = graphicsAPI->window;
	#ifdef _WIN32
	mainViewport->PlatformHandleRaw = glfwGetWin32Window((GLFWwindow*)graphicsAPI->window);
	#elif defined(__APPLE__)
	mainViewport->PlatformHandleRaw = (void*)glfwGetCocoaWindow((GLFWwindow*)graphicsAPI->window);
	#else
    IM_UNUSED(mainViewport);
	#endif

    // Note: Register a WndProc hook so we can intercept some messages.
	#ifdef _WIN32
    prevWndProc = (WNDPROC)::GetWindowLongPtrW((HWND)mainViewport->PlatformHandleRaw, GWLP_WNDPROC);
    GARDEN_ASSERT(prevWndProc != nullptr);
    ::SetWindowLongPtrW((HWND)mainViewport->PlatformHandleRaw, GWLP_WNDPROC, (LONG_PTR)imGuiWndProc);
	#endif

	auto contentScale = inputSystem->getContentScale();
	auto fontSize = 14.0f * std::max(contentScale.x, contentScale.y);

	#if GARDEN_DEBUG
	auto fontString = (GARDEN_RESOURCES_PATH / fontPath).generic_string();
	auto fontResult = io.Fonts->AddFontFromFileTTF(fontString.c_str(), fontSize);
	GARDEN_ASSERT_MSG(fontResult, "Failed to load ImGui font [" + fontString + "]");
	#else
	auto& packReader = ResourceSystem::Instance::get()->getPackReader();
	auto fontIndex = packReader.getItemIndex(fontPath);
	auto fontDataSize = packReader.getItemDataSize(fontIndex);
	auto fontData = malloc<uint8>(fontDataSize);
	packReader.readItemData(fontIndex, fontData);
	io.Fonts->AddFontFromMemoryTTF(fontData, fontDataSize, fontSize);
	#endif

	unsigned char* pixels; int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	fontTexture = graphicsSystem->createImage(Image::Format::UnormR8G8B8A8, 
		Image::Usage::Sampled | Image::Usage::TransferDst | Image::Usage::TransferQ, 
		{ { pixels } }, uint2(width, height), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(fontTexture, "image.imgui.fontTexture");

	auto fontTextureView = graphicsSystem->get(fontTexture);
	io.Fonts->SetTexID((ImTextureID)*fontTextureView->getDefaultView());

	if (isEnabled)
	{
		if (!pipeline)
			pipeline = createPipeline();
		if (!linearSampler)
			linearSampler = createLinearSampler();
		if (!nearestSampler)
			nearestSampler = createNearestSampler();
	}
}
void ImGuiRenderSystem::postInit()
{
	ECSM_SUBSCRIBE_TO_EVENT("UiRender", ImGuiRenderSystem::uiRender);
}

//**********************************************************************************************************************
void ImGuiRenderSystem::postDeinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(indexBuffers);
		graphicsSystem->destroy(vertexBuffers);
		graphicsSystem->destroy(fontDescriptorSet);
		graphicsSystem->destroy(fontTexture);
		graphicsSystem->destroy(pipeline);

		if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		{
			auto& io = ImGui::GetIO();
			io.BackendRendererName = nullptr;
			io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;
		}

		#ifdef _WIN32
		auto mainViewport = ImGui::GetMainViewport();
		::SetWindowLongPtrW((HWND)mainViewport->PlatformHandleRaw, GWLP_WNDPROC, (LONG_PTR)prevWndProc);
		prevWndProc = nullptr;
		#endif

		auto& io = ImGui::GetIO();
		io.BackendPlatformName = nullptr;
		io.BackendFlags &= ~(ImGuiBackendFlags_HasMouseCursors | 
			ImGuiBackendFlags_HasSetMousePos | ImGuiBackendFlags_HasGamepad);

		ECSM_UNSUBSCRIBE_FROM_EVENT("Input", ImGuiRenderSystem::input);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Render", ImGuiRenderSystem::render);
		ECSM_UNSUBSCRIBE_FROM_EVENT("UiRender", ImGuiRenderSystem::uiRender);
	}
}

//**********************************************************************************************************************
static ImGuiKey keyToImGuiKey(int keycode, int scancode)
{
	IM_UNUSED(scancode);
	switch (keycode)
	{
	case GLFW_KEY_TAB: return ImGuiKey_Tab;
	case GLFW_KEY_LEFT: return ImGuiKey_LeftArrow;
	case GLFW_KEY_RIGHT: return ImGuiKey_RightArrow;
	case GLFW_KEY_UP: return ImGuiKey_UpArrow;
	case GLFW_KEY_DOWN: return ImGuiKey_DownArrow;
	case GLFW_KEY_PAGE_UP: return ImGuiKey_PageUp;
	case GLFW_KEY_PAGE_DOWN: return ImGuiKey_PageDown;
	case GLFW_KEY_HOME: return ImGuiKey_Home;
	case GLFW_KEY_END: return ImGuiKey_End;
	case GLFW_KEY_INSERT: return ImGuiKey_Insert;
	case GLFW_KEY_DELETE: return ImGuiKey_Delete;
	case GLFW_KEY_BACKSPACE: return ImGuiKey_Backspace;
	case GLFW_KEY_SPACE: return ImGuiKey_Space;
	case GLFW_KEY_ENTER: return ImGuiKey_Enter;
	case GLFW_KEY_ESCAPE: return ImGuiKey_Escape;
	case GLFW_KEY_APOSTROPHE: return ImGuiKey_Apostrophe;
	case GLFW_KEY_COMMA: return ImGuiKey_Comma;
	case GLFW_KEY_MINUS: return ImGuiKey_Minus;
	case GLFW_KEY_PERIOD: return ImGuiKey_Period;
	case GLFW_KEY_SLASH: return ImGuiKey_Slash;
	case GLFW_KEY_SEMICOLON: return ImGuiKey_Semicolon;
	case GLFW_KEY_EQUAL: return ImGuiKey_Equal;
	case GLFW_KEY_LEFT_BRACKET: return ImGuiKey_LeftBracket;
	case GLFW_KEY_BACKSLASH: return ImGuiKey_Backslash;
	case GLFW_KEY_RIGHT_BRACKET: return ImGuiKey_RightBracket;
	case GLFW_KEY_GRAVE_ACCENT: return ImGuiKey_GraveAccent;
	case GLFW_KEY_CAPS_LOCK: return ImGuiKey_CapsLock;
	case GLFW_KEY_SCROLL_LOCK: return ImGuiKey_ScrollLock;
	case GLFW_KEY_NUM_LOCK: return ImGuiKey_NumLock;
	case GLFW_KEY_PRINT_SCREEN: return ImGuiKey_PrintScreen;
	case GLFW_KEY_PAUSE: return ImGuiKey_Pause;
	case GLFW_KEY_KP_0: return ImGuiKey_Keypad0;
	case GLFW_KEY_KP_1: return ImGuiKey_Keypad1;
	case GLFW_KEY_KP_2: return ImGuiKey_Keypad2;
	case GLFW_KEY_KP_3: return ImGuiKey_Keypad3;
	case GLFW_KEY_KP_4: return ImGuiKey_Keypad4;
	case GLFW_KEY_KP_5: return ImGuiKey_Keypad5;
	case GLFW_KEY_KP_6: return ImGuiKey_Keypad6;
	case GLFW_KEY_KP_7: return ImGuiKey_Keypad7;
	case GLFW_KEY_KP_8: return ImGuiKey_Keypad8;
	case GLFW_KEY_KP_9: return ImGuiKey_Keypad9;
	case GLFW_KEY_KP_DECIMAL: return ImGuiKey_KeypadDecimal;
	case GLFW_KEY_KP_DIVIDE: return ImGuiKey_KeypadDivide;
	case GLFW_KEY_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
	case GLFW_KEY_KP_SUBTRACT: return ImGuiKey_KeypadSubtract;
	case GLFW_KEY_KP_ADD: return ImGuiKey_KeypadAdd;
	case GLFW_KEY_KP_ENTER: return ImGuiKey_KeypadEnter;
	case GLFW_KEY_KP_EQUAL: return ImGuiKey_KeypadEqual;
	case GLFW_KEY_LEFT_SHIFT: return ImGuiKey_LeftShift;
	case GLFW_KEY_LEFT_CONTROL: return ImGuiKey_LeftCtrl;
	case GLFW_KEY_LEFT_ALT: return ImGuiKey_LeftAlt;
	case GLFW_KEY_LEFT_SUPER: return ImGuiKey_LeftSuper;
	case GLFW_KEY_RIGHT_SHIFT: return ImGuiKey_RightShift;
	case GLFW_KEY_RIGHT_CONTROL: return ImGuiKey_RightCtrl;
	case GLFW_KEY_RIGHT_ALT: return ImGuiKey_RightAlt;
	case GLFW_KEY_RIGHT_SUPER: return ImGuiKey_RightSuper;
	case GLFW_KEY_MENU: return ImGuiKey_Menu;
	case GLFW_KEY_0: return ImGuiKey_0;
	case GLFW_KEY_1: return ImGuiKey_1;
	case GLFW_KEY_2: return ImGuiKey_2;
	case GLFW_KEY_3: return ImGuiKey_3;
	case GLFW_KEY_4: return ImGuiKey_4;
	case GLFW_KEY_5: return ImGuiKey_5;
	case GLFW_KEY_6: return ImGuiKey_6;
	case GLFW_KEY_7: return ImGuiKey_7;
	case GLFW_KEY_8: return ImGuiKey_8;
	case GLFW_KEY_9: return ImGuiKey_9;
	case GLFW_KEY_A: return ImGuiKey_A;
	case GLFW_KEY_B: return ImGuiKey_B;
	case GLFW_KEY_C: return ImGuiKey_C;
	case GLFW_KEY_D: return ImGuiKey_D;
	case GLFW_KEY_E: return ImGuiKey_E;
	case GLFW_KEY_F: return ImGuiKey_F;
	case GLFW_KEY_G: return ImGuiKey_G;
	case GLFW_KEY_H: return ImGuiKey_H;
	case GLFW_KEY_I: return ImGuiKey_I;
	case GLFW_KEY_J: return ImGuiKey_J;
	case GLFW_KEY_K: return ImGuiKey_K;
	case GLFW_KEY_L: return ImGuiKey_L;
	case GLFW_KEY_M: return ImGuiKey_M;
	case GLFW_KEY_N: return ImGuiKey_N;
	case GLFW_KEY_O: return ImGuiKey_O;
	case GLFW_KEY_P: return ImGuiKey_P;
	case GLFW_KEY_Q: return ImGuiKey_Q;
	case GLFW_KEY_R: return ImGuiKey_R;
	case GLFW_KEY_S: return ImGuiKey_S;
	case GLFW_KEY_T: return ImGuiKey_T;
	case GLFW_KEY_U: return ImGuiKey_U;
	case GLFW_KEY_V: return ImGuiKey_V;
	case GLFW_KEY_W: return ImGuiKey_W;
	case GLFW_KEY_X: return ImGuiKey_X;
	case GLFW_KEY_Y: return ImGuiKey_Y;
	case GLFW_KEY_Z: return ImGuiKey_Z;
	case GLFW_KEY_F1: return ImGuiKey_F1;
	case GLFW_KEY_F2: return ImGuiKey_F2;
	case GLFW_KEY_F3: return ImGuiKey_F3;
	case GLFW_KEY_F4: return ImGuiKey_F4;
	case GLFW_KEY_F5: return ImGuiKey_F5;
	case GLFW_KEY_F6: return ImGuiKey_F6;
	case GLFW_KEY_F7: return ImGuiKey_F7;
	case GLFW_KEY_F8: return ImGuiKey_F8;
	case GLFW_KEY_F9: return ImGuiKey_F9;
	case GLFW_KEY_F10: return ImGuiKey_F10;
	case GLFW_KEY_F11: return ImGuiKey_F11;
	case GLFW_KEY_F12: return ImGuiKey_F12;
	case GLFW_KEY_F13: return ImGuiKey_F13;
	case GLFW_KEY_F14: return ImGuiKey_F14;
	case GLFW_KEY_F15: return ImGuiKey_F15;
	case GLFW_KEY_F16: return ImGuiKey_F16;
	case GLFW_KEY_F17: return ImGuiKey_F17;
	case GLFW_KEY_F18: return ImGuiKey_F18;
	case GLFW_KEY_F19: return ImGuiKey_F19;
	case GLFW_KEY_F20: return ImGuiKey_F20;
	case GLFW_KEY_F21: return ImGuiKey_F21;
	case GLFW_KEY_F22: return ImGuiKey_F22;
	case GLFW_KEY_F23: return ImGuiKey_F23;
	case GLFW_KEY_F24: return ImGuiKey_F24;
	default: return ImGuiKey_None;
	}
}

//**********************************************************************************************************************
static void updateImGuiKeyModifiers(InputSystem* inputSystem, ImGuiIO& io)
{
	io.AddKeyEvent(ImGuiMod_Ctrl, inputSystem->getKeyboardState(KeyboardButton::LeftControl) ||
		inputSystem->getKeyboardState(KeyboardButton::RightControl));
	io.AddKeyEvent(ImGuiMod_Shift, inputSystem->getKeyboardState(KeyboardButton::LeftShift) ||
		inputSystem->getKeyboardState(KeyboardButton::RightShift));
	io.AddKeyEvent(ImGuiMod_Alt, inputSystem->getKeyboardState(KeyboardButton::LeftAlt) ||
		inputSystem->getKeyboardState(KeyboardButton::RightAlt));
	io.AddKeyEvent(ImGuiMod_Super, inputSystem->getKeyboardState(KeyboardButton::LeftSuper) ||
		inputSystem->getKeyboardState(KeyboardButton::RightSuper));
}

void ImGuiRenderSystem::input()
{
	auto& io = ImGui::GetIO();
	auto inputSystem = InputSystem::Instance::get();
	auto cursorPos = inputSystem->getCursorPosition();
	io.AddMousePosEvent(cursorPos.x, cursorPos.y);
	lastValidMousePos = cursorPos;

	if (inputSystem->isCursorEntered())
	{
		io.AddMousePosEvent(lastValidMousePos.x, lastValidMousePos.y);
	}
	else if (inputSystem->isCursorLeaved())
	{
		lastValidMousePos = float2(io.MousePos.x, io.MousePos.y);
		io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
	}

	if (inputSystem->isWindowFocused())
		io.AddFocusEvent(true);
	else if (inputSystem->isWindowUnfocused())
		io.AddFocusEvent(false);

	// TODO: Check if we need ImGui_ImplGlfw_MonitorCallback() after docking branch merge.

	auto mouseScroll = inputSystem->getMouseScroll();
	io.AddMouseWheelEvent(mouseScroll.x, mouseScroll.y);

	for (uint8 i = 0; i < keyboardButtonCount; i++)
	{
		auto button = allKeyboardButtons[i];

		int action = -1;
		if (inputSystem->isKeyboardPressed(button))
			action = GLFW_PRESS;
		else if (inputSystem->isKeyboardReleased(button))
			action = GLFW_RELEASE;

		if (action != -1)
		{
			updateImGuiKeyModifiers(inputSystem, io);
			auto scancode = glfwGetKeyScancode((int)button);
			auto imgui_key = keyToImGuiKey((int)button, scancode);
			io.AddKeyEvent(imgui_key, (action == GLFW_PRESS));
			io.SetKeyEventNativeData(imgui_key, (int)button, scancode); // To support legacy indexing (<1.87 user code)
		}
	}
	for (int i = 0; i < (int)MouseButton::Last + 1; i++)
	{
		int action = -1;
		if (inputSystem->isMousePressed((MouseButton)i))
			action = GLFW_PRESS;
		else if (inputSystem->isMouseReleased((MouseButton)i))
			action = GLFW_RELEASE;

		if (action != -1)
		{
			updateImGuiKeyModifiers(inputSystem, io);
			if (i >= 0 && i < ImGuiMouseButton_COUNT)
				io.AddMouseButtonEvent(i, action == GLFW_PRESS);
		}
	}

	const auto& keyboardChars = inputSystem->getKeyboardChars32();
	for (auto keyboardChar : keyboardChars)
		io.AddInputCharacter(keyboardChar);
}

//**********************************************************************************************************************
void ImGuiRenderSystem::update()
{
	SET_CPU_ZONE_SCOPED("ImGui Update");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!isEnabled || !graphicsSystem->canRender())
		return;

	auto& io = ImGui::GetIO();
	auto inputSystem = InputSystem::Instance::get();
	auto windowSize = inputSystem->getWindowSize();
	auto pixelRatio = (float2)graphicsSystem->getFramebufferSize() / windowSize;
	io.DisplaySize = ImVec2(windowSize.x, windowSize.y);
	io.DisplayFramebufferScale = ImVec2(pixelRatio.x, pixelRatio.y);
	io.DeltaTime = inputSystem->getDeltaTime();

	if (!(io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) && inputSystem->getCursorMode() != CursorMode::Locked)
	{
		auto imguiCursor = ImGui::GetMouseCursor();
		if (imguiCursor == ImGuiMouseCursor_None || io.MouseDrawCursor)
		{
			inputSystem->setCursorMode(CursorMode::Hidden);
		}
		else
		{
			CursorType cursorType;
			switch (imguiCursor)
			{
				case ImGuiMouseCursor_Arrow:
					cursorType = CursorType::Arrow; break;
				case ImGuiMouseCursor_TextInput:
					cursorType = CursorType::Ibeam; break;
				case ImGuiMouseCursor_ResizeAll:
					cursorType = CursorType::ResizeAll; break;
				case ImGuiMouseCursor_ResizeNS:
					cursorType = CursorType::ResizeNS; break;
				case ImGuiMouseCursor_ResizeEW:
					cursorType = CursorType::ResizeEW; break;
				case ImGuiMouseCursor_ResizeNESW:
					cursorType = CursorType::ResizeNESW; break;
				case ImGuiMouseCursor_ResizeNWSE:
					cursorType = CursorType::ResizeNWSE; break;
				case ImGuiMouseCursor_Hand:
					cursorType = CursorType::PointingHand; break;
				case ImGuiMouseCursor_NotAllowed:
					cursorType = CursorType::NotAllowed; break;
				default:
					cursorType = CursorType::Default; break;
			}

			inputSystem->setCursorType(cursorType);
			inputSystem->setCursorMode(CursorMode::Normal);
		}
	}

	// TODO: implement these if required for something:
	//
	// ImGui_ImplGlfw_UpdateMouseData();
	// ImGui_ImplGlfw_UpdateGamepads();

	ImGui::NewFrame();

	if (isEnabled)
	{
		if (!pipeline)
			pipeline = createPipeline();
		if (!linearSampler)
			linearSampler = createLinearSampler();
		if (!nearestSampler)
			nearestSampler = createNearestSampler();
	}
}

void ImGuiRenderSystem::render()
{
	if (!GraphicsSystem::Instance::get()->canRender())
		ImGui::EndFrame();
}

//**********************************************************************************************************************
void ImGuiRenderSystem::uiRender()
{
	SET_CPU_ZONE_SCOPED("ImGui UI Render");

	ImGui::Render();

	if (!isEnabled)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(pipeline);
	auto fontTextureView = graphicsSystem->get(fontTexture);
	if (!pipelineView->isReady() || !fontTextureView->isReady())
		return;

	if (!fontDescriptorSet)
	{
		auto uniforms = getUniforms(fontTextureView->getDefaultView());
		auto samplers = getSamplers(linearSampler);
		fontDescriptorSet = graphicsSystem->createDescriptorSet(
			pipeline, std::move(uniforms), std::move(samplers));
		SET_RESOURCE_DEBUG_NAME(fontDescriptorSet, "descriptorSet.imgui.font");
	}

	auto drawData = ImGui::GetDrawData();
	auto& cmdLists = drawData->CmdLists;
	auto defaultFontTexture = fontTextureView->getDefaultView();
	ID<Buffer> vertexBuffer = {}, indexBuffer = {};

	if (drawData->TotalVtxCount > 0 && drawData->TotalIdxCount > 0)
	{
		auto vertexSize = drawData->TotalVtxCount * sizeof(ImDrawVert);
		auto indexSize = drawData->TotalIdxCount * sizeof(ImDrawIdx);

		if (vertexBuffers.size() == 0 || graphicsSystem->get(vertexBuffers[0])->getBinarySize() < vertexSize)
		{
			graphicsSystem->destroy(vertexBuffers);
			createBuffers(vertexBuffers, vertexSize, Buffer::Usage::Vertex);
		}
		if (indexBuffers.size() == 0 || graphicsSystem->get(indexBuffers[0])->getBinarySize() < indexSize)
		{
			graphicsSystem->destroy(indexBuffers);
			createBuffers(indexBuffers, indexSize, Buffer::Usage::Index);
		}

		auto inFlightIndex = graphicsSystem->getInFlightIndex();
		vertexBuffer = vertexBuffers[inFlightIndex];
		indexBuffer = indexBuffers[inFlightIndex];
		auto vertexBufferView = graphicsSystem->get(vertexBuffer);
		auto indexBufferView = graphicsSystem->get(indexBuffer);
		auto vtxDst = (ImDrawVert*)vertexBufferView->getMap();
		auto idxDst = (ImDrawIdx*)indexBufferView->getMap();

		for (int n = 0; n < cmdLists.Size; n++)
		{
			const auto drawList = cmdLists[n];
			memcpy(vtxDst, drawList->VtxBuffer.Data, drawList->VtxBuffer.Size * sizeof(ImDrawVert));
			memcpy(idxDst, drawList->IdxBuffer.Data, drawList->IdxBuffer.Size * sizeof(ImDrawIdx));
			vtxDst += drawList->VtxBuffer.Size;
			idxDst += drawList->IdxBuffer.Size;
		}
		
		vertexBufferView->flush(vertexSize);
		indexBufferView->flush(indexSize);
	}

	const auto indexType = sizeof(ImDrawIdx) == 2 ? IndexType::Uint16 : IndexType::Uint32;
	auto framebufferView = graphicsSystem->get(pipelineView->getFramebuffer());
	auto framebufferSize = framebufferView->getSize();

	PushConstants pc;
	pc.scale = float2(2.0f / drawData->DisplaySize.x, 2.0f / drawData->DisplaySize.y);
	pc.translate = float2(-1.0f - drawData->DisplayPos.x * pc.scale.x,
		-1.0f - drawData->DisplayPos.y * pc.scale.y);

	SET_GPU_DEBUG_LABEL("ImGui", Color::transparent);
	pipelineView->bind();
	pipelineView->setViewport();
	pipelineView->pushConstants(&pc);

	auto clipOff = drawData->DisplayPos;
	auto clipScale = drawData->FramebufferScale;
	int globalVtxOffset = 0, globalIdxOffset = 0;

	// TODO: use async commands recording.
	for (int i = 0; i < cmdLists.Size; i++)
	{
		auto cmdList = cmdLists[i];
		auto& cmdBuffer = cmdList->CmdBuffer;

		for (int j = 0; j < cmdBuffer.Size; j++)
		{
			auto& cmd = cmdBuffer[j];
			ImVec2 clipMin((cmd.ClipRect.x - clipOff.x) * clipScale.x, (cmd.ClipRect.y - clipOff.y) * clipScale.y);
			ImVec2 clipMax((cmd.ClipRect.z - clipOff.x) * clipScale.x, (cmd.ClipRect.w - clipOff.y) * clipScale.y);

			if (clipMin.x < 0.0f) clipMin.x = 0.0f;
			if (clipMin.y < 0.0f) clipMin.y = 0.0f;
			if (clipMax.x > framebufferSize.x) clipMax.x = (float)framebufferSize.x;
			if (clipMax.y > framebufferSize.y) clipMax.y = (float)framebufferSize.y;
			if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y)
				continue;

			pipelineView->setScissor(int4(clipMin.x, clipMin.y, clipMax.x - clipMin.x, clipMax.y - clipMin.y));

			ID<DescriptorSet> descriptorSet;
			if (cmd.TextureId == *defaultFontTexture)
			{
				descriptorSet = fontDescriptorSet;
			}
			else
			{
				ID<ImageView> texture;
				*texture = cmd.TextureId;

				auto uniforms = getUniforms(texture);
				auto samplers = getSamplers(nearestSampler);
				auto tmpDescriptorSet = graphicsSystem->createDescriptorSet(
					pipeline, std::move(uniforms), std::move(samplers));
				SET_RESOURCE_DEBUG_NAME(tmpDescriptorSet, "descriptorSet.imgui.imageView.tmp");
				graphicsSystem->destroy(tmpDescriptorSet); // TODO: use here Vulkan extensions which allows to bind resources directly without DS.
				descriptorSet = tmpDescriptorSet;
			}
			pipelineView->bindDescriptorSet(descriptorSet);
			
			pipelineView->drawIndexed(vertexBuffer, indexBuffer, indexType, cmd.ElemCount, 1, 
				cmd.IdxOffset + globalIdxOffset, cmd.VtxOffset + globalVtxOffset);
		}
		globalIdxOffset += cmdList->IdxBuffer.Size;
		globalVtxOffset += cmdList->VtxBuffer.Size;
	}
}

ID<GraphicsPipeline> ImGuiRenderSystem::getPipeline()
{
	if (!pipeline)
		pipeline = createPipeline();
	return pipeline;
}