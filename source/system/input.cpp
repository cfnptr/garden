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

#include "garden/system/input.hpp"
#include "garden/system/log.hpp"
#include "garden/system/graphics.hpp"
#include "garden/system/resource.hpp"
#include "garden/graphics/vulkan/api.hpp"
#include "garden/graphics/glfw.hpp" // Note: Do not move it.
#include "garden/profiler.hpp"
#include "mpmt/thread.hpp"

using namespace garden;
using namespace garden::graphics;

static void updateWindowMode()
{
	auto primaryMonitor = glfwGetPrimaryMonitor();
	if (!primaryMonitor)
		return;

	auto videoMode = glfwGetVideoMode(primaryMonitor);
	if (!videoMode)
		return;

	auto window = (GLFWwindow*)GraphicsAPI::get()->window;
	if (glfwGetWindowAttrib(window, GLFW_DECORATED) == GLFW_FALSE)
	{
		int width = 0, height = 0; glfwGetWindowSize(window, &width, &height);
		glfwSetWindowMonitor(window, nullptr, videoMode->width / 2 - width / 2, videoMode->height / 2 - height / 2,
			InputSystem::defaultWindowWidth, InputSystem::defaultWindowHeight, videoMode->refreshRate);
		glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_TRUE);
	}
	else
	{
		glfwSetWindowMonitor(window, primaryMonitor, 0, 0,
			videoMode->width, videoMode->height, videoMode->refreshRate);
		glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
	}
}

//**********************************************************************************************************************
void InputSystem::onKeyboardButton(void* window, int key, int scancode, int action, int mods)
{
	if (key == GLFW_KEY_F11 && action == GLFW_PRESS)
		updateWindowMode();
}
void InputSystem::onMouseScroll(void* window, double offsetX, double offsetY)
{
	InputSystem::Instance::get()->accumMouseScroll += float2((float)offsetX, (float)offsetY);
}
void InputSystem::onFileDrop(void* window, int count, const char** paths)
{
	GARDEN_LOG_INFO("Dropped " + to_string(count) + " items on a window.");

	auto inputSystem = InputSystem::Instance::get();
	for (int i = 0; i < count; i++)
		inputSystem->accumFileDrops.push_back(paths[i]);
}
void InputSystem::onKeyboardChar(void* window, unsigned int codepoint)
{
	InputSystem::Instance::get()->accumKeyboardChars.push_back(codepoint);
}

void InputSystem::renderThread()
{
	mpmt::Thread::setName("RENDER");
	mpmt::Thread::setForegroundPriority();
	Manager::Instance::get()->start();
}

//**********************************************************************************************************************
InputSystem::InputSystem(bool setSingleton) : Singleton(setSingleton),
	newKeyboardStates((psize)KeyboardButton::Last + 1, false),
	lastKeyboardStates((psize)KeyboardButton::Last + 1, false),
	currKeyboardStates((psize)KeyboardButton::Last + 1, false),
	newMouseStates((psize)MouseButton::Last + 1, false),
	lastMouseStates((psize)MouseButton::Last + 1, false),
	currMouseStates((psize)MouseButton::Last + 1, false)
{
	auto manager = Manager::Instance::get();
	manager->registerEventBefore("Input", "Update");
	manager->registerEventAfter("Output", "Update");
	manager->registerEvent("FileDrop");

	ECSM_SUBSCRIBE_TO_EVENT("PreInit", InputSystem::preInit);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", InputSystem::deinit);
}
InputSystem::~InputSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreInit", InputSystem::preInit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", InputSystem::deinit);

		auto manager = Manager::Instance::get();
		manager->unregisterEvent("Input");
		manager->unregisterEvent("FileDrop");
	}

	unsetSingleton();
}

//**********************************************************************************************************************
void InputSystem::preInit()
{
	ECSM_SUBSCRIBE_TO_EVENT("Input", InputSystem::input);
	ECSM_SUBSCRIBE_TO_EVENT("Output", InputSystem::output);

	auto window = (GLFWwindow*)GraphicsAPI::get()->window;
	glfwSetKeyCallback(window, (GLFWkeyfun)InputSystem::onKeyboardButton);
	glfwSetScrollCallback(window, (GLFWscrollfun)InputSystem::onMouseScroll);
	glfwSetDropCallback(window, (GLFWdropfun)InputSystem::onFileDrop);
	glfwSetCharCallback(window, (GLFWcharfun)InputSystem::onKeyboardChar);

	int width = 0, height = 0;
	glfwGetFramebufferSize(window, &width, &height);
	newFramebufferSize = currFramebufferSize = uint2((uint32)width, (uint32)height);

	glfwGetWindowSize(window, &width, &height);
	newWindowSize = currWindowSize = uint2((uint32)width, (uint32)height);

	glfwGetWindowContentScale(window, &currContentScale.x, &currContentScale.y);
	newContentScale = currContentScale;

	double x = 0.0, y = 0.0;
	glfwGetCursorPos(window, &x, &y);
	newCursorPos = currCursorPos = float2((float)x, (float)y);

	newCursorMode = currCursorMode = (CursorMode)(glfwGetInputMode(window, GLFW_CURSOR) - 0x00034001);
	newCursorInWindow = lastCursorInWindow = currCursorInWindow = glfwGetWindowAttrib(window, GLFW_HOVERED);
	newWindowInFocus = lastWindowInFocus = currWindowInFocus = glfwGetWindowAttrib(window, GLFW_FOCUSED);

	for (uint8 i = 0; i < keyboardButtonCount; i++)
	{
		auto button = (int)allKeyboardButtons[i];
		auto state = glfwGetKey(window, button);
		newKeyboardStates[button] = lastKeyboardStates[button] = currKeyboardStates[button] = state;
	}

	for (int i = 0; i < (int)MouseButton::Last + 1; i++)
	{
		auto state = glfwGetMouseButton(window, i);
		newMouseStates[i] = lastMouseStates[i] = currMouseStates[i] = state;
	}

	auto currentCallback = glfwSetErrorCallback(nullptr);
	auto clipboard = glfwGetClipboardString(nullptr);
	glfwSetErrorCallback(currentCallback);

	if (clipboard)
		newClipboard = lastClipboard = currClipboard = clipboard;

	standardCursors.resize((uint8)CursorType::Count - 1);
	for (uint8 i = 0; i < (uint8)standardCursors.size(); i++)
		standardCursors[i] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR + i);

	auto primaryMonitor = glfwGetPrimaryMonitor();
	if (primaryMonitor)
	{
		auto videoMode = glfwGetVideoMode(primaryMonitor);
		if (videoMode)
		{
			GARDEN_LOG_INFO("Monitor resolution: " + to_string(videoMode->width) + 
				"x" + to_string(videoMode->height) + " sc");
			GARDEN_LOG_INFO("Monitor refresh rate: " + to_string(videoMode->refreshRate) + " Hz");
		}
	}

	GARDEN_LOG_INFO("Window size: " + to_string(currWindowSize.x) + "x" + to_string(currWindowSize.y) + " sc");
	GARDEN_LOG_INFO("Framebuffer size: " + to_string(currFramebufferSize.x) + "x" + to_string(currFramebufferSize.y) + " px");
	GARDEN_LOG_INFO("Content scale: " + to_string(currContentScale.x) + "x" + to_string(currContentScale.y));
	systemTime = glfwGetTime();
}
void InputSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		for (uint8 i = 0; i < (uint8)standardCursors.size(); i++)
			glfwDestroyCursor((GLFWcursor*)standardCursors[i]);
		standardCursors.clear();

		ECSM_UNSUBSCRIBE_FROM_EVENT("Input", InputSystem::input);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Output", InputSystem::output);
	}
}

//**********************************************************************************************************************
void InputSystem::input()
{
	SET_CPU_ZONE_SCOPED("Input Update");

	#if GARDEN_OS_WINDOWS
		#if GARDEN_DEBUG
		if (mpmt::Thread::isCurrentMain())
			throw GardenError("Expected to run on a render thread."); // Note: See the startRenderThread().
		#endif

	eventLocker.lock();
	#endif

	if (GraphicsSystem::Instance::get()->isOutOfDateSwapchain())
	{
		if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		{
			auto vulkanAPI = VulkanAPI::get();
			auto surfaceCapabilities = vulkanAPI->physicalDevice.getSurfaceCapabilitiesKHR(vulkanAPI->surface);
			if (surfaceCapabilities.currentExtent != UINT32_MAX &&
				(surfaceCapabilities.currentExtent.width != newFramebufferSize.x ||
				surfaceCapabilities.currentExtent.height != newFramebufferSize.y))
			{
				auto pixelRatio = (float2)newWindowSize / newFramebufferSize;
				newFramebufferSize = uint2(surfaceCapabilities.currentExtent.width,
					surfaceCapabilities.currentExtent.height);
				newWindowSize = (uint2)((float2)newFramebufferSize * pixelRatio);
			}
		}
		else abort();
	}

	currFramebufferSize = newFramebufferSize;
	currWindowSize = newWindowSize;
	currContentScale = newContentScale;
	cursorDelta = newCursorPos - currCursorPos;
	currCursorPos = newCursorPos;
	currMouseScroll = newMouseScroll;
	newMouseScroll = 0.0f;
	lastCursorInWindow = currCursorInWindow;
	currCursorInWindow = newCursorInWindow;
	lastWindowInFocus = currWindowInFocus;
	currWindowInFocus = newWindowInFocus;

	swap(currKeyboardStates, lastKeyboardStates);
	copy(newKeyboardStates.begin(), newKeyboardStates.end(), currKeyboardStates.begin());
	swap(currMouseStates, lastMouseStates);
	copy(newMouseStates.begin(), newMouseStates.end(), currMouseStates.begin());

	swap(newKeyboardChars, currKeyboardChars);
	if (!newKeyboardChars.empty())
		newKeyboardChars.clear();
	swap(newFileDrops, currFileDrops);

	if (currClipboard != newClipboard)
		newClipboard = currClipboard;

	#if GARDEN_OS_WINDOWS
	eventLocker.unlock();
	#endif

	auto time = glfwGetTime();
	deltaTime = (time - systemTime) * timeMultiplier;
	currentTime += deltaTime;
	systemTime = time;

	if (!currFileDrops.empty())
	{
		const auto& event = Manager::Instance::get()->getEvent("FileDrop");
		for (const auto& path : currFileDrops)
		{
			currFileDropPath = &path;
			event.run();
		}
		currFileDropPath = nullptr;
		currFileDrops.clear();
	}
}
void InputSystem::output()
{
	SET_CPU_ZONE_SCOPED("Output Update");

	#if GARDEN_OS_WINDOWS
	eventLocker.lock();
	#endif

	currCursorMode = newCursorMode;

	if (newWindowTitle != "")
	{
		swap(currWindowTitle, newWindowTitle);
		newWindowTitle = "";
	}
	if (!newWindowIconPaths.empty())
	{
		swap(currWindowIconPaths, newWindowIconPaths);
		newWindowIconPaths.clear();
	}

	if (hasNewClipboard && newClipboard != currClipboard)
	{
		currClipboard = newClipboard;
		hasNewClipboard = false;
	}

	auto window = (GLFWwindow*)GraphicsAPI::get()->window;
	if (glfwWindowShouldClose(window))
		Manager::Instance::get()->isRunning = false;

	#if GARDEN_OS_WINDOWS
	eventLocker.unlock();
	glfwPostEmptyEvent();
	#endif
}

void InputSystem::setWindowIcon(const vector<string>& paths)
{
	newWindowIconPaths = paths;
}

//**********************************************************************************************************************
void InputSystem::startRenderThread()
{
	mpmt::Thread::setForegroundPriority();

	#if GARDEN_OS_WINDOWS
	auto renderThread = thread(InputSystem::renderThread);
	#else
	Manager::Instance::get()->isRunning = true;
	#endif

	auto inputSystem = InputSystem::Instance::get();
	auto window = (GLFWwindow*)GraphicsAPI::get()->window;

	while (!glfwWindowShouldClose(window))
	{
		#if GARDEN_OS_WINDOWS
		glfwWaitEvents();
		#else
		glfwPollEvents();
		#endif

		int framebufferX = 0.0, framebufferY = 0.0;
		glfwGetFramebufferSize(window, &framebufferX, &framebufferY);
		int windowX = 0.0, windowY = 0.0;
		glfwGetWindowSize(window, &windowX, &windowY);
		float contentX = 0.0f, contentY = 0.0f;
		glfwGetWindowContentScale(window, &contentX, &contentY);
		double cursorX = 0.0, cursorY = 0.0;
		glfwGetCursorPos(window, &cursorX, &cursorY);

		auto cursorInWindow = (bool)glfwGetWindowAttrib(window, GLFW_HOVERED);
		auto windowInFocus = (bool)glfwGetWindowAttrib(window, GLFW_FOCUSED);

		auto cursorMode = (CursorMode)(glfwGetInputMode(window, GLFW_CURSOR) - 0x00034001);
		int newCursorMode = -1, newCursorType = -1;

		auto currentCallback = glfwSetErrorCallback(nullptr);
		auto clipboard = glfwGetClipboardString(nullptr);
		glfwSetErrorCallback(currentCallback);
		string newClipboard; auto hasNewClipboard = false;

		string newWindowTitle = "";
		vector<vector<uint8>> imageData; vector<GLFWimage> images;

		#if GARDEN_OS_WINDOWS
		inputSystem->eventLocker.lock();
		#endif

		inputSystem->newFramebufferSize = uint2((uint32)framebufferX, (uint32)framebufferY);
		inputSystem->newWindowSize = uint2((uint32)windowX, (uint32)windowY);
		inputSystem->newContentScale = float2(contentX, contentY);
		inputSystem->newCursorPos = float2((float)cursorX, (float)cursorY);
		inputSystem->newMouseScroll += inputSystem->accumMouseScroll;
		inputSystem->accumMouseScroll = 0.0f;
		inputSystem->newCursorInWindow = cursorInWindow;
		inputSystem->newWindowInFocus = windowInFocus;

		if (inputSystem->currCursorMode != cursorMode)
			newCursorMode = (int)inputSystem->currCursorMode;
		if (inputSystem->currCursorType != inputSystem->newCursorType)
		{
			newCursorType = (int)inputSystem->newCursorType;
			inputSystem->currCursorType = inputSystem->newCursorType;
		}

		auto& newKeyboardStates = inputSystem->newKeyboardStates;
		for (uint8 i = 0; i < keyboardButtonCount; i++)
		{
			auto button = (int)allKeyboardButtons[i];
			newKeyboardStates[button] = glfwGetKey(window, button);
		}

		auto& newMouseStates = inputSystem->newMouseStates;
		for (int i = 0; i < (int)MouseButton::Last + 1; i++)
			newMouseStates[i] = glfwGetMouseButton(window, i);

		if (!inputSystem->accumKeyboardChars.empty())
		{
			inputSystem->newKeyboardChars.insert(inputSystem->newKeyboardChars.end(),
				inputSystem->accumKeyboardChars.begin(), inputSystem->accumKeyboardChars.end());
			inputSystem->accumKeyboardChars.clear();
		}

		if (inputSystem->currClipboard != inputSystem->lastClipboard)
		{
			newClipboard = inputSystem->lastClipboard = inputSystem->currClipboard;
			hasNewClipboard = true;
		}
		else
		{
			if (clipboard)
			{
				if (inputSystem->currClipboard != clipboard)
					inputSystem->lastClipboard = inputSystem->currClipboard = clipboard;
			}
			else if (inputSystem->currClipboard != "")
			{
				inputSystem->lastClipboard = inputSystem->currClipboard = "";
			}
		}

		if (!inputSystem->accumFileDrops.empty())
		{
			inputSystem->newFileDrops.insert(inputSystem->newFileDrops.end(),
				inputSystem->accumFileDrops.begin(), inputSystem->accumFileDrops.end());
			inputSystem->accumFileDrops.clear();
		}

		if (inputSystem->currWindowTitle != "")
			swap(newWindowTitle, inputSystem->currWindowTitle);

		if (!inputSystem->currWindowIconPaths.empty())
		{
			auto platform = glfwGetPlatform();
			if (platform == GLFW_PLATFORM_WIN32 || platform == GLFW_PLATFORM_X11)
			{
				auto resourceSystem = ResourceSystem::Instance::get();
				const auto& paths = inputSystem->currWindowIconPaths;
				imageData.resize(paths.size());
				images.resize(paths.size());

				for (psize i = 0; i < paths.size(); i++)
				{
					uint2 size; Image::Format format;
					resourceSystem->loadImageData(paths[i], imageData[i], size, format);

					GLFWimage image;
					image.width = size.x;
					image.height = size.y;
					image.pixels = imageData[i].data();
					images[i] = image;
				}
			}
			inputSystem->currWindowIconPaths.clear();
		}

		#if GARDEN_OS_WINDOWS
		inputSystem->eventLocker.unlock();
		#endif

		//if (newCursorMode != -1)
			// glfwSetInputMode(window, GLFW_CURSOR, newCursorMode + 0x00034001);
		if (newCursorType != -1)
		{
			glfwSetCursor(window, newCursorType == (int)CursorType::Default ?
				nullptr : (GLFWcursor*)inputSystem->standardCursors[newCursorType - 1]);
		}

		if (hasNewClipboard)
			glfwSetClipboardString(nullptr, newClipboard.c_str());
		if (newWindowTitle != "")
			glfwSetWindowTitle(window, newWindowTitle.c_str());
		if (!images.empty())
			glfwSetWindowIcon(window, images.size(), images.data());

		#if !GARDEN_OS_WINDOWS
		Manager::Instance::get()->update();
		#endif
	}

	glfwHideWindow(window);

	#if GARDEN_OS_WINDOWS
	renderThread.join();
	#else
	Manager::Instance::get()->isRunning = false;
	#endif
}