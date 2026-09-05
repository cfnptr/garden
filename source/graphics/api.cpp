// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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
#include "garden/system/log.hpp" // TODO: we should not include system here

#if GARDEN_DEBUG
#include "garden/graphics/renderdoc-app.h"
static RENDERDOC_API_1_7_0* rdocApi = NULL;
#endif

#if GARDEN_USE_BASIS_UNIVERSAL
	#if GARDEN_EDITOR
	#include "basisu_enc.h"
	#endif
#include "basisu_transcoder.h"
#endif

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

GraphicsAPI::GraphicsAPI(const string& appName, const string& appID, uint2 windowSize, 
	ThreadPool* threadPool, bool isFullscreen, bool isDecorated) : threadPool(threadPool)
{
	#if GARDEN_OS_LINUX
	glfwWindowHintString(GLFW_WAYLAND_APP_ID, appID.c_str());
	#endif

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

	if (glfwRawMouseMotionSupported())
		glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
	glfwSetWindowSizeLimits(window, GraphicsAPI::minFramebufferSize, 
		GraphicsAPI::minFramebufferSize, GLFW_DONT_CARE, GLFW_DONT_CARE);

	#if GARDEN_OS_LINUX
	switch(glfwGetPlatform())
	{
		case GLFW_PLATFORM_X11: displayProtocol = DisplayProtocol::X11; break;
		case GLFW_PLATFORM_WAYLAND: displayProtocol = DisplayProtocol::Wayland; break;
		default: abort();
	}
	// TODO: android
	#elif GARDEN_OS_APPLE
	displayProtocol = DisplayProtocol::Metal;
	#elif GARDEN_OS_WINDOWS
	BOOL value = TRUE; auto hwnd = glfwGetWin32Window(window);
	::DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
	displayProtocol = DisplayProtocol::Win32;
	#endif
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
static bool isEnv(const char* name, const char* value) noexcept
{
	auto envValue = getenv(name);
	if (!envValue) return false;
	return strcmp(envValue, value) == 0;
}

void GraphicsAPI::initialize(GraphicsBackend backendType, const string& appName, const string& appID, 
	const string& appDataName, Version appVersion, uint2 windowSize, ThreadPool* threadPool, 
	bool useVsync, bool useTripleBuffering, bool isFullscreen, bool isDecorated)
{
	#if GARDEN_DEBUG
		#if GARDEN_OS_LINUX || GARDEN_OS_APPLE
		int result = 1;
		if(auto mod = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD))
		{
			// TODO: For android replace librenderdoc.so with libVkLayer_GLES_RenderDoc.so
			auto renderDocGetAPI = (pRENDERDOC_GetAPI)dlsym(mod, "RENDERDOC_GetAPI");
			result = renderDocGetAPI(eRENDERDOC_API_Version_1_7_0, (void**)&rdocApi);
		}
		#elif GARDEN_OS_WINDOWS
		if(auto mod = GetModuleHandleA("renderdoc.dll"))
		{
			auto renderDocGetAPI = (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
			result = renderDocGetAPI(eRENDERDOC_API_Version_1_7_0, (void**)&rdocApi);
		}
		#endif
		GARDEN_ASSERT_MSG(result == 1, "Failed to get RenderDoc API instance");
	#endif

	#if GARDEN_USE_BASIS_UNIVERSAL
		#if GARDEN_DEBUG
		// basisu::enable_debug_printf(true);
		#endif
		#if GARDEN_EDITOR
		if (!basisu::basisu_encoder_init(GARDEN_USE_OPENCL ? true : false))
			throw GardenError("Failed to initialize basis universal encoder.");
		#endif
	basist::basisu_transcoder_init();
	#endif

	#if GARDEN_OS_LINUX
	if (isEnv("GLFW_PLATFORM", "x11") || GARDEN_USE_MESA_RGP)
		glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
	#endif

	if (!glfwInit())
		throw GardenError("Failed to initialize GLFW.");

	glfwSetErrorCallback([](int error_code, const char* description)
	{
		GARDEN_LOG_ERROR("GLFW::ERROR: " + string(description) + " (code: " + to_string(error_code) + ")");
	});

	GARDEN_ASSERT_MSG(!apiInstance, "Graphics API is already initialized");
	if (backendType == GraphicsBackend::VulkanAPI)
	{
		apiInstance = new VulkanAPI(appName, appID, appDataName, appVersion, windowSize, 
			threadPool, useVsync, useTripleBuffering, isFullscreen, isDecorated);
	}
	else abort();
}
void GraphicsAPI::terminate()
{
	GARDEN_ASSERT_MSG(apiInstance, "Graphics API is not initialized");
	delete apiInstance;
	apiInstance = nullptr;
	glfwTerminate();
}

#if GARDEN_DEBUG
bool GraphicsAPI::hasFrameDebugger()
{
	return rdocApi;
}
void GraphicsAPI::startFrameCapture()
{
	if (rdocApi) rdocApi->StartFrameCapture(NULL, NULL);
	else abort(); // TODO: implement other graphics debuggers.
}
void GraphicsAPI::stopFrameCapture()
{
	if (rdocApi) rdocApi->EndFrameCapture(NULL, NULL);
	else abort();
}
#endif