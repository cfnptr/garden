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
#include "garden/graphics/vulkan.hpp"
#include "garden/graphics/imgui-impl.hpp"
#include "garden/graphics/glfw.hpp"
#include "garden/resource/primitive.hpp"
#include "garden/profiler.hpp"

#include "math/matrix/transform.hpp"
#include "mpio/os.hpp"

using namespace mpio;
using namespace garden;
using namespace garden::primitive;

#if GARDEN_EDITOR
//**********************************************************************************************************************
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

void GraphicsSystem::initializeImGui()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	setImGuiStyle();

	const auto& swapchainBuffer = Vulkan::swapchain.getCurrentBuffer();
	auto swapchainImage = GraphicsAPI::imagePool.get(swapchainBuffer.colorImage);

	vector<Framebuffer::Subpass> subpasses =
	{
		Framebuffer::Subpass(PipelineType::Graphics, {},
		{
			Framebuffer::OutputAttachment(swapchainImage->getDefaultView(), false, true, true)
		})
	};

	// Hack for the ImGui render pass creation.
	auto imGuiFramebuffer = createFramebuffer(framebufferSize, std::move(subpasses));
	auto& framebuffer = **GraphicsAPI::framebufferPool.get(imGuiFramebuffer);
	ImGuiData::renderPass = (VkRenderPass)FramebufferExt::getRenderPass(framebuffer);
	Vulkan::device.destroyFramebuffer(vk::Framebuffer((VkFramebuffer)ResourceExt::getInstance(framebuffer)));
	ResourceExt::getInstance(framebuffer) = nullptr;
	FramebufferExt::getRenderPass(framebuffer) = nullptr;
	destroy(imGuiFramebuffer);

	vk::FramebufferCreateInfo framebufferInfo({}, ImGuiData::renderPass,
		1, nullptr, framebufferSize.x, framebufferSize.y, 1);
	ImGuiData::framebuffers.resize(Vulkan::swapchain.getBufferCount());

	for (uint32 i = 0; i < (uint32)ImGuiData::framebuffers.size(); i++)
	{
		auto colorImage = GraphicsAPI::imagePool.get(Vulkan::swapchain.getBuffers()[i].colorImage);
		auto& imageView = **GraphicsAPI::imageViewPool.get(colorImage->getDefaultView());
		framebufferInfo.pAttachments = (vk::ImageView*)&ResourceExt::getInstance(imageView);
		ImGuiData::framebuffers[i] = Vulkan::device.createFramebuffer(framebufferInfo);
	}

	auto window = (GLFWwindow*)GraphicsAPI::window;
	ImGui_ImplGlfw_InitForVulkan(window, true);
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = Vulkan::instance;
	init_info.PhysicalDevice = Vulkan::physicalDevice;
	init_info.Device = Vulkan::device;
	init_info.QueueFamily = Vulkan::graphicsQueueFamilyIndex;
	init_info.Queue = Vulkan::frameQueue;
	init_info.DescriptorPool = Vulkan::descriptorPool;
	init_info.RenderPass = ImGuiData::renderPass;
	init_info.MinImageCount = 2;
	init_info.ImageCount = (uint32)ImGuiData::framebuffers.size();
	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	init_info.PipelineCache = Vulkan::pipelineCache;
	init_info.Subpass = 0;
	init_info.UseDynamicRendering = false; // TODO: use it instead of render pass hack.
	init_info.Allocator = nullptr;
	init_info.CheckVkResultFn = imGuiCheckVkResult;
	init_info.MinAllocationSize = 1024 * 1024;
	ImGui_ImplVulkan_Init(&init_info);

	auto pixelRatioXY = (float2)framebufferSize / windowSize;
	auto pixelRatio = std::max(pixelRatioXY.x, pixelRatioXY.y);
	auto contentScaleX = 0.0f, contentScaleY = 0.0f;
	glfwGetWindowContentScale(window, &contentScaleX, &contentScaleY);
	auto contentScale = std::max(contentScaleX, contentScaleY);

	auto& io = ImGui::GetIO();
	const auto fontPath = "fonts/dejavu-bold.ttf";
	const auto fontSize = 14.0f * contentScale;

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
}
void GraphicsSystem::terminateImGui()
{
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	for (auto framebuffer : ImGuiData::framebuffers)
		Vulkan::device.destroyFramebuffer(framebuffer);
	Vulkan::device.destroyRenderPass(ImGuiData::renderPass);
	ImGui::DestroyContext();
}
void GraphicsSystem::recreateImGui()
{
	for (auto framebuffer : ImGuiData::framebuffers)
		Vulkan::device.destroyFramebuffer(framebuffer);

	vk::FramebufferCreateInfo framebufferInfo({}, ImGuiData::renderPass,
		1, nullptr, framebufferSize.x, framebufferSize.y, 1);
	ImGuiData::framebuffers.resize(Vulkan::swapchain.getBufferCount());

	for (uint32 i = 0; i < (uint32)ImGuiData::framebuffers.size(); i++)
	{
		auto colorImage = GraphicsAPI::imagePool.get(Vulkan::swapchain.getBuffers()[i].colorImage);
		auto& imageView = **GraphicsAPI::imageViewPool.get(colorImage->getDefaultView());
		framebufferInfo.pAttachments = (vk::ImageView*)&ResourceExt::getInstance(imageView);
		ImGuiData::framebuffers[i] = Vulkan::device.createFramebuffer(framebufferInfo);
	}
}
#endif

static ID<ImageView> createDepthStencilBuffer(uint2 size, Image::Format format)
{
	auto depthImage = GraphicsSystem::Instance::get()->createImage(format, 
		Image::Bind::TransferDst | Image::Bind::DepthStencilAttachment | Image::Bind::Sampled |
		Image::Bind::Fullscreen, { { nullptr } }, size, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(depthImage, "image.depthBuffer");
	auto imageView = GraphicsAPI::imagePool.get(depthImage);
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
	Vulkan::initialize(appInfoSystem->getName(), appInfoSystem->getAppDataName(), appInfoSystem->getVersion(),
		windowSize, isFullscreen, useVsync, useTripleBuffering, asyncRecording);

	int sizeX = 0, sizeY = 0;
	auto window = (GLFWwindow*)GraphicsAPI::window;
	glfwGetFramebufferSize(window, &sizeX, &sizeY);
	framebufferSize = uint2(sizeX, sizeY);
	glfwGetWindowSize(window, &sizeX, &sizeY);
	this->windowSize = uint2(sizeX, sizeY);

	if (depthStencilFormat != Image::Format::Undefined)
		depthStencilBuffer = createDepthStencilBuffer(framebufferSize, depthStencilFormat);

	const auto& swapchainBuffer = Vulkan::swapchain.getCurrentBuffer();
	auto swapchainImage = GraphicsAPI::imagePool.get(swapchainBuffer.colorImage);
	swapchainFramebuffer = GraphicsAPI::framebufferPool.create(
		framebufferSize, swapchainImage->getDefaultView(), depthStencilBuffer);
	SET_RESOURCE_DEBUG_NAME(swapchainFramebuffer, "framebuffer.swapchain");

	auto swapchainBufferCount = Vulkan::swapchain.getBufferCount();
	cameraConstantsBuffers.resize(swapchainBufferCount);

	for (uint32 i = 0; i < swapchainBufferCount; i++)
	{
		auto constantsBuffer = createBuffer(Buffer::Bind::Uniform,
			Buffer::Access::SequentialWrite, sizeof(CameraConstants),
			Buffer::Usage::Auto, Buffer::Strategy::Size);
		SET_RESOURCE_DEBUG_NAME(constantsBuffer,
			"buffer.uniform.cameraConstants" + to_string(i));
		cameraConstantsBuffers[i].push_back(constantsBuffer);
	}
}
GraphicsSystem::~GraphicsSystem()
{
	if (Manager::Instance::get()->isRunning())
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
	}

	#if GARDEN_EDITOR
	terminateImGui();
	#endif

	Vulkan::terminate();
	unsetSingleton();
}

//**********************************************************************************************************************
static string getDeviceDriverVersion()
{
	auto version = Vulkan::deviceProperties.properties.driverVersion;
	if (Vulkan::deviceProperties.properties.vendorID == 0x10DE) // Nvidia
	{
		return to_string((version >> 22u) & 0x3FFu) + "." + to_string((version >> 14u) & 0x0FFu) + "." +
			to_string((version >> 6u) & 0x0ff) + "." + to_string(version & 0x003Fu);
	}

	#if GARDEN_OS_WINDOWS
	if (Vulkan::deviceProperties.properties.vendorID == 0x8086) // Intel
		return to_string(version >> 14u) + "." + to_string(version & 0x3FFFu);
	#endif

	if (Vulkan::deviceProperties.properties.vendorID == 0x14E4) // Broadcom
		return to_string(version / 10000) + "." + to_string((version % 10000) / 100);

	if (Vulkan::deviceProperties.properties.vendorID == 0x1010) // ImgTec
	{
		if (version > 500000000)
			return "0.0." + to_string(version);
		else
			return to_string(version);
	}

	return to_string(VK_API_VERSION_MAJOR(version)) + "." + to_string(VK_API_VERSION_MINOR(version)) + "." +
		to_string(VK_API_VERSION_PATCH(version)) + "." + to_string(VK_API_VERSION_VARIANT(version));
}

void GraphicsSystem::preInit()
{
	ECSM_SUBSCRIBE_TO_EVENT("Input", GraphicsSystem::input);
	
	auto threadSystem = ThreadSystem::Instance::tryGet();
	if (threadSystem)
		Vulkan::swapchain.setThreadPool(threadSystem->getForegroundPool());

	GARDEN_LOG_INFO("GPU: " + string(Vulkan::deviceProperties.properties.deviceName.data()));
	auto apiVersion = Vulkan::deviceProperties.properties.apiVersion;
	GARDEN_LOG_INFO("Device Vulkan API: " + to_string(VK_API_VERSION_MAJOR(apiVersion)) + "." +
		to_string(VK_API_VERSION_MINOR(apiVersion)) + "." + to_string(VK_API_VERSION_PATCH(apiVersion)));
	GARDEN_LOG_INFO("Driver version: " + getDeviceDriverVersion());
	GARDEN_LOG_INFO("Framebuffer size: " + to_string(framebufferSize.x) + "x" + to_string(framebufferSize.y));

	if (Vulkan::isCacheLoaded)
		GARDEN_LOG_INFO("Loaded existing pipeline cache.");
	else
		GARDEN_LOG_INFO("Created a new pipeline cache.");

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
	Vulkan::device.waitIdle();

	if (Manager::Instance::get()->isRunning())
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

//**********************************************************************************************************************
static void updateWindowInput(uint2& framebufferSize, uint2& windowSize, bool& isFramebufferSizeValid)
{
	int sizeX = 0, sizeY = 0;
	auto window = (GLFWwindow*)GraphicsAPI::window;
	glfwGetFramebufferSize(window, &sizeX, &sizeY);

	isFramebufferSizeValid = sizeX != 0 && sizeY != 0;
	if (isFramebufferSizeValid)
		framebufferSize = uint2(sizeX, sizeY);

	glfwGetWindowSize(window, &sizeX, &sizeY);

	if (sizeX != 0 && sizeY != 0)
		windowSize = uint2(sizeX, sizeY);
}

//**********************************************************************************************************************
static void recreateCameraBuffers(DescriptorSetBuffers& cameraConstantsBuffers)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	graphicsSystem->destroy(cameraConstantsBuffers);

	auto swapchainBufferCount = Vulkan::swapchain.getBufferCount();
	cameraConstantsBuffers.resize(swapchainBufferCount);

	for (uint32 i = 0; i < swapchainBufferCount; i++)
	{
		auto constantsBuffer = graphicsSystem->createBuffer(Buffer::Bind::Uniform, Buffer::Access::SequentialWrite, 
			sizeof(CameraConstants), Buffer::Usage::Auto, Buffer::Strategy::Size);
		SET_RESOURCE_DEBUG_NAME(constantsBuffer, "buffer.uniform.cameraConstants" + to_string(i));
		cameraConstantsBuffers[i].resize(1); cameraConstantsBuffers[i][0] = constantsBuffer;
	}
}

//**********************************************************************************************************************
static void updateCurrentFramebuffer(ID<Framebuffer> swapchainFramebuffer,
	ID<ImageView> depthStencilBuffer, uint2 framebufferSize)
{
	const auto& swapchainBuffer = Vulkan::swapchain.getCurrentBuffer();
	auto& framebuffer = **GraphicsAPI::framebufferPool.get(swapchainFramebuffer);
	auto colorImage = GraphicsAPI::imagePool.get(swapchainBuffer.colorImage);
	FramebufferExt::getSize(framebuffer) = framebufferSize;
	FramebufferExt::getColorAttachments(framebuffer)[0].imageView = colorImage->getDefaultView();
	FramebufferExt::getDepthStencilAttachment(framebuffer).imageView = depthStencilBuffer;
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
void GraphicsSystem::input()
{
	updateWindowInput(framebufferSize, windowSize, isFramebufferSizeValid);
	beginSleepClock = isFramebufferSizeValid ? 0.0 : OS::getCurrentClock();
}
void GraphicsSystem::update()
{
	SET_CPU_ZONE_SCOPED("Graphics Update");

	SwapchainChanges newSwapchainChanges;
	newSwapchainChanges.framebufferSize = framebufferSize != Vulkan::swapchain.getFramebufferSize();
	newSwapchainChanges.bufferCount = useTripleBuffering != Vulkan::swapchain.useTripleBuffering();
	newSwapchainChanges.vsyncState = useVsync != Vulkan::swapchain.useVsync();

	auto swapchainRecreated = isFramebufferSizeValid && (newSwapchainChanges.framebufferSize ||
		newSwapchainChanges.bufferCount || newSwapchainChanges.vsyncState || suboptimalSwapchain);

	swapchainChanges.framebufferSize |= newSwapchainChanges.framebufferSize;
	swapchainChanges.bufferCount |= newSwapchainChanges.bufferCount;
	swapchainChanges.vsyncState |= newSwapchainChanges.vsyncState;
	
	if (swapchainRecreated)
	{
		SET_CPU_ZONE_SCOPED("Swapchain Recreate");

		Vulkan::swapchain.recreate(framebufferSize, useVsync, useTripleBuffering);
		suboptimalSwapchain = false;

		if (depthStencilBuffer)
		{
			auto depthStencilBufferView = GraphicsAPI::imageViewPool.get(depthStencilBuffer);
			auto depthStencilImageView = GraphicsAPI::imagePool.get(depthStencilBufferView->getImage());
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
			to_string(framebufferSize.y) + ", " + to_string(Vulkan::swapchain.getBufferCount()) + "B)");
	}

	if (Vulkan::swapchain.getBufferCount() != cameraConstantsBuffers.size())
	{
		recreateCameraBuffers(cameraConstantsBuffers);
		swapchainChanges.bufferCount = true;
	}

	if (isFramebufferSizeValid)
	{
		if (!Vulkan::swapchain.acquireNextImage())
		{
			isFramebufferSizeValid = false;
			suboptimalSwapchain = true;
			GARDEN_LOG_WARN("Suboptimal or out of date swapchain.");
		}
	}

	updateCurrentFramebuffer(swapchainFramebuffer, depthStencilBuffer, framebufferSize);
	
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
		auto swapchainBufferIndex = Vulkan::swapchain.getCurrentBufferIndex();
		auto cameraBuffer = GraphicsAPI::bufferPool.get(
			cameraConstantsBuffers[swapchainBufferIndex][0]);
		cameraBuffer->writeData(&currentCameraConstants);
	}

	if (isFramebufferSizeValid)
	{
		#if GARDEN_EDITOR
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		#endif

		if (renderScale != 1.0f) // TODO: make this optional
		{
			startRecording(CommandBufferType::Frame);
			auto depthStencilBufferView = GraphicsAPI::imageViewPool.get(depthStencilBuffer);
			GraphicsAPI::imagePool.get(depthStencilBufferView->getImage())->clear(0.0f, 0x00);
			stopRecording();
		}
	}
}
void GraphicsSystem::present()
{
	SET_CPU_ZONE_SCOPED("Frame Present");

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
		
		GraphicsAPI::graphicsCommandBuffer.submit(frameIndex);
		GraphicsAPI::transferCommandBuffer.submit(frameIndex);
		GraphicsAPI::computeCommandBuffer.submit(frameIndex);
		GraphicsAPI::frameCommandBuffer.submit(frameIndex);
	}
	
	GraphicsAPI::descriptorSetPool.dispose();
	GraphicsAPI::computePipelinePool.dispose();
	GraphicsAPI::graphicsPipelinePool.dispose();
	GraphicsAPI::framebufferPool.dispose();
	GraphicsAPI::imageViewPool.dispose();
	GraphicsAPI::imagePool.dispose();
	GraphicsAPI::bufferPool.dispose();
	Vulkan::updateDestroyBuffer();

	if (isFramebufferSizeValid)
	{
		if (!Vulkan::swapchain.present())
		{
			isFramebufferSizeValid = false;
			suboptimalSwapchain = true;
			GARDEN_LOG_WARN("Suboptimal or out of date swapchain.");
		}
		frameIndex++;
	}
	else
	{
		auto endClock = OS::getCurrentClock();
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
uint2 GraphicsSystem::getScaledFramebufferSize() const noexcept
{
	return max((uint2)(float2(framebufferSize) * renderScale), uint2(1));
}

bool GraphicsSystem::hasDynamicRendering() const noexcept
{
	return Vulkan::hasDynamicRendering;
}
bool GraphicsSystem::hasDescriptorIndexing() const noexcept
{
	return Vulkan::hasDescriptorIndexing;
}

uint32 GraphicsSystem::getSwapchainSize() const noexcept
{
	return (uint32)Vulkan::swapchain.getBufferCount();
}
uint32 GraphicsSystem::getSwapchainIndex() const noexcept
{
	return Vulkan::swapchain.getCurrentBufferIndex();
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
		emptyTexture = GraphicsAPI::imagePool.get(texture)->getDefaultView();
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
		whiteTexture = GraphicsAPI::imagePool.get(texture)->getDefaultView();
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
		greenTexture = GraphicsAPI::imagePool.get(texture)->getDefaultView();
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
		normalMapTexture = GraphicsAPI::imagePool.get(texture)->getDefaultView();
	}
	return normalMapTexture;
}

//**********************************************************************************************************************
void GraphicsSystem::setWindowTitle(const string& title)
{
	glfwSetWindowTitle((GLFWwindow*)GraphicsAPI::window, title.c_str());
}

void GraphicsSystem::setWindowIcon(const vector<string>& paths)
{
	#if GARDEN_OS_WINDOWS
	vector<vector<uint8>> imageData(paths.size());
	vector<GLFWimage> images(paths.size());
	auto resourceSystem = ResourceSystem::Instance::get();

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
 
	glfwSetWindowIcon((GLFWwindow*)GraphicsAPI::window, images.size(), images.data());
	#else
	throw runtime_error("Window icons are not supported on this platform.");
	#endif
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
	auto resource = GraphicsAPI::bufferPool.get(buffer);
	resource->setDebugName(name);
}
void GraphicsSystem::setDebugName(ID<Image> image, const string& name)
{
	auto resource = GraphicsAPI::imagePool.get(image);
	resource->setDebugName(name);
}
void GraphicsSystem::setDebugName(ID<ImageView> imageView, const string& name)
{
	auto resource = GraphicsAPI::imageViewPool.get(imageView);
	resource->setDebugName(name);
}
void GraphicsSystem::setDebugName(ID<Framebuffer> framebuffer, const string& name)
{
	auto resource = GraphicsAPI::framebufferPool.get(framebuffer);
	resource->setDebugName(name);
}
void GraphicsSystem::setDebugName(ID<DescriptorSet> descriptorSet, const string& name)
{
	auto resource = GraphicsAPI::descriptorSetPool.get(descriptorSet);
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

	auto buffer = GraphicsAPI::bufferPool.create(bind, access, usage, strategy, size, 0);
	SET_RESOURCE_DEBUG_NAME(buffer, "buffer" + to_string(*buffer));

	if (data)
	{
		auto bufferView = GraphicsAPI::bufferPool.get(buffer);
		if (bufferView->isMappable())
		{
			bufferView->writeData(data, size);
		}
		else
		{
			auto stagingBuffer = GraphicsAPI::bufferPool.create(
				Buffer::Bind::TransferSrc, Buffer::Access::SequentialWrite,
				Buffer::Usage::Auto, Buffer::Strategy::Speed, size, 0);
			SET_RESOURCE_DEBUG_NAME(stagingBuffer,
				"buffer.staging" + to_string(*stagingBuffer));
			auto stagingBufferView = GraphicsAPI::bufferPool.get(stagingBuffer);
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

			GraphicsAPI::bufferPool.destroy(stagingBuffer);
		}
	}

	return buffer;
}
void GraphicsSystem::destroy(ID<Buffer> buffer)
{
	GraphicsAPI::bufferPool.destroy(buffer);
}
View<Buffer> GraphicsSystem::get(ID<Buffer> buffer) const
{
	return GraphicsAPI::bufferPool.get(buffer);
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

	auto image = GraphicsAPI::imagePool.create(type, format, bind, strategy, size, mipCount, layerCount, 0);
	SET_RESOURCE_DEBUG_NAME(image, "image" + to_string(*image));

	if (stagingCount > 0)
	{
		GARDEN_ASSERT(hasAnyFlag(bind, Image::Bind::TransferDst));
		auto stagingBuffer = GraphicsAPI::bufferPool.create(Buffer::Bind::TransferSrc, 
			Buffer::Access::SequentialWrite, Buffer::Usage::Auto, Buffer::Strategy::Speed, stagingSize, 0);
		SET_RESOURCE_DEBUG_NAME(stagingBuffer, "buffer.imageStaging" + to_string(*stagingBuffer));
		
		ID<Image> targetImage; 
		if (format == dataFormat)
		{
			targetImage = image;
		}
		else
		{
			targetImage = GraphicsAPI::imagePool.create(type, dataFormat, Image::Bind::TransferDst | 
				Image::Bind::TransferSrc, Image::Strategy::Speed, size, mipCount, layerCount, 0);
			SET_RESOURCE_DEBUG_NAME(targetImage, "image.staging" + to_string(*targetImage));
		}

		auto stagingBufferView = GraphicsAPI::bufferPool.get(stagingBuffer);
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
			GraphicsAPI::bufferPool.destroy(stagingBuffer);

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
			GraphicsAPI::imagePool.destroy(targetImage);
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
			GraphicsAPI::bufferPool.destroy(stagingBuffer);
		}
	}

	return image;
}
void GraphicsSystem::destroy(ID<Image> image)
{
	if (image)
	{
		auto imageView = GraphicsAPI::imagePool.get(image);
		GARDEN_ASSERT(!imageView->isSwapchain());

		if (imageView->hasDefaultView() && GraphicsAPI::isRunning)
			GraphicsAPI::imageViewPool.destroy(imageView->getDefaultView());
	}
	
	GraphicsAPI::imagePool.destroy(image);
}
View<Image> GraphicsSystem::get(ID<Image> image) const
{
	return GraphicsAPI::imagePool.get(image);
}

//**********************************************************************************************************************
ID<ImageView> GraphicsSystem::createImageView(ID<Image> image, Image::Type type,
	Image::Format format, uint8 baseMip, uint8 mipCount, uint32 baseLayer, uint32 layerCount)
{
	GARDEN_ASSERT(image);

	auto _image = GraphicsAPI::imagePool.get(image);
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

	auto imageView = GraphicsAPI::imageViewPool.create(false, image,
		type, format, baseMip, mipCount, baseLayer, layerCount);
	SET_RESOURCE_DEBUG_NAME(imageView, "imageView" + to_string(*imageView));
	return imageView;
}
void GraphicsSystem::destroy(ID<ImageView> imageView)
{
	#if GARDEN_DEBUG
	if (imageView)
		GARDEN_ASSERT(!GraphicsAPI::imageViewPool.get(imageView)->isDefault());
	#endif
	GraphicsAPI::imageViewPool.destroy(imageView);
}
View<ImageView> GraphicsSystem::get(ID<ImageView> imageView) const
{
	return GraphicsAPI::imageViewPool.get(imageView);
}

//**********************************************************************************************************************
// TODO: add checks if attachments do not overlaps and repeat.
ID<Framebuffer> GraphicsSystem::createFramebuffer(uint2 size,
	vector<Framebuffer::OutputAttachment>&& colorAttachments, Framebuffer::OutputAttachment depthStencilAttachment)
{
	GARDEN_ASSERT(size > 0u);
	GARDEN_ASSERT(!colorAttachments.empty() || depthStencilAttachment.imageView);

	// TODO: we can use attachments with different sizes, but should we?
	#if GARDEN_DEBUG
	for	(auto colorAttachment : colorAttachments)
	{
		GARDEN_ASSERT(colorAttachment.imageView);
		auto imageView = GraphicsAPI::imageViewPool.get(colorAttachment.imageView);
		GARDEN_ASSERT(isFormatColor(imageView->getFormat()));
		auto image = GraphicsAPI::imagePool.get(imageView->getImage());
		GARDEN_ASSERT(size == calcSizeAtMip((uint2)image->getSize(), imageView->getBaseMip()));
		GARDEN_ASSERT(hasAnyFlag(image->getBind(), Image::Bind::ColorAttachment));
	}
	if (depthStencilAttachment.imageView)
	{
		auto imageView = GraphicsAPI::imageViewPool.get(depthStencilAttachment.imageView);
		GARDEN_ASSERT(isFormatDepthOrStencil(imageView->getFormat()));
		auto image = GraphicsAPI::imagePool.get(imageView->getImage());
		GARDEN_ASSERT(size == calcSizeAtMip((uint2)image->getSize(), imageView->getBaseMip()));
		GARDEN_ASSERT(hasAnyFlag(image->getBind(), Image::Bind::DepthStencilAttachment));
	}
	#endif

	auto framebuffer = GraphicsAPI::framebufferPool.create(size,
		std::move(colorAttachments), depthStencilAttachment);
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer" + to_string(*framebuffer));
	return framebuffer;
}
ID<Framebuffer> GraphicsSystem::createFramebuffer(uint2 size, vector<Framebuffer::Subpass>&& subpasses)
{
	GARDEN_ASSERT(size > 0u);
	GARDEN_ASSERT(!subpasses.empty());

	#if GARDEN_DEBUG
	psize outputAttachmentCount = 0;
	for (const auto& subpass : subpasses)
	{
		for	(auto inputAttachment : subpass.inputAttachments)
		{
			GARDEN_ASSERT(inputAttachment.imageView);
			GARDEN_ASSERT(inputAttachment.shaderStages != ShaderStage::None);
			auto imageView = GraphicsAPI::imageViewPool.get(inputAttachment.imageView);
			auto image = GraphicsAPI::imagePool.get(imageView->getImage());
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
			auto imageView = GraphicsAPI::imageViewPool.get(outputAttachment.imageView);
			auto image = GraphicsAPI::imagePool.get(imageView->getImage());
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

	auto framebuffer = GraphicsAPI::framebufferPool.create(size, std::move(subpasses));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer" + to_string(*framebuffer));
	return framebuffer;
}
void GraphicsSystem::destroy(ID<Framebuffer> framebuffer)
{
	#if GARDEN_DEBUG
	if (framebuffer)
		GARDEN_ASSERT(!GraphicsAPI::framebufferPool.get(framebuffer)->isSwapchainFramebuffer());
	#endif
	GraphicsAPI::framebufferPool.destroy(framebuffer);
}
View<Framebuffer> GraphicsSystem::get(ID<Framebuffer> framebuffer) const
{
	return GraphicsAPI::framebufferPool.get(framebuffer);
}

//**********************************************************************************************************************
void GraphicsSystem::destroy(ID<GraphicsPipeline> graphicsPipeline)
{
	GraphicsAPI::graphicsPipelinePool.destroy(graphicsPipeline);
}
View<GraphicsPipeline> GraphicsSystem::get(ID<GraphicsPipeline> graphicsPipeline) const
{
	return GraphicsAPI::graphicsPipelinePool.get(graphicsPipeline);
}

void GraphicsSystem::destroy(ID<ComputePipeline> computePipeline)
{
	GraphicsAPI::computePipelinePool.destroy(computePipeline);
}
View<ComputePipeline> GraphicsSystem::get(ID<ComputePipeline> computePipeline) const
{
	return GraphicsAPI::computePipelinePool.get(computePipeline);
}

//**********************************************************************************************************************
ID<DescriptorSet> GraphicsSystem::createDescriptorSet(ID<GraphicsPipeline> graphicsPipeline,
	map<string, DescriptorSet::Uniform>&& uniforms, uint8 index)
{
	GARDEN_ASSERT(graphicsPipeline);
	GARDEN_ASSERT(!uniforms.empty());

	#if GARDEN_DEBUG
	auto pipelineView = GraphicsAPI::graphicsPipelinePool.get(graphicsPipeline);
	GARDEN_ASSERT(ResourceExt::getInstance(**pipelineView)); // is ready
	GARDEN_ASSERT(index < PipelineExt::getDescriptorSetLayouts(**pipelineView).size());
	#endif

	auto descriptorSet = GraphicsAPI::descriptorSetPool.create(
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
	auto pipelineView = GraphicsAPI::computePipelinePool.get(computePipeline);
	GARDEN_ASSERT(ResourceExt::getInstance(**pipelineView)); // is ready
	GARDEN_ASSERT(index < PipelineExt::getDescriptorSetLayouts(**pipelineView).size());
	#endif

	auto descriptorSet = GraphicsAPI::descriptorSetPool.create(
		ID<Pipeline>(computePipeline), PipelineType::Compute, std::move(uniforms), index);
	SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet" + to_string(*descriptorSet));
	return descriptorSet;
}
void GraphicsSystem::destroy(ID<DescriptorSet> descriptorSet)
{
	GraphicsAPI::descriptorSetPool.destroy(descriptorSet);
}
View<DescriptorSet> GraphicsSystem::get(ID<DescriptorSet> descriptorSet) const
{
	return GraphicsAPI::descriptorSetPool.get(descriptorSet);
}

//**********************************************************************************************************************
bool GraphicsSystem::isRecording() const noexcept
{
	return GraphicsAPI::currentCommandBuffer;
}
void GraphicsSystem::startRecording(CommandBufferType commandBufferType)
{
	#if GARDEN_DEBUG
	if (GraphicsAPI::currentCommandBuffer)
		throw runtime_error("Already recording.");
	#endif
	
	switch (commandBufferType)
	{
	case CommandBufferType::Frame:
		GraphicsAPI::currentCommandBuffer = &GraphicsAPI::frameCommandBuffer; break;
	case CommandBufferType::Graphics:
		GraphicsAPI::currentCommandBuffer = &GraphicsAPI::graphicsCommandBuffer; break;
	case CommandBufferType::TransferOnly:
		GraphicsAPI::currentCommandBuffer = &GraphicsAPI::transferCommandBuffer; break;
	case CommandBufferType::ComputeOnly:
		GraphicsAPI::currentCommandBuffer = &GraphicsAPI::computeCommandBuffer; break;
	default: abort();
	}
}
void GraphicsSystem::stopRecording()
{
	#if GARDEN_DEBUG
	if (!GraphicsAPI::currentCommandBuffer)
		throw runtime_error("Not recording.");
	#endif
	GraphicsAPI::currentCommandBuffer = nullptr;
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

	auto pipelineView = GraphicsAPI::graphicsPipelinePool.get(linePipeline);
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

	auto pipelineView = GraphicsAPI::graphicsPipelinePool.get(aabbPipeline);
	pipelineView->bind();
	pipelineView->setViewportScissor();
	auto pushConstants = pipelineView->getPushConstants<AabbPC>();
	pushConstants->mvp = mvp;
	pushConstants->color = color;
	pipelineView->pushConstants();
	pipelineView->draw({}, 24);
}
#endif