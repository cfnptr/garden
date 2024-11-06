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

#include "garden/system/graphics.hpp"
#include "garden/system/log.hpp"
#include "garden/system/input.hpp"
#include "garden/system/thread.hpp"
#include "garden/system/camera.hpp"
#include "garden/system/settings.hpp"
#include "garden/system/app-info.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/transform.hpp"
#include "garden/graphics/vulkan/api.hpp"
#include "garden/graphics/imgui-impl.hpp"
#include "garden/graphics/glfw.hpp"
#include "garden/resource/primitive.hpp"
#include "garden/profiler.hpp"

#include "math/matrix/transform.hpp"
#include "garden/os.hpp"

using namespace garden;
using namespace garden::primitive;

namespace
{
	struct LinePC
	{
		float4x4 mvp;
		float4 color;
		float4 startPoint;
		float4 endPoint;
	};
	struct AabbPC
	{
		float4x4 mvp;
		float4 color;
	};
}

#if GARDEN_EDITOR
//**********************************************************************************************************************
static void initializeImGui() // TODO: Separate into an ImGuiSystem
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	setImGuiStyle();

	auto graphicsAPI = GraphicsAPI::get();
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto window = (GLFWwindow*)graphicsAPI->window;
	auto framebufferSize = graphicsAPI->swapchain->getFramebufferSize();

	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		auto swapchainBuffer = vulkanAPI->swapchain->getCurrentBuffer();
		auto swapchainImage = vulkanAPI->imagePool.get(swapchainBuffer->colorImage);

		vector<Framebuffer::Subpass> subpasses =
		{
			Framebuffer::Subpass(PipelineType::Graphics, {},
			{
				Framebuffer::OutputAttachment(swapchainImage->getDefaultView(), false, true, true)
			})
		};

		// Hack for the ImGui render pass creation.
		auto imGuiFramebuffer = graphicsSystem->createFramebuffer(framebufferSize, std::move(subpasses));
		auto framebufferView = vulkanAPI->framebufferPool.get(imGuiFramebuffer);
		ImGuiData::renderPass = (VkRenderPass)FramebufferExt::getRenderPass(**framebufferView);
		vulkanAPI->device.destroyFramebuffer(vk::Framebuffer((VkFramebuffer)
			ResourceExt::getInstance(**framebufferView)));
		ResourceExt::getInstance(**framebufferView) = nullptr;
		FramebufferExt::getRenderPass(**framebufferView) = nullptr;
		graphicsSystem->destroy(imGuiFramebuffer);

		vk::FramebufferCreateInfo framebufferInfo({}, ImGuiData::renderPass,
			1, nullptr, framebufferSize.x, framebufferSize.y, 1);
		ImGuiData::framebuffers.resize(vulkanAPI->swapchain->getBufferCount());

		auto& swapchainBuffers = vulkanAPI->swapchain->getBuffers();
		for (uint32 i = 0; i < (uint32)ImGuiData::framebuffers.size(); i++)
		{
			auto colorImage = vulkanAPI->imagePool.get(swapchainBuffers[i]->colorImage);
			auto imageView = vulkanAPI->imageViewPool.get(colorImage->getDefaultView());
			framebufferInfo.pAttachments = (vk::ImageView*)&ResourceExt::getInstance(**imageView);
			ImGuiData::framebuffers[i] = vulkanAPI->device.createFramebuffer(framebufferInfo);
		}
		
		ImGui_ImplGlfw_InitForVulkan(window, false);
		ImGui_ImplVulkan_InitInfo init_info = {};
		init_info.Instance = vulkanAPI->instance;
		init_info.PhysicalDevice = vulkanAPI->physicalDevice;
		init_info.Device = vulkanAPI->device;
		init_info.QueueFamily = vulkanAPI->graphicsQueueFamilyIndex;
		init_info.Queue = vulkanAPI->frameQueue;
		init_info.DescriptorPool = vulkanAPI->descriptorPool;
		init_info.RenderPass = ImGuiData::renderPass;
		init_info.MinImageCount = 2;
		init_info.ImageCount = (uint32)ImGuiData::framebuffers.size();
		init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		init_info.PipelineCache = vulkanAPI->pipelineCache;
		init_info.Subpass = 0;
		init_info.UseDynamicRendering = false; // TODO: use it instead of render pass hack.
		init_info.Allocator = nullptr;
		init_info.CheckVkResultFn = imGuiCheckVkResult;
		init_info.MinAllocationSize = 1024 * 1024;
		ImGui_ImplVulkan_Init(&init_info);
	}
	else abort();
	
	auto inputSystem = InputSystem::Instance::get();
	auto windowSize = inputSystem->getWindowSize();
	auto contentScale = inputSystem->getContentScale();
	auto pixelRatioXY = (float2)framebufferSize / windowSize;
	auto pixelRatio = std::max(pixelRatioXY.x, pixelRatioXY.y);
	auto fontScale = std::max(contentScale.x, contentScale.y);

	auto& io = ImGui::GetIO();
	const auto fontPath = "fonts/dejavu-bold.ttf";
	const auto fontSize = 14.0f * fontScale;

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

	io.FontGlobalScale = 1.0f / pixelRatio;
	io.DisplayFramebufferScale = ImVec2(pixelRatioXY.x, pixelRatioXY.y);
	// TODO: dynamically detect when system scale is changed or moved to another monitor and recreate fonts.

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
}
static void terminateImGui()
{
	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();

		auto vulkanAPI = VulkanAPI::get();
		for (auto framebuffer : ImGuiData::framebuffers)
			vulkanAPI->device.destroyFramebuffer(framebuffer);
		vulkanAPI->device.destroyRenderPass(ImGuiData::renderPass);
	}
	else abort();

	ImGui::DestroyContext();
}
static void recreateImGui()
{
	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		auto framebufferSize = vulkanAPI->swapchain->getFramebufferSize();

		for (auto framebuffer : ImGuiData::framebuffers)
			vulkanAPI->device.destroyFramebuffer(framebuffer);

		vk::FramebufferCreateInfo framebufferInfo({}, ImGuiData::renderPass,
			1, nullptr, framebufferSize.x, framebufferSize.y, 1);
		auto& swapchainBuffers = vulkanAPI->swapchain->getBuffers();
		ImGuiData::framebuffers.resize(swapchainBuffers.size());

		for (uint32 i = 0; i < (uint32)swapchainBuffers.size(); i++)
		{
			auto colorImage = vulkanAPI->imagePool.get(swapchainBuffers[i]->colorImage);
			auto imageView = vulkanAPI->imageViewPool.get(colorImage->getDefaultView());
			framebufferInfo.pAttachments = (vk::ImageView*)&ResourceExt::getInstance(**imageView);
			ImGuiData::framebuffers[i] = vulkanAPI->device.createFramebuffer(framebufferInfo);
		}
	}
	else abort();
}
#endif

static ID<ImageView> createDepthStencilBuffer(uint2 size, Image::Format format)
{
	auto depthImage = GraphicsSystem::Instance::get()->createImage(format, 
		Image::Bind::TransferDst | Image::Bind::DepthStencilAttachment | Image::Bind::Sampled |
		Image::Bind::Fullscreen, { { nullptr } }, size, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(depthImage, "image.depthBuffer");
	auto imageView = GraphicsAPI::get()->imagePool.get(depthImage);
	return imageView->getDefaultView();
}

//**********************************************************************************************************************
GraphicsSystem::GraphicsSystem(uint2 windowSize, Image::Format depthStencilFormat, bool isFullscreen, 
	bool useVsync, bool useTripleBuffering, bool useAsyncRecording, bool _setSingleton) : Singleton(false),
	asyncRecording(useAsyncRecording), useVsync(useVsync), useTripleBuffering(useTripleBuffering)
{
	auto manager = Manager::Instance::get();
	manager->registerEventAfter("Render", "Update");
	manager->registerEventAfter("Present", "Render");
	manager->registerEvent("SwapchainRecreate");

	ECSM_SUBSCRIBE_TO_EVENT("PreInit", GraphicsSystem::preInit);
	ECSM_SUBSCRIBE_TO_EVENT("PreDeinit", GraphicsSystem::preDeinit);
	ECSM_SUBSCRIBE_TO_EVENT("Update", GraphicsSystem::update);
	ECSM_SUBSCRIBE_TO_EVENT("Present", GraphicsSystem::present);

	if (_setSingleton)
		setSingleton();

	auto appInfoSystem = AppInfoSystem::Instance::get();
	auto threadCount = getBestForegroundThreadCount();

	GraphicsAPI::initialize(GraphicsBackend::VulkanAPI, appInfoSystem->getName(), appInfoSystem->getAppDataName(),
		appInfoSystem->getVersion(), windowSize, threadCount, useVsync, useTripleBuffering, isFullscreen);

	auto graphicsAPI = GraphicsAPI::get();
	auto swapchainBuffer = graphicsAPI->swapchain->getCurrentBuffer();
	auto swapchainImage = graphicsAPI->imagePool.get(swapchainBuffer->colorImage);
	auto swapchainImageView = swapchainImage->getDefaultView();
	auto framebufferSize = (uint2)swapchainImage->getSize();

	if (depthStencilFormat != Image::Format::Undefined)
		depthStencilBuffer = createDepthStencilBuffer(framebufferSize, depthStencilFormat);

	swapchainFramebuffer = graphicsAPI->framebufferPool.create(
		framebufferSize, swapchainImageView, depthStencilBuffer);
	SET_RESOURCE_DEBUG_NAME(swapchainFramebuffer, "framebuffer.swapchain");

	auto swapchainBufferCount = graphicsAPI->swapchain->getBufferCount();
	const auto& swapchainBuffers = graphicsAPI->swapchain->getBuffers();
	cameraConstantsBuffers.resize(swapchainBufferCount);

	for (uint32 i = 0; i < swapchainBufferCount; i++)
	{
		SET_RESOURCE_DEBUG_NAME(swapchainBuffers[i]->colorImage, "image.swapchain" + to_string(i));

		auto constantsBuffer = createBuffer(Buffer::Bind::Uniform, Buffer::Access::SequentialWrite, 
			sizeof(CameraConstants), Buffer::Usage::Auto, Buffer::Strategy::Size);
		SET_RESOURCE_DEBUG_NAME(constantsBuffer, "buffer.uniform.cameraConstants" + to_string(i));
		cameraConstantsBuffers[i].push_back(constantsBuffer);
	}
}
GraphicsSystem::~GraphicsSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		// Note: constants buffers and other resources will destroyed by terminating graphics API.

		ECSM_UNSUBSCRIBE_FROM_EVENT("PreInit", GraphicsSystem::preInit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreDeinit", GraphicsSystem::preDeinit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Update", GraphicsSystem::update);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Present", GraphicsSystem::present);

		auto manager = Manager::Instance::get();
		manager->unregisterEvent("Render");
		manager->unregisterEvent("Present");
		manager->unregisterEvent("SwapchainRecreate");

		#if GARDEN_EDITOR
		terminateImGui();
		#endif

		GraphicsAPI::terminate();
	}
	
	unsetSingleton();
}

//**********************************************************************************************************************
static string getVkDeviceDriverVersion()
{
	auto vulkanAPI = VulkanAPI::get();
	auto version = vulkanAPI->deviceProperties.properties.driverVersion;
	if (vulkanAPI->deviceProperties.properties.vendorID == 0x10DE) // Nvidia
	{
		return to_string((version >> 22u) & 0x3FFu) + "." + to_string((version >> 14u) & 0x0FFu) + "." +
			to_string((version >> 6u) & 0x0ff) + "." + to_string(version & 0x003Fu);
	}

	#if GARDEN_OS_WINDOWS
	if (vulkanAPI->deviceProperties.properties.vendorID == 0x8086) // Intel
		return to_string(version >> 14u) + "." + to_string(version & 0x3FFFu);
	#endif

	if (vulkanAPI->deviceProperties.properties.vendorID == 0x14E4) // Broadcom
		return to_string(version / 10000) + "." + to_string((version % 10000) / 100);

	if (vulkanAPI->deviceProperties.properties.vendorID == 0x1010) // ImgTec
	{
		if (version > 500000000)
			return "0.0." + to_string(version);
		else
			return to_string(version);
	}

	return to_string(VK_API_VERSION_MAJOR(version)) + "." + to_string(VK_API_VERSION_MINOR(version)) + "." +
		to_string(VK_API_VERSION_PATCH(version)) + "." + to_string(VK_API_VERSION_VARIANT(version));
}
static void logVkGpuInfo()
{
	auto vulkanAPI = VulkanAPI::get();
	GARDEN_LOG_INFO("GPU: " + string(vulkanAPI->deviceProperties.properties.deviceName.data()));
	auto apiVersion = vulkanAPI->deviceProperties.properties.apiVersion;
	GARDEN_LOG_INFO("Device Vulkan API: " + to_string(VK_API_VERSION_MAJOR(apiVersion)) + "." +
		to_string(VK_API_VERSION_MINOR(apiVersion)) + "." + to_string(VK_API_VERSION_PATCH(apiVersion)));
	GARDEN_LOG_INFO("Driver version: " + getVkDeviceDriverVersion());

	if (vulkanAPI->isCacheLoaded)
		GARDEN_LOG_INFO("Loaded existing pipeline cache.");
	else
		GARDEN_LOG_INFO("Created a new pipeline cache.");
}

void GraphicsSystem::preInit()
{
	ECSM_SUBSCRIBE_TO_EVENT("Input", GraphicsSystem::input);

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		logVkGpuInfo();
	else abort();

	auto settingsSystem = SettingsSystem::Instance::tryGet();
	if (settingsSystem)
	{
		settingsSystem->getBool("useVsync", useVsync);
		settingsSystem->getInt("frameRate", frameRate);
		settingsSystem->getFloat("renderScale", renderScale);
	}

	#if GARDEN_EDITOR
	initializeImGui();
	#endif
}
void GraphicsSystem::preDeinit()
{
	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		VulkanAPI::get()->device.waitIdle();
	else abort();

	if (Manager::Instance::get()->isRunning)
		ECSM_UNSUBSCRIBE_FROM_EVENT("Input", GraphicsSystem::input);
}

//**********************************************************************************************************************
static float4x4 calcView(const TransformComponent* transform)
{
	return rotate(normalize(transform->rotation)) * translate(
		scale(transform->scale), -transform->position);
}
static float4x4 calcRelativeView(const TransformComponent* transform)
{
	auto view = calcView(transform);
	auto nextParent = transform->getParent();
	auto transformSystem = TransformSystem::Instance::get();

	while (nextParent)
	{
		auto nextTransformView = transformSystem->getComponent(nextParent);
		auto parentModel = ::calcModel(nextTransformView->position,
			nextTransformView->rotation, nextTransformView->scale);
		view = parentModel * view;
		nextParent = nextTransformView->getParent();
	}

	return view;
}

static void recreateCameraBuffers(DescriptorSetBuffers& cameraConstantsBuffers)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	graphicsSystem->destroy(cameraConstantsBuffers);

	auto swapchainBufferCount = GraphicsAPI::get()->swapchain->getBufferCount();
	cameraConstantsBuffers.resize(swapchainBufferCount);

	for (uint32 i = 0; i < swapchainBufferCount; i++)
	{
		auto constantsBuffer = graphicsSystem->createBuffer(Buffer::Bind::Uniform, Buffer::Access::SequentialWrite, 
			sizeof(CameraConstants), Buffer::Usage::Auto, Buffer::Strategy::Size);
		SET_RESOURCE_DEBUG_NAME(constantsBuffer, "buffer.uniform.cameraConstants" + to_string(i));
		cameraConstantsBuffers[i].resize(1); cameraConstantsBuffers[i][0] = constantsBuffer;
	}
}

static void updateCurrentFramebuffer(ID<Framebuffer> swapchainFramebuffer,
	ID<ImageView> depthStencilBuffer, uint2 framebufferSize)
{
	auto graphicsAPI = GraphicsAPI::get();
	auto swapchainBuffer = graphicsAPI->swapchain->getCurrentBuffer();
	auto framebufferView = graphicsAPI->framebufferPool.get(swapchainFramebuffer);
	auto colorImage = graphicsAPI->imagePool.get(swapchainBuffer->colorImage);
	FramebufferExt::getSize(**framebufferView) = framebufferSize;
	FramebufferExt::getColorAttachments(**framebufferView)[0].imageView = colorImage->getDefaultView();
	FramebufferExt::getDepthStencilAttachment(**framebufferView).imageView = depthStencilBuffer;
}

//**********************************************************************************************************************
static void prepareCameraConstants(ID<Entity> camera, ID<Entity> directionalLight,
	uint2 scaledFramebufferSize, CameraConstants& cameraConstants)
{
	auto manager = Manager::Instance::get();

	auto transformView = manager->tryGet<TransformComponent>(camera);
	if (transformView)
	{
		cameraConstants.view = calcRelativeView(*transformView);
		setTranslation(cameraConstants.view, float3(0.0f));
		cameraConstants.cameraPos = float4(transformView->position, 0.0f);
	}
	else
	{
		cameraConstants.view = float4x4::identity;
		cameraConstants.cameraPos = 0.0f;
	}

	auto cameraView = manager->tryGet<CameraComponent>(camera);
	if (cameraView)
	{
		cameraConstants.projection = cameraView->calcProjection();
		cameraConstants.nearPlane = cameraView->getNearPlane();
	}
	else
	{
		cameraConstants.projection = float4x4::identity;
		cameraConstants.nearPlane = defaultHmdDepth;
	}

	cameraConstants.viewProj = cameraConstants.projection * cameraConstants.view;
	cameraConstants.viewInverse = inverse(cameraConstants.view);
	cameraConstants.projInverse = inverse(cameraConstants.projection);
	cameraConstants.viewProjInv = inverse(cameraConstants.viewProj);
	auto viewDir = cameraConstants.view * float4(float3::front, 1.0f);
	cameraConstants.viewDir = float4(normalize((float3)viewDir), 0.0f);

	if (directionalLight)
	{
		auto lightTransformView = manager->tryGet<TransformComponent>(directionalLight);
		if (lightTransformView)
		{
			auto lightDir = lightTransformView->rotation * float3::front;
			cameraConstants.lightDir = float4(normalize(lightDir), 0.0f);
		}
		else
		{
			cameraConstants.lightDir = float4(float3::bottom, 0.0f);
		}
	}
	else
	{
		cameraConstants.lightDir = float4(float3::bottom, 0.0f);
	}

	cameraConstants.frameSize = scaledFramebufferSize;
	cameraConstants.frameSizeInv = 1.0f / (float2)scaledFramebufferSize;
	cameraConstants.frameSizeInv2 = cameraConstants.frameSizeInv * 2.0f;
}

//**********************************************************************************************************************
static int getKeyboardMods(InputSystem* inputSystem)
{
	int mods = 0;
	if (inputSystem->getKeyboardState(KeyboardButton::LeftShift) ||
		inputSystem->getKeyboardState(KeyboardButton::RightShift))
		mods |= GLFW_MOD_SHIFT;
	if (inputSystem->getKeyboardState(KeyboardButton::LeftControl) ||
		inputSystem->getKeyboardState(KeyboardButton::RightControl))
		mods |= GLFW_MOD_CONTROL;
	if (inputSystem->getKeyboardState(KeyboardButton::LeftAlt) ||
		inputSystem->getKeyboardState(KeyboardButton::RightAlt))
		mods |= GLFW_MOD_ALT;
	if (inputSystem->getKeyboardState(KeyboardButton::LeftSuper) ||
		inputSystem->getKeyboardState(KeyboardButton::RightSuper))
		mods |= GLFW_MOD_SUPER;
	// TODO: check caps lock if enabled GLFW_LOCK_KEY_MODS input mode
	return mods;
}

#if GARDEN_EDITOR
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

// WARNING! Check for changes in the underlying ImGui functions to prevent possible race conditions!
static void updateImGuiGlfwInput() 
{
	auto& io = ImGui::GetIO();
	auto inputSystem = InputSystem::Instance::get();
	auto window = (GLFWwindow*)GraphicsAPI::get()->window;
	auto cursorPos = inputSystem->getCursorPosition();
	ImGui_ImplGlfw_CursorPosCallback(window, cursorPos.x, cursorPos.y);

	if (inputSystem->isCursorEntered())
		ImGui_ImplGlfw_CursorEnterCallback(window, true);
	else if (inputSystem->isCursorLeaved())
		ImGui_ImplGlfw_CursorEnterCallback(window, false);

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
static void imGuiNewFrame()
{
	auto& io = ImGui::GetIO();
	auto inputSystem = InputSystem::get();

	auto windowSize = inputSystem->getWindowSize();
	auto framebufferSize = inputSystem->getFramebufferSize();
	io.DisplaySize = ImVec2(windowSize.x, windowSize.y);

	if (windowSize.x > 0 && windowSize.y > 0)
	{
		io.DisplayFramebufferScale = ImVec2((float)framebufferSize.x / windowSize.x, 
			(float)framebufferSize.y / windowSize.y);
	}

	io.DeltaTime = inputSystem->getDeltaTime();

	// TODO: implement these if required for something:
	//
	// ImGui_ImplGlfw_UpdateMouseData();
	// ImGui_ImplGlfw_UpdateMouseCursor();
	// ImGui_ImplGlfw_UpdateGamepads();
}
#endif

//**********************************************************************************************************************
void GraphicsSystem::input()
{
	auto inputSystem = InputSystem::Instance::get();
	auto windowFramebufferSize = inputSystem->getFramebufferSize();
	isFramebufferSizeValid = windowFramebufferSize.x > 0 && windowFramebufferSize.y > 0;
	beginSleepClock = isFramebufferSizeValid ? 0.0 : mpio::OS::getCurrentClock();

	#if GARDEN_EDITOR
	updateImGuiGlfwInput();
	#endif
}
void GraphicsSystem::update()
{
	SET_CPU_ZONE_SCOPED("Graphics Update");

	auto graphicsAPI = GraphicsAPI::get();
	auto swapchain = graphicsAPI->swapchain;
	auto inputSystem = InputSystem::Instance::get();

	SwapchainChanges newSwapchainChanges;
	newSwapchainChanges.framebufferSize = inputSystem->getFramebufferSize() != swapchain->getFramebufferSize();
	newSwapchainChanges.bufferCount = useTripleBuffering != swapchain->isUseTripleBuffering();
	newSwapchainChanges.vsyncState = useVsync != swapchain->isUseVsync();

	auto swapchainRecreated = isFramebufferSizeValid && (newSwapchainChanges.framebufferSize ||
		newSwapchainChanges.bufferCount || newSwapchainChanges.vsyncState || outOfDateSwapchain);

	swapchainChanges.framebufferSize |= newSwapchainChanges.framebufferSize;
	swapchainChanges.bufferCount |= newSwapchainChanges.bufferCount;
	swapchainChanges.vsyncState |= newSwapchainChanges.vsyncState;
	
	if (swapchainRecreated)
	{
		SET_CPU_ZONE_SCOPED("Swapchain Recreate");

		swapchain->recreate(inputSystem->getFramebufferSize(), useVsync, useTripleBuffering);
		auto framebufferSize = swapchain->getFramebufferSize();
		outOfDateSwapchain = false;

		if (depthStencilBuffer)
		{
			auto depthStencilBufferView = graphicsAPI->imageViewPool.get(depthStencilBuffer);
			auto depthStencilImageView = graphicsAPI->imagePool.get(depthStencilBufferView->getImage());
			if (framebufferSize != (uint2)depthStencilImageView->getSize())
			{
				auto format = depthStencilBufferView->getFormat();
				destroy(depthStencilBufferView->getImage());
				depthStencilBuffer = createDepthStencilBuffer(framebufferSize, format);
			}
		}

		#if GARDEN_EDITOR
		recreateImGui();
		#endif

		GARDEN_LOG_INFO("Recreated swapchain. (" + to_string(framebufferSize.x) + "x" +
			to_string(framebufferSize.y) + ", " + to_string(swapchain->getBufferCount()) + "B)");
	}

	if (swapchain->getBufferCount() != cameraConstantsBuffers.size())
	{
		recreateCameraBuffers(cameraConstantsBuffers);
		swapchainChanges.bufferCount = true;
	}

	if (isFramebufferSizeValid)
	{
		auto threadSystem = ThreadSystem::Instance::tryGet();
		if (!swapchain->acquireNextImage(&threadSystem->getForegroundPool()))
		{
			isFramebufferSizeValid = false;
			outOfDateSwapchain = true;
			GARDEN_LOG_DEBUG("Out of date swapchain.");
		}
	}

	updateCurrentFramebuffer(swapchainFramebuffer, 
		depthStencilBuffer, swapchain->getFramebufferSize());
	
	if (swapchainRecreated || forceRecreateSwapchain)
	{
		SET_CPU_ZONE_SCOPED("Swapchain Recreate");
		Manager::Instance::get()->runEvent("SwapchainRecreate");
		swapchainChanges = {};
		forceRecreateSwapchain = false;
	}

	if (camera)
	{
		prepareCameraConstants(camera, directionalLight,
			getScaledFramebufferSize(), currentCameraConstants);
		auto cameraBuffer = graphicsAPI->bufferPool.get(
			cameraConstantsBuffers[swapchain->getCurrentBufferIndex()][0]);
		cameraBuffer->writeData(&currentCameraConstants);
	}

	if (isFramebufferSizeValid)
	{
		#if GARDEN_EDITOR
		if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
			ImGui_ImplVulkan_NewFrame();
		else abort();

		imGuiNewFrame();
		ImGui::NewFrame();
		#endif

		if (renderScale != 1.0f) // TODO: make this optional
		{
			startRecording(CommandBufferType::Frame);
			auto depthStencilBufferView = graphicsAPI->imageViewPool.get(depthStencilBuffer);
			graphicsAPI->imagePool.get(depthStencilBufferView->getImage())->clear(0.0f, 0x00);
			stopRecording();
		}
	}
}
void GraphicsSystem::present()
{
	SET_CPU_ZONE_SCOPED("Frame Present");

	auto graphicsAPI = GraphicsAPI::get();
	if (isFramebufferSizeValid)
	{
		#if GARDEN_EDITOR
		startRecording(CommandBufferType::Frame);
		{
			SET_CPU_ZONE_SCOPED("ImGui Render");
			INSERT_GPU_DEBUG_LABEL("ImGui", Color::transparent);
			ImGui::Render();
		}
		stopRecording();
		#endif
		
		graphicsAPI->graphicsCommandBuffer->submit();
		graphicsAPI->transferCommandBuffer->submit();
		graphicsAPI->computeCommandBuffer->submit();
		graphicsAPI->frameCommandBuffer->submit();
	}
	
	graphicsAPI->descriptorSetPool.dispose();
	graphicsAPI->computePipelinePool.dispose();
	graphicsAPI->graphicsPipelinePool.dispose();
	graphicsAPI->framebufferPool.dispose();
	graphicsAPI->imageViewPool.dispose();
	graphicsAPI->imagePool.dispose();
	graphicsAPI->bufferPool.dispose();
	graphicsAPI->flushDestroyBuffer();

	if (isFramebufferSizeValid)
	{
		if (!graphicsAPI->swapchain->present())
		{
			isFramebufferSizeValid = false;
			outOfDateSwapchain = true;
			GARDEN_LOG_DEBUG("Out of date swapchain.");
		}
		frameIndex++;
	}
	else
	{
		auto endClock = mpio::OS::getCurrentClock();
		auto deltaClock = (endClock - beginSleepClock) * 1000.0;
		auto delayTime = 1000 / frameRate - (int)deltaClock;
		if (delayTime > 0)
			this_thread::sleep_for(chrono::milliseconds(delayTime));
		// TODO: use loop with empty cycles to improve sleep precision.
	}

	tickIndex++;

	#if GARDEN_TRACY_PROFILER
	FrameMark;
	#endif
}

//**********************************************************************************************************************
void GraphicsSystem::setRenderScale(float renderScale)
{
	if (renderScale == this->renderScale)
		return;

	this->renderScale = renderScale;

	SwapchainChanges swapchainChanges;
	swapchainChanges.framebufferSize = true;
	recreateSwapchain(swapchainChanges);
}

uint2 GraphicsSystem::getFramebufferSize() const noexcept
{
	return GraphicsAPI::get()->swapchain->getFramebufferSize();
}
uint2 GraphicsSystem::getScaledFramebufferSize() const noexcept
{
	auto framebufferSize = GraphicsAPI::get()->swapchain->getFramebufferSize();
	return max((uint2)(float2(framebufferSize) * renderScale), uint2(1));
}

uint32 GraphicsSystem::getSwapchainSize() const noexcept
{
	return (uint32)GraphicsAPI::get()->swapchain->getBufferCount();
}
uint32 GraphicsSystem::getSwapchainIndex() const noexcept
{
	return GraphicsAPI::get()->swapchain->getCurrentBufferIndex();
}

//**********************************************************************************************************************
ID<Buffer> GraphicsSystem::getFullSquareVertices()
{
	if (!fullSquareVertices)
	{
		fullSquareVertices = createBuffer(Buffer::Bind::Vertex |
			Buffer::Bind::TransferDst, Buffer::Access::None, fullSquareVert2D);
		SET_RESOURCE_DEBUG_NAME(fullSquareVertices, "buffer.vertex.fullSquare");
	}
	return fullSquareVertices;
}
ID<Buffer> GraphicsSystem::getFullCubeVertices()
{
	if (!fullCubeVertices)
	{
		fullCubeVertices = createBuffer(Buffer::Bind::Vertex |
			Buffer::Bind::TransferDst, Buffer::Access::None, fullCubeVert);
		SET_RESOURCE_DEBUG_NAME(fullCubeVertices, "buffer.vertex.fullCube");
	}
	return fullCubeVertices;
}


ID<ImageView> GraphicsSystem::getEmptyTexture()
{
	if (!emptyTexture)
	{
		const Color data[1] = { Color::transparent };
		auto texture = createImage(Image::Format::UnormR8G8B8A8,
			Image::Bind::Sampled | Image::Bind::TransferDst, { { data } }, uint2(1));
		SET_RESOURCE_DEBUG_NAME(texture, "image.emptyTexture");
		emptyTexture = GraphicsAPI::get()->imagePool.get(texture)->getDefaultView();
	}
	return emptyTexture;
}
ID<ImageView> GraphicsSystem::getWhiteTexture()
{
	if (!whiteTexture)
	{
		const Color data[1] = { Color::white };
		auto texture = createImage(Image::Format::UnormR8G8B8A8,
			Image::Bind::Sampled | Image::Bind::TransferDst, { { data } }, uint2(1));
		SET_RESOURCE_DEBUG_NAME(texture, "image.whiteTexture");
		whiteTexture = GraphicsAPI::get()->imagePool.get(texture)->getDefaultView();
	}
	return whiteTexture;
}
ID<ImageView> GraphicsSystem::getGreenTexture()
{
	if (!greenTexture)
	{
		const Color data[1] = { Color::green };
		auto texture = createImage(Image::Format::UnormR8G8B8A8,
			Image::Bind::Sampled | Image::Bind::TransferDst, { { data } }, uint2(1));
		SET_RESOURCE_DEBUG_NAME(texture, "image.greenTexture");
		greenTexture = GraphicsAPI::get()->imagePool.get(texture)->getDefaultView();
	}
	return greenTexture;
}
ID<ImageView> GraphicsSystem::getNormalMapTexture()
{
	if (!normalMapTexture)
	{
		const Color data[1] = { Color(127, 127, 255, 255) };
		auto texture = createImage(Image::Format::UnormR8G8B8A8,
			Image::Bind::Sampled | Image::Bind::TransferDst, { { data } }, uint2(1));
		SET_RESOURCE_DEBUG_NAME(texture, "image.normalMapTexture");
		normalMapTexture = GraphicsAPI::get()->imagePool.get(texture)->getDefaultView();
	}
	return normalMapTexture;
}

//**********************************************************************************************************************
void GraphicsSystem::recreateSwapchain(const SwapchainChanges& changes)
{
	GARDEN_ASSERT(changes.framebufferSize || changes.bufferCount || changes.vsyncState);
	swapchainChanges.framebufferSize |= changes.framebufferSize;
	swapchainChanges.bufferCount |= changes.bufferCount;
	swapchainChanges.vsyncState |= changes.vsyncState;
	forceRecreateSwapchain = true;
}

#if GARDEN_DEBUG || GARDEN_EDITOR
//**********************************************************************************************************************
void GraphicsSystem::setDebugName(ID<Buffer> buffer, const string& name)
{
	auto resource = GraphicsAPI::get()->bufferPool.get(buffer);
	resource->setDebugName(name);
}
void GraphicsSystem::setDebugName(ID<Image> image, const string& name)
{
	auto resource = GraphicsAPI::get()->imagePool.get(image);
	resource->setDebugName(name);
}
void GraphicsSystem::setDebugName(ID<ImageView> imageView, const string& name)
{
	auto resource = GraphicsAPI::get()->imageViewPool.get(imageView);
	resource->setDebugName(name);
}
void GraphicsSystem::setDebugName(ID<Framebuffer> framebuffer, const string& name)
{
	auto resource = GraphicsAPI::get()->framebufferPool.get(framebuffer);
	resource->setDebugName(name);
}
void GraphicsSystem::setDebugName(ID<DescriptorSet> descriptorSet, const string& name)
{
	auto resource = GraphicsAPI::get()->descriptorSetPool.get(descriptorSet);
	resource->setDebugName(name);
}
#endif

//**********************************************************************************************************************
ID<Buffer> GraphicsSystem::createBuffer(Buffer::Bind bind, Buffer::Access access,
	const void* data, uint64 size, Buffer::Usage usage, Buffer::Strategy strategy)
{
	GARDEN_ASSERT(size > 0);

	#if GARDEN_DEBUG
	if (data)
		GARDEN_ASSERT(hasAnyFlag(bind, Buffer::Bind::TransferDst));
	#endif

	auto graphicsAPI = GraphicsAPI::get();
	auto buffer = graphicsAPI->bufferPool.create(bind, access, usage, strategy, size, 0);
	SET_RESOURCE_DEBUG_NAME(buffer, "buffer" + to_string(*buffer));

	if (data)
	{
		auto bufferView = graphicsAPI->bufferPool.get(buffer);
		if (bufferView->isMappable())
		{
			bufferView->writeData(data, size);
		}
		else
		{
			auto stagingBuffer = graphicsAPI->bufferPool.create(
				Buffer::Bind::TransferSrc, Buffer::Access::SequentialWrite,
				Buffer::Usage::Auto, Buffer::Strategy::Speed, size, 0);
			SET_RESOURCE_DEBUG_NAME(stagingBuffer,
				"buffer.staging" + to_string(*stagingBuffer));
			auto stagingBufferView = graphicsAPI->bufferPool.get(stagingBuffer);
			stagingBufferView->writeData(data, size);

			if (!isRecording())
			{
				startRecording(CommandBufferType::TransferOnly);
				Buffer::copy(stagingBuffer, buffer);
				stopRecording();
			}
			else
			{
				Buffer::copy(stagingBuffer, buffer);
			}

			graphicsAPI->bufferPool.destroy(stagingBuffer);
		}
	}

	return buffer;
}
void GraphicsSystem::destroy(ID<Buffer> buffer)
{
	GraphicsAPI::get()->bufferPool.destroy(buffer);
}
View<Buffer> GraphicsSystem::get(ID<Buffer> buffer) const
{
	return GraphicsAPI::get()->bufferPool.get(buffer);
}

//**********************************************************************************************************************
ID<Image> GraphicsSystem::createImage(Image::Type type, Image::Format format, Image::Bind bind,
	const Image::Mips& data, const uint3& size, Image::Strategy strategy, Image::Format dataFormat)
{
	GARDEN_ASSERT(format != Image::Format::Undefined);
	GARDEN_ASSERT(!data.empty());
	GARDEN_ASSERT(size > 0u);

	auto mipCount = (uint8)data.size();
	auto layerCount = (uint32)data[0].size();

	#if GARDEN_DEBUG
	if (type == Image::Type::Texture1D)
	{
		GARDEN_ASSERT(size.y == 1);
		GARDEN_ASSERT(size.z == 1);
		GARDEN_ASSERT(layerCount == 1);
	}
	else if (type == Image::Type::Texture2D)
	{
		GARDEN_ASSERT(size.z == 1);
		GARDEN_ASSERT(layerCount == 1);
	}
	else if (type == Image::Type::Texture3D)
	{
		GARDEN_ASSERT(layerCount == 1);
	}
	else if (type == Image::Type::Texture1DArray)
	{
		GARDEN_ASSERT(size.y == 1);
		GARDEN_ASSERT(size.z == 1);
		GARDEN_ASSERT(layerCount >= 1);
	}
	else if (type == Image::Type::Texture2DArray)
	{
		GARDEN_ASSERT(size.z == 1);
		GARDEN_ASSERT(layerCount >= 1);
	}
	else if (type == Image::Type::Cubemap)
	{
		GARDEN_ASSERT(size.x == size.y);
		GARDEN_ASSERT(size.z == 1);
		GARDEN_ASSERT(layerCount == 6);
	}
	else abort();
	#endif

	if (dataFormat == Image::Format::Undefined)
		dataFormat = format;

	auto mipSize = size;
	auto formatBinarySize = (uint64)toBinarySize(dataFormat);
	uint64 stagingSize = 0;
	uint32 stagingCount = 0;

	for (uint8 mip = 0; mip < mipCount; mip++)
	{
		const auto& mipData = data[mip];
		auto binarySize = formatBinarySize * mipSize.x * mipSize.y * mipSize.z;

		for (auto layerData : mipData)
		{
			if (!layerData)
				continue;
			stagingSize += binarySize;
			stagingCount++;
		}

		mipSize = max(mipSize / 2u, uint3(1));
	}

	auto graphicsAPI = GraphicsAPI::get();
	auto image = graphicsAPI->imagePool.create(type, format, bind, strategy, size, mipCount, layerCount, 0);
	SET_RESOURCE_DEBUG_NAME(image, "image" + to_string(*image));

	if (stagingCount > 0)
	{
		GARDEN_ASSERT(hasAnyFlag(bind, Image::Bind::TransferDst));
		auto stagingBuffer = graphicsAPI->bufferPool.create(Buffer::Bind::TransferSrc,
			Buffer::Access::SequentialWrite, Buffer::Usage::Auto, Buffer::Strategy::Speed, stagingSize, 0);
		SET_RESOURCE_DEBUG_NAME(stagingBuffer, "buffer.imageStaging" + to_string(*stagingBuffer));
		
		ID<Image> targetImage; 
		if (format == dataFormat)
		{
			targetImage = image;
		}
		else
		{
			targetImage = graphicsAPI->imagePool.create(type, dataFormat, Image::Bind::TransferDst |
				Image::Bind::TransferSrc, Image::Strategy::Speed, size, mipCount, layerCount, 0);
			SET_RESOURCE_DEBUG_NAME(targetImage, "image.staging" + to_string(*targetImage));
		}

		auto stagingBufferView = graphicsAPI->bufferPool.get(stagingBuffer);
		auto stagingMap = stagingBufferView->getMap();
		vector<Image::CopyBufferRegion> regions(stagingCount);
		mipSize = size;
		uint64 stagingOffset = 0;
		uint32 copyIndex = 0;

		for (uint8 mip = 0; mip < mipCount; mip++)
		{
			const auto& mipData = data[mip];
			auto binarySize = formatBinarySize * mipSize.x * mipSize.y * mipSize.z;

			for (uint32 layer = 0; layer < layerCount; layer++)
			{
				auto layerData = mipData[layer];
				if (!layerData)
					continue;

				Image::CopyBufferRegion region;
				region.bufferOffset = stagingOffset;
				region.imageExtent = mipSize;
				region.imageBaseLayer = layer;
				region.imageLayerCount = 1;
				region.imageMipLevel = mip;
				regions[copyIndex++] = region;

				memcpy(stagingMap + stagingOffset, layerData, binarySize);
				stagingOffset += binarySize;
			}

			mipSize = max(mipSize / 2u, uint3(1));
		}

		GARDEN_ASSERT(stagingCount == copyIndex);
		GARDEN_ASSERT(stagingSize == stagingOffset);

		stagingBufferView->flush();

		if (format != dataFormat)
		{
			auto shouldEnd = false;
			if (!isRecording())
			{
				startRecording(CommandBufferType::Graphics);
				shouldEnd = true;
			}

			Image::copy(stagingBuffer, targetImage, regions);
			graphicsAPI->bufferPool.destroy(stagingBuffer);

			vector<Image::BlitRegion> blitRegions(mipCount);
			mipSize = size;

			for (uint8 i = 0; i < mipCount; i++)
			{
				Image::BlitRegion region;
				region.srcExtent = mipSize;
				region.dstExtent = mipSize;
				region.layerCount = layerCount;
				region.srcMipLevel = i;
				region.dstMipLevel = i;
				blitRegions[i] = region;
				mipSize = max(mipSize / 2u, uint3(1));
			}

			Image::blit(targetImage, image, blitRegions);
			if (shouldEnd)
				stopRecording();
			graphicsAPI->imagePool.destroy(targetImage);
		}
		else
		{
			if (!isRecording())
			{
				startRecording(CommandBufferType::TransferOnly);
				Image::copy(stagingBuffer, targetImage, regions);
				stopRecording();
			}
			else
			{
				Image::copy(stagingBuffer, targetImage, regions);
			}
			graphicsAPI->bufferPool.destroy(stagingBuffer);
		}
	}

	return image;
}
void GraphicsSystem::destroy(ID<Image> image)
{
	auto graphicsAPI = GraphicsAPI::get();
	if (image)
	{
		auto imageView = graphicsAPI->imagePool.get(image);
		GARDEN_ASSERT(!imageView->isSwapchain());

		if (imageView->hasDefaultView() && !graphicsAPI->forceResourceDestroy)
			graphicsAPI->imageViewPool.destroy(imageView->getDefaultView());
	}
	
	graphicsAPI->imagePool.destroy(image);
}
View<Image> GraphicsSystem::get(ID<Image> image) const
{
	return GraphicsAPI::get()->imagePool.get(image);
}

//**********************************************************************************************************************
ID<ImageView> GraphicsSystem::createImageView(ID<Image> image, Image::Type type,
	Image::Format format, uint8 baseMip, uint8 mipCount, uint32 baseLayer, uint32 layerCount)
{
	GARDEN_ASSERT(image);

	auto graphicsAPI = GraphicsAPI::get();
	auto _image = graphicsAPI->imagePool.get(image);
	GARDEN_ASSERT(ResourceExt::getInstance(**_image));
	GARDEN_ASSERT(mipCount + baseMip <= _image->getMipCount());
	GARDEN_ASSERT(layerCount + baseLayer <= _image->getLayerCount());

	if (format == Image::Format::Undefined)
		format = _image->getFormat();
	if (mipCount == 0)
		mipCount = _image->getMipCount();
	if (layerCount == 0)
		layerCount = _image->getLayerCount();

	if (type != Image::Type::Texture1DArray && type != Image::Type::Texture2DArray)
		GARDEN_ASSERT(layerCount == 1);

	auto imageView = graphicsAPI->imageViewPool.create(false, image,
		type, format, baseMip, mipCount, baseLayer, layerCount);
	SET_RESOURCE_DEBUG_NAME(imageView, "imageView" + to_string(*imageView));
	return imageView;
}
void GraphicsSystem::destroy(ID<ImageView> imageView)
{
	#if GARDEN_DEBUG
	if (imageView)
		GARDEN_ASSERT(!GraphicsAPI::get()->imageViewPool.get(imageView)->isDefault());
	#endif
	GraphicsAPI::get()->imageViewPool.destroy(imageView);
}
View<ImageView> GraphicsSystem::get(ID<ImageView> imageView) const
{
	return GraphicsAPI::get()->imageViewPool.get(imageView);
}

//**********************************************************************************************************************
// TODO: add checks if attachments do not overlaps and repeat.
ID<Framebuffer> GraphicsSystem::createFramebuffer(uint2 size,
	vector<Framebuffer::OutputAttachment>&& colorAttachments, Framebuffer::OutputAttachment depthStencilAttachment)
{
	GARDEN_ASSERT(size > 0u);
	GARDEN_ASSERT(!colorAttachments.empty() || depthStencilAttachment.imageView);
	auto graphicsAPI = GraphicsAPI::get();

	// TODO: we can use attachments with different sizes, but should we?
	#if GARDEN_DEBUG
	for	(auto colorAttachment : colorAttachments)
	{
		GARDEN_ASSERT(colorAttachment.imageView);
		auto imageView = graphicsAPI->imageViewPool.get(colorAttachment.imageView);
		GARDEN_ASSERT(isFormatColor(imageView->getFormat()));
		auto image = graphicsAPI->imagePool.get(imageView->getImage());
		GARDEN_ASSERT(size == calcSizeAtMip((uint2)image->getSize(), imageView->getBaseMip()));
		GARDEN_ASSERT(hasAnyFlag(image->getBind(), Image::Bind::ColorAttachment));
	}
	if (depthStencilAttachment.imageView)
	{
		auto imageView = graphicsAPI->imageViewPool.get(depthStencilAttachment.imageView);
		GARDEN_ASSERT(isFormatDepthOrStencil(imageView->getFormat()));
		auto image = graphicsAPI->imagePool.get(imageView->getImage());
		GARDEN_ASSERT(size == calcSizeAtMip((uint2)image->getSize(), imageView->getBaseMip()));
		GARDEN_ASSERT(hasAnyFlag(image->getBind(), Image::Bind::DepthStencilAttachment));
	}
	#endif

	auto framebuffer = graphicsAPI->framebufferPool.create(size,
		std::move(colorAttachments), depthStencilAttachment);
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer" + to_string(*framebuffer));
	return framebuffer;
}
ID<Framebuffer> GraphicsSystem::createFramebuffer(uint2 size, vector<Framebuffer::Subpass>&& subpasses)
{
	GARDEN_ASSERT(size > 0u);
	GARDEN_ASSERT(!subpasses.empty());
	auto graphicsAPI = GraphicsAPI::get();

	#if GARDEN_DEBUG
	psize outputAttachmentCount = 0;
	for (const auto& subpass : subpasses)
	{
		for	(auto inputAttachment : subpass.inputAttachments)
		{
			GARDEN_ASSERT(inputAttachment.imageView);
			GARDEN_ASSERT(inputAttachment.shaderStages != ShaderStage::None);
			auto imageView = graphicsAPI->imageViewPool.get(inputAttachment.imageView);
			auto image = graphicsAPI->imagePool.get(imageView->getImage());
			GARDEN_ASSERT(size == calcSizeAtMip((uint2)image->getSize(), imageView->getBaseMip()));
			GARDEN_ASSERT(hasAnyFlag(image->getBind(), Image::Bind::InputAttachment));
		}

		for (uint32 i = 0; i < (uint32)subpass.outputAttachments.size(); i++)
		{
			auto outputAttachment = subpass.outputAttachments[i];
			GARDEN_ASSERT(outputAttachment.imageView);
			GARDEN_ASSERT((!outputAttachment.clear && !outputAttachment.load) ||
				(outputAttachment.clear && !outputAttachment.load) ||
				(!outputAttachment.clear && outputAttachment.load));
			auto imageView = graphicsAPI->imageViewPool.get(outputAttachment.imageView);
			auto image = graphicsAPI->imagePool.get(imageView->getImage());
			GARDEN_ASSERT(size == calcSizeAtMip((uint2)image->getSize(), imageView->getBaseMip()));
			#if GARDEN_DEBUG
			if (isFormatColor(imageView->getFormat()))
				GARDEN_ASSERT(hasAnyFlag(image->getBind(), Image::Bind::ColorAttachment));
			#endif
			outputAttachmentCount++;

			for	(auto inputAttachment : subpass.inputAttachments)
				GARDEN_ASSERT(outputAttachment.imageView != inputAttachment.imageView);
		}
	}

	GARDEN_ASSERT(outputAttachmentCount > 0);
	#endif

	auto framebuffer = graphicsAPI->framebufferPool.create(size, std::move(subpasses));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer" + to_string(*framebuffer));
	return framebuffer;
}
void GraphicsSystem::destroy(ID<Framebuffer> framebuffer)
{
	#if GARDEN_DEBUG
	if (framebuffer)
		GARDEN_ASSERT(!GraphicsAPI::get()->framebufferPool.get(framebuffer)->isSwapchainFramebuffer());
	#endif
	GraphicsAPI::get()->framebufferPool.destroy(framebuffer);
}
View<Framebuffer> GraphicsSystem::get(ID<Framebuffer> framebuffer) const
{
	return GraphicsAPI::get()->framebufferPool.get(framebuffer);
}

//**********************************************************************************************************************
void GraphicsSystem::destroy(ID<GraphicsPipeline> graphicsPipeline)
{
	GraphicsAPI::get()->graphicsPipelinePool.destroy(graphicsPipeline);
}
View<GraphicsPipeline> GraphicsSystem::get(ID<GraphicsPipeline> graphicsPipeline) const
{
	return GraphicsAPI::get()->graphicsPipelinePool.get(graphicsPipeline);
}

void GraphicsSystem::destroy(ID<ComputePipeline> computePipeline)
{
	GraphicsAPI::get()->computePipelinePool.destroy(computePipeline);
}
View<ComputePipeline> GraphicsSystem::get(ID<ComputePipeline> computePipeline) const
{
	return GraphicsAPI::get()->computePipelinePool.get(computePipeline);
}

//**********************************************************************************************************************
ID<DescriptorSet> GraphicsSystem::createDescriptorSet(ID<GraphicsPipeline> graphicsPipeline,
	map<string, DescriptorSet::Uniform>&& uniforms, uint8 index)
{
	GARDEN_ASSERT(graphicsPipeline);
	GARDEN_ASSERT(!uniforms.empty());

	#if GARDEN_DEBUG
	auto pipelineView = GraphicsAPI::get()->graphicsPipelinePool.get(graphicsPipeline);
	GARDEN_ASSERT(ResourceExt::getInstance(**pipelineView)); // is ready
	GARDEN_ASSERT(index < PipelineExt::getDescriptorSetLayouts(**pipelineView).size());
	#endif

	auto descriptorSet = GraphicsAPI::get()->descriptorSetPool.create(
		ID<Pipeline>(graphicsPipeline), PipelineType::Graphics, std::move(uniforms), index);
	SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet" + to_string(*descriptorSet));
	return descriptorSet;
}
ID<DescriptorSet> GraphicsSystem::createDescriptorSet(ID<ComputePipeline> computePipeline,
	map<string, DescriptorSet::Uniform>&& uniforms, uint8 index)
{
	GARDEN_ASSERT(computePipeline);
	GARDEN_ASSERT(!uniforms.empty());

	#if GARDEN_DEBUG
	auto pipelineView = GraphicsAPI::get()->computePipelinePool.get(computePipeline);
	GARDEN_ASSERT(ResourceExt::getInstance(**pipelineView)); // is ready
	GARDEN_ASSERT(index < PipelineExt::getDescriptorSetLayouts(**pipelineView).size());
	#endif

	auto descriptorSet = GraphicsAPI::get()->descriptorSetPool.create(
		ID<Pipeline>(computePipeline), PipelineType::Compute, std::move(uniforms), index);
	SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet" + to_string(*descriptorSet));
	return descriptorSet;
}
void GraphicsSystem::destroy(ID<DescriptorSet> descriptorSet)
{
	GraphicsAPI::get()->descriptorSetPool.destroy(descriptorSet);
}
View<DescriptorSet> GraphicsSystem::get(ID<DescriptorSet> descriptorSet) const
{
	return GraphicsAPI::get()->descriptorSetPool.get(descriptorSet);
}

//**********************************************************************************************************************
bool GraphicsSystem::isRecording() const noexcept
{
	return GraphicsAPI::get()->currentCommandBuffer;
}
void GraphicsSystem::startRecording(CommandBufferType commandBufferType)
{
	auto graphicsAPI = GraphicsAPI::get();

	#if GARDEN_DEBUG
	if (graphicsAPI->currentCommandBuffer)
		throw GardenError("Already recording.");
	#endif
	
	switch (commandBufferType)
	{
	case CommandBufferType::Frame:
		graphicsAPI->currentCommandBuffer = graphicsAPI->frameCommandBuffer; break;
	case CommandBufferType::Graphics:
		graphicsAPI->currentCommandBuffer = graphicsAPI->graphicsCommandBuffer; break;
	case CommandBufferType::TransferOnly:
		graphicsAPI->currentCommandBuffer = graphicsAPI->transferCommandBuffer; break;
	case CommandBufferType::ComputeOnly:
		graphicsAPI->currentCommandBuffer = graphicsAPI->computeCommandBuffer; break;
	default: abort();
	}
}
void GraphicsSystem::stopRecording()
{
	#if GARDEN_DEBUG
	if (!GraphicsAPI::get()->currentCommandBuffer)
		throw GardenError("Not recording.");
	#endif
	GraphicsAPI::get()->currentCommandBuffer = nullptr;
}

#if GARDEN_DEBUG || GARDEN_EDITOR
//**********************************************************************************************************************
void GraphicsSystem::drawLine(const float4x4& mvp, const float3& startPoint,
	const float3& endPoint, const float4& color)
{
	if (!linePipeline)
	{
		linePipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline(
			"editor/wireframe-line", swapchainFramebuffer, false, false);
	}

	auto pipelineView = GraphicsAPI::get()->graphicsPipelinePool.get(linePipeline);
	pipelineView->bind();
	pipelineView->setViewportScissor();
	auto pushConstants = pipelineView->getPushConstants<LinePC>();
	pushConstants->mvp = mvp;
	pushConstants->color = color;
	pushConstants->startPoint = float4(startPoint, 1.0f);
	pushConstants->endPoint = float4(endPoint, 1.0f);
	pipelineView->pushConstants();
	pipelineView->draw({}, 24);
}
void GraphicsSystem::drawAabb(const float4x4& mvp, const float4& color)
{
	if (!aabbPipeline)
	{
		aabbPipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline(
			"editor/aabb-lines", swapchainFramebuffer, false, false);
	}

	auto pipelineView = GraphicsAPI::get()->graphicsPipelinePool.get(aabbPipeline);
	pipelineView->bind();
	pipelineView->setViewportScissor();
	auto pushConstants = pipelineView->getPushConstants<AabbPC>();
	pushConstants->mvp = mvp;
	pushConstants->color = color;
	pipelineView->pushConstants();
	pipelineView->draw({}, 24);
}
#endif