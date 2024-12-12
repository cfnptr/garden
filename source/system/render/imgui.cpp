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

#include "garden/system/render/imgui.hpp"
#include "garden/system/resource.hpp"
#include "garden/graphics/api.hpp"
#include "garden/graphics/glfw.hpp" // Do not move it.
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

static ID<GraphicsPipeline> createPipeline()
{
	return ResourceSystem::Instance::get()->loadGraphicsPipeline(
		"imgui", GraphicsSystem::Instance::get()->getSwapchainFramebuffer());
}
static map<string, DescriptorSet::Uniform> getUniforms(ID<ImageView> texture)
{
	map<string, DescriptorSet::Uniform> uniforms = { { "tex", DescriptorSet::Uniform(texture) } };
	return uniforms;
}
static void createBuffers(vector<ID<Buffer>>& buffers, uint64 bufferSize, Buffer::Bind bind)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto swapchainSize = graphicsSystem->getSwapchainSize();
	buffers.resize(swapchainSize);

	for (uint32 i = 0; i < swapchainSize; i++)
	{
		auto buffer = graphicsSystem->createBuffer(bind, Buffer::Access::SequentialWrite,
			bufferSize, Buffer::Usage::Auto, Buffer::Strategy::Size);
		SET_RESOURCE_DEBUG_NAME(buffer, "buffer.imgui." + string(toString(bind)) + to_string(i));
		buffers[i] = buffer;
	}
}

//**********************************************************************************************************************
ImGuiRenderSystem::ImGuiRenderSystem(bool setSingleton, 
	const fs::path& fontPath) : Singleton(setSingleton), fontPath(fontPath)
{
	ECSM_SUBSCRIBE_TO_EVENT("PreInit", ImGuiRenderSystem::preInit);
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
		ECSM_UNSUBSCRIBE_FROM_EVENT("PostDeinit", ImGuiRenderSystem::postDeinit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Update", ImGuiRenderSystem::update);
	}
	unsetSingleton();
}

#ifdef _WIN32
static WNDPROC prevWndProc = nullptr;

// GLFW doesn't allow to distinguish Mouse vs TouchScreen vs Pen.
// Add support for Win32 (based on imgui_impl_win32), because we rely on _TouchScreen info to trickle inputs differently.
static ImGuiMouseSource GetMouseSourceFromMessageExtraInfo()
{
	LPARAM extra_info = ::GetMessageExtraInfo();
	if ((extra_info & 0xFFFFFF80) == 0xFF515700)
		return ImGuiMouseSource_Pen;
	if ((extra_info & 0xFFFFFF80) == 0xFF515780)
		return ImGuiMouseSource_TouchScreen;
	return ImGuiMouseSource_Mouse;
}
static LRESULT CALLBACK ImGui_ImplGlfw_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_MOUSEMOVE: case WM_NCMOUSEMOVE:
	case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_LBUTTONUP:
	case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: case WM_RBUTTONUP:
	case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: case WM_MBUTTONUP:
	case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: case WM_XBUTTONUP:
		ImGui::GetIO().AddMouseSourceEvent(GetMouseSourceFromMessageExtraInfo());
		break;
	}
	return ::CallWindowProcW(prevWndProc, hWnd, msg, wParam, lParam);
}
#endif

//**********************************************************************************************************************
void ImGuiRenderSystem::preInit()
{
	ECSM_SUBSCRIBE_TO_EVENT("Input", ImGuiRenderSystem::input);
	ECSM_SUBSCRIBE_TO_EVENT("Present", ImGuiRenderSystem::present);
	ECSM_SUBSCRIBE_TO_EVENT("SwapchainRecreate", ImGuiRenderSystem::swapchainRecreate);

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
	mainViewport->PlatformHandleRaw = (void*)glfwGetCocoaWindow(bd->Window);
	#else
    IM_UNUSED(mainViewport);
	#endif

    // Windows: register a WndProc hook so we can intercept some messages.
	#ifdef _WIN32
    prevWndProc = (WNDPROC)::GetWindowLongPtrW((HWND)mainViewport->PlatformHandleRaw, GWLP_WNDPROC);
    GARDEN_ASSERT(prevWndProc != nullptr);
    ::SetWindowLongPtrW((HWND)mainViewport->PlatformHandleRaw, GWLP_WNDPROC, (LONG_PTR)ImGui_ImplGlfw_WndProc);
	#endif

	auto contentScale = inputSystem->getContentScale();
	auto fontSize = 14.0f * std::max(contentScale.x, contentScale.y);

	#if GARDEN_DEBUG
	auto fontString = (GARDEN_RESOURCES_PATH / fontPath).generic_string();
	auto fontResult = io.Fonts->AddFontFromFileTTF(fontString.c_str(), fontSize);
	GARDEN_ASSERT(fontResult);
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
	fontTexture = graphicsSystem->createImage(Image::Format::UnormR8G8B8A8, Image::Bind::Sampled | 
		Image::Bind::TransferDst, { { pixels } }, uint2(width, height), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(fontTexture, "image.imgui.fontTexture");

	auto fontTextureView = graphicsSystem->get(fontTexture);
	io.Fonts->SetTexID((ImTextureID)*fontTextureView->getDefaultView());

	if (isEnabled)
	{
		if (!pipeline)
			pipeline = createPipeline();
	}
}
void ImGuiRenderSystem::postDeinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		for (auto buffer : indexBuffers)
			graphicsSystem->destroy(buffer);
		for (auto buffer : vertexBuffers)
			graphicsSystem->destroy(buffer);
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
		ECSM_UNSUBSCRIBE_FROM_EVENT("Present", ImGuiRenderSystem::present);
		ECSM_UNSUBSCRIBE_FROM_EVENT("SwapchainRecreate", ImGuiRenderSystem::swapchainRecreate);
	}
}

//**********************************************************************************************************************
extern ImGuiKey ImGui_ImplGlfw_KeyToImGuiKey(int keycode, int scancode);

static void updateImGuiKeyModifiers(InputSystem* inutSystem, ImGuiIO& io)
{
	io.AddKeyEvent(ImGuiMod_Ctrl, inutSystem->getKeyboardState(KeyboardButton::LeftControl) ||
		inutSystem->getKeyboardState(KeyboardButton::RightControl));
	io.AddKeyEvent(ImGuiMod_Shift, inutSystem->getKeyboardState(KeyboardButton::LeftShift) ||
		inutSystem->getKeyboardState(KeyboardButton::RightShift));
	io.AddKeyEvent(ImGuiMod_Alt, inutSystem->getKeyboardState(KeyboardButton::LeftAlt) ||
		inutSystem->getKeyboardState(KeyboardButton::RightAlt));
	io.AddKeyEvent(ImGuiMod_Super, inutSystem->getKeyboardState(KeyboardButton::LeftSuper) ||
		inutSystem->getKeyboardState(KeyboardButton::RightSuper));
}

void ImGuiRenderSystem::input()
{
	auto& io = ImGui::GetIO();
	auto inputSystem = InputSystem::Instance::get();
	auto window = (GLFWwindow*)GraphicsAPI::get()->window;
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
			auto imgui_key = ImGui_ImplGlfw_KeyToImGuiKey((int)button, scancode);
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
	for (psize i = 0; i < keyboardChars.size(); i++)
		io.AddInputCharacter(keyboardChars[i]);
}

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

	ImGui::NewFrame();
	startedFrame = true;


	// TODO: implement these if required for something:
	//
	// ImGui_ImplGlfw_UpdateMouseData();
	// ImGui_ImplGlfw_UpdateGamepads();
}

//**********************************************************************************************************************
void ImGuiRenderSystem::present()
{
	SET_CPU_ZONE_SCOPED("ImGui Present");

	if (startedFrame)
	{
		ImGui::Render();
		startedFrame = false;
	}

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!isEnabled || !graphicsSystem->canRender())
		return;

	if (!pipeline)
		pipeline = createPipeline();
	
	auto pipelineView = graphicsSystem->get(pipeline);
	auto fontTextureView = graphicsSystem->get(fontTexture);
	if (!pipelineView->isReady() || !fontTextureView->isReady())
		return;

	if (!fontDescriptorSet)
	{
		auto uniforms = getUniforms(fontTextureView->getDefaultView());
		fontDescriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(fontDescriptorSet, "descriptorSet.imgui.font");
	}

	auto drawData = ImGui::GetDrawData();
	auto& cmdLists = drawData->CmdLists;
	auto defaultFontTexture = fontTextureView->getDefaultView();
	ID<Buffer> vertexBuffer = {}, indexBuffer = {};

	if (drawData->TotalVtxCount > 0)
	{
		auto vertexSize = drawData->TotalVtxCount * sizeof(ImDrawVert);
		auto indexSize = drawData->TotalIdxCount * sizeof(ImDrawIdx);

		if (vertexBuffers.size() == 0 || graphicsSystem->get(vertexBuffers[0])->getBinarySize() < vertexSize)
		{
			for (auto buffer : vertexBuffers)
				graphicsSystem->destroy(buffer);
			createBuffers(vertexBuffers, vertexSize, Buffer::Bind::Vertex);
		}
		if (indexBuffers.size() == 0 || graphicsSystem->get(indexBuffers[0])->getBinarySize() < indexSize)
		{
			for (auto buffer : indexBuffers)
				graphicsSystem->destroy(buffer);
			createBuffers(indexBuffers, indexSize, Buffer::Bind::Index);
		}

		auto swapchainIndex = graphicsSystem->getSwapchainIndex();
		vertexBuffer = vertexBuffers[swapchainIndex];
		indexBuffer = indexBuffers[swapchainIndex];
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

	const auto indexType = sizeof(ImDrawIdx) == 2 ?
		GraphicsPipeline::Index::Uint16 : GraphicsPipeline::Index::Uint32;
	auto framebufferView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());
	auto framebufferSize = framebufferView->getSize();

	graphicsSystem->startRecording(CommandBufferType::Frame);
	auto pushConstants = pipelineView->getPushConstants<PushConstants>();
	pushConstants->scale = float2(2.0f / drawData->DisplaySize.x, 2.0f / drawData->DisplaySize.y);
	pushConstants->translate = float2(-1.0f - drawData->DisplayPos.x * pushConstants->scale.x,
		-1.0f - drawData->DisplayPos.y * pushConstants->scale.y);

	BEGIN_GPU_DEBUG_LABEL("ImGui", Color::transparent);
	framebufferView->beginRenderPass(float4(0.0f));
	pipelineView->bind();
	pipelineView->setViewport();
	pipelineView->pushConstants();

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
				auto tmpDescriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
				SET_RESOURCE_DEBUG_NAME(tmpDescriptorSet, "descriptorSet.imgui.imageView");
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

	framebufferView->endRenderPass();
	END_GPU_DEBUG_LABEL();
	graphicsSystem->stopRecording();
}

//**********************************************************************************************************************
void ImGuiRenderSystem::swapchainRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	const auto& swapchainChanges = graphicsSystem->getSwapchainChanges();

	if (swapchainChanges.bufferCount)
	{
		auto vertexSize = vertexBuffers.size() > 0 ? graphicsSystem->get(vertexBuffers[0])->getBinarySize() : 0;
		auto indexSize = indexBuffers.size() > 0 ? graphicsSystem->get(indexBuffers[0])->getBinarySize() : 0;

		for (auto buffer : indexBuffers)
			graphicsSystem->destroy(buffer);
		for (auto buffer : vertexBuffers)
			graphicsSystem->destroy(buffer);

		if (vertexSize > 0)
			createBuffers(vertexBuffers, vertexSize, Buffer::Bind::Vertex);
		if (indexSize > 0)
			createBuffers(indexBuffers, indexSize, Buffer::Bind::Vertex);
	}
}

ID<GraphicsPipeline> ImGuiRenderSystem::getPipeline()
{
	if (!pipeline)
		pipeline = createPipeline();
	return pipeline;
}