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
#include "garden/graphics/glfw.hpp"
#include "garden/graphics/api.hpp"

using namespace garden;
using namespace garden::graphics;

//**********************************************************************************************************************
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
		inputSystem->fileDropPaths.push_back(paths[i]);
}

void InputSystem::onMouseScroll(void* window, double offsetX, double offsetY)
{
	InputSystem::Instance::get()->mouseScroll += float2((float)offsetX, (float)offsetY);
}

//**********************************************************************************************************************

InputSystem::InputSystem(bool setSingleton) : Singleton(setSingleton),
	lastKeyboardStates((psize)KeyboardButton::Last + 1, false),
	lastMouseStates((psize)MouseButton::Last + 1, false)
{
	auto manager = Manager::Instance::get();
	manager->registerEventBefore("Input", "Update");
	manager->registerEvent("FileDrop");

	ECSM_SUBSCRIBE_TO_EVENT("PreInit", InputSystem::preInit);
	ECSM_SUBSCRIBE_TO_EVENT("Input", InputSystem::input);
}
InputSystem::~InputSystem()
{
	if (Manager::Instance::get()->isRunning())
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreInit", InputSystem::preInit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Input", InputSystem::input);

		auto manager = Manager::Instance::get();
		manager->unregisterEvent("Input");
		manager->unregisterEvent("FileDrop");
	}

	unsetSingleton();
}

//**********************************************************************************************************************
static void updateFileDrops(vector<fs::path>& fileDropPaths, const fs::path*& currentFileDropPath)
{
	if (!fileDropPaths.empty())
	{
		const auto& subscribers = Manager::Instance::get()->getEventSubscribers("FileDrop");
		for (const auto& path : fileDropPaths)
		{
			currentFileDropPath = &path;
			for (const auto& onFileDrop : subscribers)
				onFileDrop();
		}
		currentFileDropPath = nullptr;
		fileDropPaths.clear();
	}
}
static void updateWindowMode()
{
	auto inputSystem = InputSystem::Instance::get();
	if (inputSystem->isKeyboardPressed(KeyboardButton::F11))
	{
		auto primaryMonitor = glfwGetPrimaryMonitor();
		auto videoMode = glfwGetVideoMode(primaryMonitor);

		auto window = (GLFWwindow*)GraphicsAPI::window;
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
}

//**********************************************************************************************************************
void InputSystem::preInit()
{
	auto window = (GLFWwindow*)GraphicsAPI::window;
	glfwSetDropCallback(window, (GLFWdropfun)onFileDrop);
	glfwSetScrollCallback(window, (GLFWscrollfun)onMouseScroll);

	double x = 0.0, y = 0.0;
	glfwGetCursorPos(window, &x, &y);
	cursorPosition = float2((float)x, (float)y);
}
void InputSystem::input()
{
	auto window = (GLFWwindow*)GraphicsAPI::window;
	mouseScroll = float2(0.0f);
	
	for (psize i = 0; i < keyboardButtonCount; i++)
	{
		auto button = (int)allKeyboardButtons[i];
		lastKeyboardStates[button] = glfwGetKey(window, button);
	}

	for (int i = 0; i < (int)MouseButton::Count; i++)
		lastMouseStates[i] = glfwGetMouseButton(window, i);

	glfwPollEvents();

	if (glfwWindowShouldClose(window))
	{
		// TODO: add modal if user sure want to exit.
		// And also allow to force quit or wait for running threads.
		glfwHideWindow(window);
		Manager::Instance::get()->stop();
		return;
	}

	auto currentTime = glfwGetTime();
	deltaTime = (currentTime - systemTime) * timeMultiplier;
	time += deltaTime;
	systemTime = currentTime;

	double x = 0.0, y = 0.0;
	glfwGetCursorPos(window, &x, &y);
	auto newPosition = float2((float)x, (float)y);
	cursorDelta = newPosition - cursorPosition;
	cursorPosition = newPosition;

	updateFileDrops(fileDropPaths, currentFileDropPath);
	updateWindowMode();
}

//**********************************************************************************************************************
bool InputSystem::isKeyboardPressed(KeyboardButton button) const noexcept
{
	GARDEN_ASSERT((int)button >= 0 && (int)button <= (int)KeyboardButton::Last);
	return !lastKeyboardStates[(psize)button] &&
		glfwGetKey((GLFWwindow*)GraphicsAPI::window, (int)button);
}
bool InputSystem::isKeyboardReleased(KeyboardButton button) const noexcept
{
	GARDEN_ASSERT((int)button >= 0 && (int)button <= (int)KeyboardButton::Last);
	return lastKeyboardStates[(psize)button] &&
		!glfwGetKey((GLFWwindow*)GraphicsAPI::window, (int)button);
}

bool InputSystem::isMousePressed(MouseButton button) const noexcept
{
	GARDEN_ASSERT((int)button >= 0 && (int)button <= (int)MouseButton::Last);
	return !lastMouseStates[(psize)button] &&
		glfwGetMouseButton((GLFWwindow*)GraphicsAPI::window, (int)button);
}
bool InputSystem::isMouseReleased(MouseButton button) const noexcept
{
	GARDEN_ASSERT((int)button >= 0 && (int)button <= (int)MouseButton::Last);
	return lastMouseStates[(psize)button] &&
		!glfwGetMouseButton((GLFWwindow*)GraphicsAPI::window, (int)button);
}

bool InputSystem::getKeyboardState(KeyboardButton button) const noexcept
{
	return glfwGetKey((GLFWwindow*)GraphicsAPI::window, (int)button);
}
bool InputSystem::getMouseState(MouseButton button) const noexcept
{
	return glfwGetMouseButton((GLFWwindow*)GraphicsAPI::window, (int)button);
}

void InputSystem::setCursorMode(CursorMode mode) noexcept
{
	if (cursorMode == mode)
		return;

	cursorMode = mode;
	glfwSetInputMode((GLFWwindow*)GraphicsAPI::window, GLFW_CURSOR, (int)mode);
}