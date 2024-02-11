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
	auto manager = (Manager*)glfwGetWindowUserPointer((GLFWwindow*)window);
	auto logSystem = manager->tryGet<LogSystem>();
	if (logSystem) logSystem->info("Dropped " + to_string(count) + " items on a window.");

	#if GARDEN_EDITOR
	/* TODO: move these to the resources system
	for (int i = 0; i < count; i++)
	{
		auto path = fs::path(paths[i]).generic_string(); 
		auto length = path.length();
		if (length < 8) continue;

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
			try { ResourceSystem::getInstance()->loadScene(filePath); }
			catch (const exception& e)
			{
				if (logSystem)
					logSystem->error("Failed to load scene. (error: " + string(e.what()) + ")");
			}
			break;
		}
	}
	*/
	#endif

	auto inputSystem = manager->get<InputSystem>();
	for (int i = 0; i < count; i++)
		inputSystem->fileDropPaths.push_back(paths[i]);
}

InputSystem::InputSystem(Manager* manager) : System(manager)
{
	manager->registerEventBefore("Input", "Update");
	manager->registerEvent("FileDrop");
	SUBSCRIBE_TO_EVENT("PreInit", InputSystem::preInit);
	SUBSCRIBE_TO_EVENT("Input", InputSystem::input);

	for (auto button : allKeyboardButtons)
		lastKeyboardButtons.emplace(make_pair(button, false));
	for (int i = 0; i < (int)MouseButton::Count; i++)
		lastMouseButtons.emplace(make_pair((MouseButton)i, false));
}
InputSystem::~InputSystem()
{
	auto manager = getManager();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("PreInit", InputSystem::preInit);
		UNSUBSCRIBE_FROM_EVENT("Input", InputSystem::input);
		manager->unregisterEvent("Input");
		manager->unregisterEvent("FileDrop");
	}
}

//**********************************************************************************************************************
static void updateFileDrops(Manager* manager, vector<fs::path>& fileDropPaths, const fs::path*& currentFileDropPath)
{
	if (!fileDropPaths.empty())
	{
		auto& subscribers = manager->getEventSubscribers("FileDrop");
		for (const auto& path : fileDropPaths)
		{
			currentFileDropPath = &path;
			for (auto& onFileDrop : subscribers)
				onFileDrop();
		}
		currentFileDropPath = nullptr;
		fileDropPaths.clear();
	}
}
static void updateWindowMode(InputSystem* inputSystem)
{
	if (inputSystem->isKeyboardClicked(KeyboardButton::F11))
	{
		auto primaryMonitor = glfwGetPrimaryMonitor();
		auto videoMode = glfwGetVideoMode(primaryMonitor);

		auto window = (GLFWwindow*)GraphicsAPI::window;
		if (glfwGetWindowAttrib(window, GLFW_DECORATED) == GLFW_FALSE)
		{
			glfwSetWindowMonitor(window, nullptr,
				videoMode->width / 2 - defaultWindowWidth / 2,
				videoMode->height / 2 - defaultWindowHeight / 2,
				defaultWindowWidth, defaultWindowHeight, videoMode->refreshRate);
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
	glfwSetWindowUserPointer(window, getManager());
	glfwSetDropCallback(window, (GLFWdropfun)onFileDrop);

	double x = 0.0, y = 0.0;
	glfwGetCursorPos(window, &x, &y);
	cursorPosition = float2((float)x, (float)y);
}
void InputSystem::input()
{
	auto manager = getManager();
	auto window = (GLFWwindow*)GraphicsAPI::window;

	for (auto i = lastKeyboardButtons.begin(); i != lastKeyboardButtons.end(); i++)
		i->second = glfwGetKey(window, (int)i->first) == GLFW_PRESS;
	for (auto i = lastMouseButtons.begin(); i != lastMouseButtons.end(); i++)
		i->second = glfwGetMouseButton(window, (int)i->first) == GLFW_PRESS;

	glfwPollEvents();
	
	if (glfwWindowShouldClose(window))
	{
		// TODO: add modal if user sure want to exit.
		// And also allow to force quit or wait for running threads.
		glfwHideWindow(window);
		manager->stop();
		return;
	}

	auto newTime = glfwGetTime();
	deltaTime = newTime - time;
	time = newTime;

	double x = 0.0, y = 0.0;
	glfwGetCursorPos(window, &x, &y);
	cursorPosition = float2((float)x, (float)y);

	updateFileDrops(manager, fileDropPaths, currentFileDropPath);
	updateWindowMode(this);
}

//**********************************************************************************************************************
bool InputSystem::isKeyboardClicked(KeyboardButton button) const
{
	return lastKeyboardButtons.at(button) &&
		glfwGetKey((GLFWwindow*)GraphicsAPI::window, (int)button) == GLFW_RELEASE;
}
bool InputSystem::isMouseClicked(MouseButton button) const
{
	return lastMouseButtons.at(button) &&
		glfwGetMouseButton((GLFWwindow*)GraphicsAPI::window, (int)button) == GLFW_RELEASE;
}

bool InputSystem::isKeyboardPressed(KeyboardButton button) const
{
	return glfwGetKey((GLFWwindow*)GraphicsAPI::window, (int)button) == GLFW_PRESS;
}
bool InputSystem::isMousePressed(MouseButton button) const
{
	return glfwGetMouseButton((GLFWwindow*)GraphicsAPI::window, (int)button) == GLFW_PRESS;
}

void InputSystem::setCursorMode(CursorMode mode)
{
	if (cursorMode == mode) return;
	cursorMode = mode;
	glfwSetInputMode((GLFWwindow*)GraphicsAPI::window, GLFW_CURSOR, (int)mode);
}