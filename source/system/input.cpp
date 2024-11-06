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

#include "garden/system/input.hpp"
#include "garden/system/log.hpp"
#include "garden/system/graphics.hpp"
#include "garden/graphics/glfw.hpp"
#include "garden/graphics/vulkan/api.hpp"
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
		glfwSetWindowMonitor(window, nullptr,
			videoMode->width / 2 - InputSystem::defaultWindowWidth / 2,
			videoMode->height / 2 - InputSystem::defaultWindowHeight / 2,
			InputSystem::defaultWindowWidth, InputSystem::defaultWindowHeight,
			videoMode->refreshRate);
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

	#if GARDEN_EDITOR
	/* TODO: move these to the resources system
	for (int i = 0; i < count; i++)
	{
		auto path = fs::path(paths[i]).generic_string(); 
		auto length = path.length();
		if (length < 8)
			continue;

		psize pathOffset = 0;
		auto cmpPath = (GARDEN_RESOURCES_PATH / "scenes").generic_string();
		if (length > cmpPath.length())
		{
			if (memcmp(path.c_str(), cmpPath.c_str(), cmpPath.length()) == 0)
				pathOffset = cmpPath.length() + 1;
		}
		cmpPath = (GARDEN_APP_RESOURCES_PATH / "scenes").generic_string();
		if (length > cmpPath.length())
		{
			if (memcmp(path.c_str(), cmpPath.c_str(), cmpPath.length()) == 0)
				pathOffset = cmpPath.length() + 1;
		}

		if (memcmp(path.c_str() + (length - 5), "scene", 5) == 0)
		{
			fs::path filePath = path.c_str() + pathOffset; filePath.replace_extension();
			try
			{
				ResourceSystem::getInstance()->loadScene(filePath);
			}
			catch (const exception& e)
			{
				GARDEN_LOG_ERROR("Failed to load scene. (error: " + string(e.what()) + ")");
			}
			break;
		}
	}
	*/
	#endif

	auto inputSystem = InputSystem::Instance::get();
	for (int i = 0; i < count; i++)
		inputSystem->newFileDrops.push_back(paths[i]);
}

void InputSystem::onKeyboardChar(void* window, unsigned int codepoint)
{
	InputSystem::Instance::get()->newKeyboardChars.push_back(codepoint);
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
	currentKeyboardStates((psize)KeyboardButton::Last + 1, false),
	newMouseStates((psize)MouseButton::Last + 1, false),
	lastMouseStates((psize)MouseButton::Last + 1, false),
	currentMouseStates((psize)MouseButton::Last + 1, false)
{
	auto manager = Manager::Instance::get();
	manager->registerEventBefore("Input", "Update");
	manager->registerEventAfter("Output", "Update");
	manager->registerEvent("FileDrop");

	ECSM_SUBSCRIBE_TO_EVENT("PreInit", InputSystem::preInit);
	ECSM_SUBSCRIBE_TO_EVENT("Input", InputSystem::input);
	ECSM_SUBSCRIBE_TO_EVENT("Output", InputSystem::output);
}
InputSystem::~InputSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreInit", InputSystem::preInit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Input", InputSystem::input);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Output", InputSystem::output);

		auto manager = Manager::Instance::get();
		manager->unregisterEvent("Input");
		manager->unregisterEvent("FileDrop");
	}

	unsetSingleton();
}

//**********************************************************************************************************************
void InputSystem::preInit()
{
	auto window = (GLFWwindow*)GraphicsAPI::get()->window;
	glfwSetKeyCallback(window, (GLFWkeyfun)InputSystem::onKeyboardButton);
	// glfwSetMouseButtonCallback(window, (GLFWmousebuttonfun)InputSystem::onMouseButton);
	glfwSetScrollCallback(window, (GLFWscrollfun)InputSystem::onMouseScroll);
	glfwSetDropCallback(window, (GLFWdropfun)InputSystem::onFileDrop);
	glfwSetCharCallback(window, (GLFWcharfun)InputSystem::onKeyboardChar);

	int width = 0.0, height = 0.0;
	glfwGetFramebufferSize(window, &width, &height);
	framebufferSize = uint2((uint32)width, (uint32)height);

	glfwGetWindowSize(window, &width, &height);
	windowSize = uint2((uint32)width, (uint32)height);

	glfwGetWindowContentScale(window, &contentScale.x, &contentScale.y);

	double x = 0.0, y = 0.0;
	glfwGetCursorPos(window, &x, &y);
	newCursorPos = currentCursorPos = float2((float)x, (float)y);
	newCursorMode = currentCursorMode = (CursorMode)glfwGetInputMode(window, GLFW_CURSOR);

	for (uint8 i = 0; i < keyboardButtonCount; i++)
	{
		auto button = (int)allKeyboardButtons[i];
		auto state = glfwGetKey(window, button);
		newKeyboardStates[button] = lastKeyboardStates[button] = currentKeyboardStates[button] = state;
	}

	for (int i = 0; i < (int)MouseButton::Last + 1; i++)
	{
		auto state = glfwGetMouseButton(window, i);
		newMouseStates[i] = lastMouseStates[i] = currentMouseStates[i] = state;
	}

	auto clipboard = glfwGetClipboardString(nullptr);
	if (clipboard)
		lastClipboard = currentClipboard = clipboard;

	auto primaryMonitor = glfwGetPrimaryMonitor();
	if (primaryMonitor)
	{
		auto videoMode = glfwGetVideoMode(primaryMonitor);
		if (videoMode)
		{
			GARDEN_LOG_INFO("Monitor resolution: " + to_string(videoMode->width) + "x" + to_string(videoMode->height));
			GARDEN_LOG_INFO("Monitor refresh rate: " + to_string(videoMode->refreshRate));
		}
	}

	GARDEN_LOG_INFO("Window size: " + to_string(windowSize.x) + "x" + to_string(windowSize.y));
	GARDEN_LOG_INFO("Framebuffer size: " + to_string(framebufferSize.x) + "x" + to_string(framebufferSize.y));
	GARDEN_LOG_INFO("Content scale: " + to_string(contentScale.x) + "x" + to_string(contentScale.y));
}

//**********************************************************************************************************************
void InputSystem::input()
{
	inputLocker.lock();

	if (GraphicsSystem::Instance::get()->isOutOfDateSwapchain())
	{
		if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		{
			auto vulkanAPI = VulkanAPI::get();
			auto surfaceCapabilities = vulkanAPI->physicalDevice.getSurfaceCapabilitiesKHR(vulkanAPI->surface);
			if (surfaceCapabilities.currentExtent != UINT32_MAX &&
				(surfaceCapabilities.currentExtent.width != framebufferSize.x ||
				surfaceCapabilities.currentExtent.height != framebufferSize.y))
			{
				framebufferSize = uint2(surfaceCapabilities.currentExtent.width,
					surfaceCapabilities.currentExtent.height);
			}
		}
		else abort();
	}

	swap(currentKeyboardStates, lastKeyboardStates);
	copy(newKeyboardStates.begin(), newKeyboardStates.end(), currentKeyboardStates.begin());
	swap(currentMouseStates, lastMouseStates);
	copy(newMouseStates.begin(), newMouseStates.end(), currentMouseStates.begin());

	cursorDelta = newCursorPos - currentCursorPos;
	currentCursorPos = newCursorPos;
	
	currentMouseScroll = newMouseScroll;
	newMouseScroll = 0.0f;

	auto currentTime = glfwGetTime();
	deltaTime = (currentTime - systemTime) * timeMultiplier;
	time += deltaTime;
	systemTime = currentTime;
	
	if (!currentFileDrops.empty())
	{
		const auto& subscribers = Manager::Instance::get()->getEventSubscribers("FileDrop");
		for (const auto& path : currentFileDrops)
		{
			currentFileDropPath = &path;
			for (const auto& onFileDrop : subscribers)
				onFileDrop();
		}
		currentFileDropPath = nullptr;
		currentFileDrops.clear();
	}
}
void InputSystem::output()
{
	currentKeyboardChars.clear();
	currentFileDrops.clear();

	auto window = (GLFWwindow*)GraphicsAPI::get()->window;
	if (glfwWindowShouldClose(window))
		Manager::Instance::get()->isRunning = false;

	inputLocker.unlock();
	glfwPostEmptyEvent();
}

//**********************************************************************************************************************
void InputSystem::startRenderThread()
{
	mpmt::Thread::setForegroundPriority();

	auto renderThread = thread(InputSystem::renderThread);
	auto inputSystem = InputSystem::Instance::get();
	auto window = (GLFWwindow*)GraphicsAPI::get()->window;

	while (!glfwWindowShouldClose(window))
	{
		glfwWaitEvents();

		inputSystem->inputLocker.lock();

		int width = 0.0, height = 0.0;
		glfwGetFramebufferSize(window, &width, &height);
		inputSystem->framebufferSize = uint2((uint32)width, (uint32)height);

		glfwGetWindowSize(window, &width, &height);
		inputSystem->windowSize = uint2((uint32)width, (uint32)height);

		glfwGetWindowContentScale(window,
			&inputSystem->contentScale.x, &inputSystem->contentScale.y);

		auto& newKeyboardStates = inputSystem->newKeyboardStates;
		for (uint8 i = 0; i < keyboardButtonCount; i++)
		{
			auto button = (int)allKeyboardButtons[i];
			newKeyboardStates[button] = glfwGetKey(window, button);
		}

		if (!inputSystem->newKeyboardChars.empty())
		{
			inputSystem->currentKeyboardChars.insert(inputSystem->currentKeyboardChars.end(),
				inputSystem->newKeyboardChars.begin(), inputSystem->newKeyboardChars.end());
			inputSystem->newKeyboardChars.clear();
		}

		auto& newMouseStates = inputSystem->newMouseStates;
		for (int i = 0; i < (int)MouseButton::Last + 1; i++)
			newMouseStates[i] = glfwGetMouseButton(window, i);

		double x = 0.0, y = 0.0;
		glfwGetCursorPos(window, &x, &y);
		inputSystem->newCursorPos = float2((float)x, (float)y);

		if (inputSystem->currentCursorMode != inputSystem->newCursorMode)
		{
			glfwSetInputMode(window, GLFW_CURSOR, (int)inputSystem->newCursorMode);
			inputSystem->currentCursorMode = inputSystem->newCursorMode;
		}

		inputSystem->newMouseScroll = inputSystem->accumMouseScroll;
		inputSystem->accumMouseScroll = 0.0f;

		if (inputSystem->currentClipboard != inputSystem->lastClipboard)
		{
			glfwSetClipboardString(nullptr, inputSystem->currentClipboard.c_str());
			inputSystem->lastClipboard = inputSystem->currentClipboard;
		}
		else
		{
			auto clipboard = glfwGetClipboardString(nullptr);
			if (clipboard)
			{
				if (clipboard != inputSystem->currentClipboard)
					inputSystem->lastClipboard = inputSystem->currentClipboard = clipboard;
			}
			else if (inputSystem->currentClipboard != "")
			{
				inputSystem->lastClipboard = inputSystem->currentClipboard = "";
			}
		}

		if (!inputSystem->newFileDrops.empty())
		{
			inputSystem->currentFileDrops.insert(inputSystem->currentFileDrops.end(),
				inputSystem->newFileDrops.begin(), inputSystem->newFileDrops.end());
			inputSystem->newFileDrops.clear();
		}

		inputSystem->inputLocker.unlock();
	}

	glfwHideWindow(window);
	renderThread.join();
}