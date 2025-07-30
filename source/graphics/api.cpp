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

#include "garden/graphics/api.hpp"
#include "garden/graphics/vulkan/api.hpp"
#include "garden/graphics/glfw.hpp" // Note: Do not move it.

#if GARDEN_OS_WINDOWS
#define GLFW_EXPOSE_NATIVE_WIN32
#include "GLFW/glfw3native.h"
#pragma comment (lib, "Dwmapi")
#include <dwmapi.h>
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#endif

using namespace garden;

GraphicsAPI::GraphicsAPI(const string& appName, uint2 windowSize, bool isFullscreen, bool isDecorated)
{
	GLFWmonitor* primaryMonitor = nullptr;
	if (isFullscreen)
	{
		auto primaryMonitor = glfwGetPrimaryMonitor();
		if (primaryMonitor)
		{
			auto videoMode = glfwGetVideoMode(primaryMonitor);
			if (videoMode)
			{
				glfwWindowHint(GLFW_REFRESH_RATE, videoMode->refreshRate);
				glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
				windowSize.x = videoMode->width;
				windowSize.y = videoMode->height;
			}
		}
	}
	else
	{
		if (!isDecorated)
			glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
	}
	
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	auto window = glfwCreateWindow(windowSize.x, windowSize.y,
		appName.c_str(), primaryMonitor, nullptr);
	if (!window)
		throw GardenError("Failed to create GLFW window.");
	this->window = window;

	#if GARDEN_OS_WINDOWS
	BOOL value = TRUE;
	auto hwnd = glfwGetWin32Window(window);
	::DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
	#endif

	if (glfwRawMouseMotionSupported())
		glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
	glfwSetWindowSizeLimits(window, GraphicsAPI::minFramebufferSize, 
		GraphicsAPI::minFramebufferSize, GLFW_DONT_CARE, GLFW_DONT_CARE);
}
GraphicsAPI::~GraphicsAPI()
{
	glfwDestroyWindow((GLFWwindow*)window);
}

void GraphicsAPI::destroyResource(DestroyResourceType type, void* data0, void* data1, uint32 count)
{
	DestroyResource destroyResource;
	destroyResource.data0 = data0;
	destroyResource.data1 = data1;
	destroyResource.type = type;
	destroyResource.count = count;
	destroyBuffers[fillDestroyIndex].push_back(destroyResource);
}

//**********************************************************************************************************************
void GraphicsAPI::initialize(GraphicsBackend backendType, const string& appName, 
	const string& appDataName, Version appVersion, uint2 windowSize, uint32 threadCount, 
	bool useVsync, bool useTripleBuffering, bool isFullscreen, bool isDecorated)
{
	if (!glfwInit())
		throw GardenError("Failed to initialize GLFW.");

	glfwSetErrorCallback([](int error_code, const char* description)
	{
		throw GardenError("GLFW::ERROR: " + string(description) + " (code: " + to_string(error_code) + ")");
	});

	GARDEN_ASSERT_MSG(!apiInstance, "Graphics API is already initialized");
	if (backendType == GraphicsBackend::VulkanAPI)
	{
		apiInstance = new VulkanAPI(appName, appDataName, appVersion, windowSize, 
			threadCount, useVsync, useTripleBuffering, isFullscreen, isDecorated);
	}
}
void GraphicsAPI::terminate()
{
	GARDEN_ASSERT_MSG(apiInstance, "Graphics API is not initialized");
	delete apiInstance;
	apiInstance = nullptr;
	glfwTerminate();
}