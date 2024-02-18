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
#include "garden/system/thread.hpp"
#include "garden/system/input.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/camera.hpp"
#include "garden/system/app-info.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/graphics/vulkan.hpp"
#include "garden/graphics/imgui-impl.hpp"
#include "garden/graphics/glfw.hpp"
#include "garden/graphics/primitive.hpp"
#include "garden/system/settings.hpp"
#include "garden/system/render/deferred.hpp"
#include "mpio/os.hpp"

#if GARDEN_EDITOR
#include "garden/system/resource.hpp"
#endif

#if GARDEN_DEBUG || GARDEN_EDITOR
#define SET_THIS_RESOURCE_DEBUG_NAME(resource, name) setDebugName(resource, name)
#else
#define SET_THIS_RESOURCE_DEBUG_NAME(resource, name)
#endif

using namespace mpio;
using namespace garden;
using namespace garden::graphics::primitive;

#if GARDEN_EDITOR
//**********************************************************************************************************************
namespace
{
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

	auto pipelineCacheInfo = vk::PipelineCacheCreateInfo();
	ImGuiData::pipelineCache = Vulkan::device.createPipelineCache(pipelineCacheInfo);

	auto& swapchainBuffer = Vulkan::swapchain.getCurrentBuffer();
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
	auto& framebuffer = **get(imGuiFramebuffer);
	ImGuiData::renderPass = (VkRenderPass)FramebufferExt::getRenderPass(framebuffer);
	Vulkan::device.destroyFramebuffer(vk::Framebuffer((VkFramebuffer)ResourceExt::getInstance(framebuffer)));
	ResourceExt::getInstance(framebuffer) = nullptr;
	FramebufferExt::getRenderPass(framebuffer) = nullptr;
	destroy(imGuiFramebuffer);

	vk::FramebufferCreateInfo framebufferInfo({}, ImGuiData::renderPass,
		1, nullptr, (uint32)framebufferSize.x, (uint32)framebufferSize.y, 1);
	ImGuiData::framebuffers.resize(Vulkan::swapchain.getBufferCount());

	for (uint32 i = 0; i < (uint32)ImGuiData::framebuffers.size(); i++)
	{
		auto colorImage = get(Vulkan::swapchain.getBuffers()[i].colorImage);
		auto& imageView = **get(colorImage->getDefaultView());
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
	init_info.PipelineCache = ImGuiData::pipelineCache;
	init_info.DescriptorPool = Vulkan::descriptorPool;
	init_info.Subpass = 0;
	init_info.MinImageCount = 2;
	init_info.ImageCount = (uint32)ImGuiData::framebuffers.size();
	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	init_info.Allocator = nullptr;
	init_info.CheckVkResultFn = imGuiCheckVkResult; 
	// TODO: use init_info.UseDynamicRendering instead of render pass hack.
	ImGui_ImplVulkan_Init(&init_info, ImGuiData::renderPass);

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
	auto& packReader = ResourceSystem::getInstance()->getPackReader();
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
	Vulkan::device.destroyPipelineCache(ImGuiData::pipelineCache);
    ImGui::DestroyContext();
}
void GraphicsSystem::recreateImGui()
{
	for (auto framebuffer : ImGuiData::framebuffers)
		Vulkan::device.destroyFramebuffer(framebuffer);

	vk::FramebufferCreateInfo framebufferInfo({}, ImGuiData::renderPass,
		1, nullptr, (uint32)framebufferSize.x, (uint32)framebufferSize.y, 1);
	ImGuiData::framebuffers.resize(Vulkan::swapchain.getBufferCount());

	for (uint32 i = 0; i < (uint32)ImGuiData::framebuffers.size(); i++)
	{
		auto colorImage = get(Vulkan::swapchain.getBuffers()[i].colorImage);
		auto& imageView = **get(colorImage->getDefaultView());
		framebufferInfo.pAttachments = (vk::ImageView*)&ResourceExt::getInstance(imageView);
		ImGuiData::framebuffers[i] = Vulkan::device.createFramebuffer(framebufferInfo);
	}
}
#endif

//**********************************************************************************************************************
GraphicsSystem::GraphicsSystem(Manager* manager, int2 windowSize, bool isFullscreen,
	bool useVsync, bool useTripleBuffering, bool useThreading) : System(manager)
{
	this->useVsync = useVsync;
	this->useTripleBuffering = useTripleBuffering;
	this->useThreading = useThreading;

	manager->registerEventAfter("Render", "Update");
	manager->registerEventAfter("Present", "Render");
	manager->registerEvent("SwapchainRecreate");
	SUBSCRIBE_TO_EVENT("PreInit", GraphicsSystem::preInit);
	SUBSCRIBE_TO_EVENT("PreDeinit", GraphicsSystem::preDeinit);
	SUBSCRIBE_TO_EVENT("Update", GraphicsSystem::update);
	SUBSCRIBE_TO_EVENT("Present", GraphicsSystem::present);

	auto appInfoSystem = manager->get<AppInfoSystem>();
	Vulkan::initialize(appInfoSystem->getName(), appInfoSystem->getAppDataName(), appInfoSystem->getVersion(),
		windowSize, isFullscreen, useVsync, useTripleBuffering, useThreading);

	auto window = (GLFWwindow*)GraphicsAPI::window;
	glfwGetFramebufferSize(window, &framebufferSize.x, &framebufferSize.y);
	glfwGetWindowSize(window, &this->windowSize.x, &this->windowSize.y);

	auto& swapchainBuffer = Vulkan::swapchain.getCurrentBuffer();
	auto swapchainImage = GraphicsAPI::imagePool.get(swapchainBuffer.colorImage);
	swapchainFramebuffer = GraphicsAPI::framebufferPool.create(
		framebufferSize, swapchainImage->getDefaultView());
	SET_THIS_RESOURCE_DEBUG_NAME(swapchainFramebuffer, "framebuffer.swapchain");

	auto swapchainBufferCount = Vulkan::swapchain.getBufferCount();
	cameraConstantsBuffers.resize(swapchainBufferCount);

	for (uint32 i = 0; i < swapchainBufferCount; i++)
	{
		auto constantsBuffer = createBuffer(Buffer::Bind::Uniform,
			Buffer::Access::SequentialWrite, sizeof(CameraConstants),
			Buffer::Usage::Auto, Buffer::Strategy::Size);
		SET_THIS_RESOURCE_DEBUG_NAME(constantsBuffer,
			"buffer.uniform.cameraConstants" + to_string(i));
		cameraConstantsBuffers[i].push_back(constantsBuffer);
	}
}
GraphicsSystem::~GraphicsSystem()
{
	auto manager = getManager();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("PreInit", GraphicsSystem::preInit);
		UNSUBSCRIBE_FROM_EVENT("PreDeinit", GraphicsSystem::preDeinit);
		UNSUBSCRIBE_FROM_EVENT("Update", GraphicsSystem::update);
		UNSUBSCRIBE_FROM_EVENT("Present", GraphicsSystem::present);
		manager->unregisterEvent("Render");
		manager->unregisterEvent("Present");
		manager->unregisterEvent("SwapchainRecreate");
	}

	#if GARDEN_EDITOR
	terminateImGui();
	#endif
	Vulkan::terminate();
}

//**********************************************************************************************************************
void GraphicsSystem::preInit()
{
	auto manager = getManager();
	GARDEN_ASSERT(manager->has<InputSystem>());
	SUBSCRIBE_TO_EVENT("Input", GraphicsSystem::input);

	auto threadSystem = manager->tryGet<ThreadSystem>();
	if (threadSystem)
		Vulkan::swapchain.setThreadPool(threadSystem->getForegroundPool());

	auto logSystem = manager->tryGet<LogSystem>();
	if (logSystem)
	{
		logSystem->info("GPU: " + string(
			Vulkan::deviceProperties.properties.deviceName.data()));
		auto apiVersion = Vulkan::deviceProperties.properties.apiVersion;
		logSystem->info("Vulkan API: " +
			to_string(VK_API_VERSION_MAJOR(apiVersion)) + "." +
			to_string(VK_API_VERSION_MINOR(apiVersion)) + "." +
			to_string(VK_API_VERSION_PATCH(apiVersion)));
	}

	auto settingsSystem = manager->tryGet<SettingsSystem>();
	if (settingsSystem)
		settingsSystem->getBool("useVsync", useVsync);

	#if GARDEN_EDITOR
	initializeImGui();
	#endif
}
void GraphicsSystem::preDeinit()
{
	Vulkan::device.waitIdle();

	auto manager = getManager();
	if (manager->isRunning())
		UNSUBSCRIBE_FROM_EVENT("Input", GraphicsSystem::input);
}

//**********************************************************************************************************************
static float4x4 calcView(const TransformComponent* transform)
{
	return rotate(normalize(transform->rotation)) * translate(
		scale(transform->scale), -transform->position);
}
static float4x4 calcRelativeView(Manager* manager, const TransformComponent* transform)
{
	auto view = calcView(transform);
	auto nextParent = transform->getParent();

	while (nextParent)
	{
		auto nextTransform = manager->get<TransformComponent>(nextParent);
		auto parentModel = ::calcModel(nextTransform->position,
			nextTransform->rotation, nextTransform->scale);
		view = parentModel * view;
		nextParent = nextTransform->getParent();
	}

	return view;
}

//**********************************************************************************************************************
static void updateWindowInput(int2& framebufferSize, int2& windowSize, bool& isFramebufferSizeValid)
{
	auto window = (GLFWwindow*)GraphicsAPI::window;
	int framebufferWidth = 0, framebufferHeight = 0;
	glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
	int windowWidth = 0, windowHeight = 0;
	glfwGetWindowSize(window, &windowWidth, &windowHeight);

	isFramebufferSizeValid = framebufferWidth != 0 && framebufferHeight != 0;
	if (isFramebufferSizeValid)
		framebufferSize = int2(framebufferWidth, framebufferHeight);
	if (windowWidth != 0 && windowHeight != 0)
		windowSize = int2(windowWidth, windowHeight);
}

//**********************************************************************************************************************
static void recreateCameraBuffers(GraphicsSystem* graphicsSystem,
	vector<vector<ID<Buffer>>>& cameraConstantsBuffers)
{
	for (uint32 i = 0; i < (uint32)cameraConstantsBuffers.size(); i++)
		graphicsSystem->destroy(cameraConstantsBuffers[i][0]);
	cameraConstantsBuffers.clear();

	auto swapchainBufferCount = Vulkan::swapchain.getBufferCount();
	cameraConstantsBuffers.resize(swapchainBufferCount);

	for (uint32 i = 0; i < swapchainBufferCount; i++)
	{
		auto constantsBuffer = graphicsSystem->createBuffer(Buffer::Bind::Uniform,
			Buffer::Access::SequentialWrite, sizeof(CameraConstants),
			Buffer::Usage::Auto, Buffer::Strategy::Size);
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, constantsBuffer,
			"buffer.uniform.cameraConstants" + to_string(i));
		cameraConstantsBuffers[i].push_back(constantsBuffer);
	}
}

//**********************************************************************************************************************
static void updateCurrentFrambuffer(ID<Framebuffer> swapchainFramebuffer, int2 framebufferSize)
{
	auto& swapchainBuffer = Vulkan::swapchain.getCurrentBuffer();
	auto& framebuffer = **GraphicsAPI::framebufferPool.get(swapchainFramebuffer);
	auto colorImage = GraphicsAPI::imagePool.get(swapchainBuffer.colorImage);
	FramebufferExt::getColorAttachments(framebuffer)[0].imageView = colorImage->getDefaultView();
	FramebufferExt::getSize(framebuffer) = framebufferSize;
}

//**********************************************************************************************************************
static void prepareCameraConstants(Manager* manager, ID<Entity> camera,
	ID<Entity> directionalLight, int2 framebufferSize, CameraConstants& cameraConstants)
{
	auto transformComponent = manager->tryGet<TransformComponent>(camera);
	if (transformComponent)
	{
		cameraConstants.view = calcRelativeView(manager, *transformComponent);
		setTranslation(cameraConstants.view, float3(0.0f));
		cameraConstants.cameraPos = float4(transformComponent->position, 0.0f);
	}
	else
	{
		cameraConstants.view = float4x4::identity;
		cameraConstants.cameraPos = 0.0f;
	}

	auto cameraComponent = manager->tryGet<CameraComponent>(camera);
	if (cameraComponent)
	{
		cameraComponent->p.perspective.aspectRatio = (float)framebufferSize.x / (float)framebufferSize.y;
		cameraConstants.projection = cameraComponent->calcProjection();
		cameraConstants.nearPlane = cameraComponent->p.perspective.nearPlane;
	}
	else
	{
		cameraConstants.projection = float4x4::identity;
		cameraConstants.nearPlane = defaultHmdDepth;
	}

	// TODO:
	/*
	auto deferredSystem = manager->tryGet<DeferredRenderSystem>();
	if (deferredSystem)
	{
		auto frameSize = deferredSystem->getFramebufferSize();
		cameraConstants.frameSize = frameSize;
		cameraConstants.frameSizeInv = 1.0f / (float2)frameSize;
	}
	else
	{
		cameraConstants.frameSize = cameraConstants.frameSizeInv = 1.0f;
	}
	*/

	cameraConstants.viewProj = cameraConstants.projection * cameraConstants.view;
	cameraConstants.viewInverse = inverse(cameraConstants.view);
	cameraConstants.projInverse = inverse(cameraConstants.projection);
	cameraConstants.viewProjInv = inverse(cameraConstants.viewProj);
	cameraConstants.frameSizeInv2 = cameraConstants.frameSizeInv * 2.0f;
	auto viewDir = cameraConstants.view * float4(float3::front, 1.0f);
	cameraConstants.viewDir = float4(normalize((float3)viewDir), 0.0f);

	if (directionalLight)
	{
		auto lightTransform = manager->tryGet<TransformComponent>(directionalLight);
		if (lightTransform)
		{
			auto lightDir = lightTransform->rotation * float3::front;
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
}

//**********************************************************************************************************************
void GraphicsSystem::input()
{
	updateWindowInput(framebufferSize, windowSize, isFramebufferSizeValid);
	beginSleepClock = isFramebufferSizeValid ? 0.0 : OS::getCurrentClock();
}
void GraphicsSystem::update()
{

	swapchainChanges.framebufferSize = framebufferSize != Vulkan::swapchain.getFramebufferSize();
	swapchainChanges.bufferCount = useTripleBuffering != Vulkan::swapchain.isUseTripleBuffering();
	swapchainChanges.vsyncState = useVsync != Vulkan::swapchain.isUseVsync();

	if (forceRecreateSwapchain)
	{
		swapchainChanges.framebufferSize |= this->swapchainChanges.framebufferSize;
		swapchainChanges.bufferCount |= this->swapchainChanges.bufferCount;
		swapchainChanges.vsyncState |= this->swapchainChanges.vsyncState;
		forceRecreateSwapchain = false;
	}

	auto swapchainRecreated = isFramebufferSizeValid && (swapchainChanges.framebufferSize ||
		swapchainChanges.bufferCount || swapchainChanges.vsyncState);
	
	auto manager = getManager();
	auto logSystem = manager->tryGet<LogSystem>();

	if (swapchainRecreated)
	{
		Vulkan::swapchain.recreate(framebufferSize, useVsync, useTripleBuffering);

		if (Vulkan::swapchain.getBufferCount() != cameraConstantsBuffers.size())
		{
			recreateCameraBuffers(this, cameraConstantsBuffers);
			swapchainChanges.bufferCount = true;
		}

		#if GARDEN_EDITOR
		recreateImGui();
		#endif

		if (logSystem)
		{
			logSystem->info("Recreated swapchain. (" +
				to_string(framebufferSize.x) + "x" + to_string(framebufferSize.y) + ")");
		}
	}

	if (isFramebufferSizeValid)
	{
		if (!Vulkan::swapchain.acquireNextImage())
		{
			if (logSystem)
				logSystem->warn("Out fo date or suboptimal swapchain.");
		}
	}

	updateCurrentFrambuffer(swapchainFramebuffer, framebufferSize);
	
	if (swapchainRecreated)
		manager->runEvent("SwapchainRecreate");

	if (camera)
	{
		prepareCameraConstants(manager, camera, directionalLight,
			framebufferSize, currentCameraConstants);
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
	}
}
void GraphicsSystem::present()
{
	if (isFramebufferSizeValid)
	{
		#if GARDEN_EDITOR
		startRecording(CommandBufferType::Frame);
		{
			INSERT_GPU_DEBUG_LABEL("ImGui", Color::transparent);
			ImGui::Render();
		}
		stopRecording();
		#endif

		GraphicsAPI::graphicsCommandBuffer.submit();
		GraphicsAPI::transferCommandBuffer.submit();
		GraphicsAPI::computeCommandBuffer.submit();
		GraphicsAPI::frameCommandBuffer.submit();
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
			auto logSystem = getManager()->tryGet<LogSystem>();
			if (logSystem)
				logSystem->warn("Out fo date or suboptimal swapchain.");
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
}

//**********************************************************************************************************************
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
ID<Buffer> GraphicsSystem::getFullCubeVertices()
{
	if (!fullCubeVertices)
	{
		fullCubeVertices = createBuffer(Buffer::Bind::Vertex |
			Buffer::Bind::TransferDst, Buffer::Access::None, fullCubeVert);
		SET_THIS_RESOURCE_DEBUG_NAME(fullCubeVertices, "buffer.vertex.fullCube");
	}

	return fullCubeVertices;
}

ID<ImageView> GraphicsSystem::getEmptyTexture()
{
	if (!emptyTexture)
	{
		const Color data[1] = { Color::transparent };
		auto texture = createImage(Image::Format::UnormR8G8B8A8,
			Image::Bind::Sampled | Image::Bind::TransferDst, { { data } }, int2(1));
		SET_THIS_RESOURCE_DEBUG_NAME(texture, "image.emptyTexture");
		emptyTexture = get(texture)->getDefaultView();
	}

	return emptyTexture;
}
ID<ImageView> GraphicsSystem::getWhiteTexture()
{
	if (!whiteTexture)
	{
		const Color data[1] = { Color::white };
		auto texture = createImage(Image::Format::UnormR8G8B8A8,
			Image::Bind::Sampled | Image::Bind::TransferDst, { { data } }, int2(1));
		SET_THIS_RESOURCE_DEBUG_NAME(texture, "image.whiteTexture");
		whiteTexture = get(texture)->getDefaultView();
	}

	return whiteTexture;
}
ID<ImageView> GraphicsSystem::getGreenTexture()
{
	if (!greenTexture)
	{
		const Color data[1] = { Color::green };
		auto texture = createImage(Image::Format::UnormR8G8B8A8,
			Image::Bind::Sampled | Image::Bind::TransferDst, { { data } }, int2(1));
		SET_THIS_RESOURCE_DEBUG_NAME(texture, "image.greenTexture");
		greenTexture = get(texture)->getDefaultView();
	}

	return greenTexture;
}
ID<ImageView> GraphicsSystem::getNormalMapTexture()
{
	if (!normalMapTexture)
	{
		const Color data[1] = { Color(127, 127, 255, 255) };
		auto texture = createImage(Image::Format::UnormR8G8B8A8,
			Image::Bind::Sampled | Image::Bind::TransferDst, { { data } }, int2(1));
		SET_THIS_RESOURCE_DEBUG_NAME(texture, "image.normalMapTexture");
		normalMapTexture = get(texture)->getDefaultView();
	}

	return normalMapTexture;
}

void GraphicsSystem::setWindowTitle(const string& title)
{
	glfwSetWindowTitle((GLFWwindow*)GraphicsAPI::window, title.c_str());
}

//**********************************************************************************************************************
void GraphicsSystem::recreateSwapchain(const SwapchainChanges& changes)
{
	GARDEN_ASSERT(changes.framebufferSize || changes.bufferCount || changes.vsyncState);
	swapchainChanges = changes;
	forceRecreateSwapchain = true;
}

#if GARDEN_DEBUG || GARDEN_EDITOR
//**********************************************************************************************************************
void GraphicsSystem::setDebugName(ID<Buffer> instance, const string& name)
{
	auto resource = GraphicsAPI::bufferPool.get(instance);
	resource->setDebugName(name);
}
void GraphicsSystem::setDebugName(ID<Image> instance, const string& name)
{
	auto resource = GraphicsAPI::imagePool.get(instance);
	resource->setDebugName(name);
}
void GraphicsSystem::setDebugName(ID<ImageView> instance, const string& name)
{
	auto resource = GraphicsAPI::imageViewPool.get(instance);
	resource->setDebugName(name);
}
void GraphicsSystem::setDebugName(ID<Framebuffer> instance, const string& name)
{
	auto resource = GraphicsAPI::framebufferPool.get(instance);
	resource->setDebugName(name);
}
void GraphicsSystem::setDebugName(ID<DescriptorSet> instance, const string& name)
{
	auto resource = GraphicsAPI::descriptorSetPool.get(instance);
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
	SET_THIS_RESOURCE_DEBUG_NAME(buffer, "buffer" + to_string(*buffer));

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
			SET_THIS_RESOURCE_DEBUG_NAME(stagingBuffer,
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
void GraphicsSystem::destroy(ID<Buffer> instance)
{
	GraphicsAPI::bufferPool.destroy(instance);
}
View<Buffer> GraphicsSystem::get(ID<Buffer> instance) const
{
	return GraphicsAPI::bufferPool.get(instance);
}

//**********************************************************************************************************************
ID<Image> GraphicsSystem::createImage(Image::Type type, Image::Format format, Image::Bind bind,
	const Image::Mips& data, const int3& size, Image::Strategy strategy, Image::Format dataFormat)
{
	GARDEN_ASSERT(format != Image::Format::Undefined);
	GARDEN_ASSERT(!data.empty());
	GARDEN_ASSERT(size > 0);

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
		auto& mipData = data[mip];
		auto binarySize = formatBinarySize * mipSize.x * mipSize.y * mipSize.z;

		for (auto layerData : mipData)
		{
			if (!layerData)
				continue;
			stagingSize += binarySize;
			stagingCount++;
		}

		mipSize = max(mipSize / 2, int3(1));
	}

	auto image = GraphicsAPI::imagePool.create(type, format,
		bind, strategy, size, mipCount, layerCount, 0);
	SET_THIS_RESOURCE_DEBUG_NAME(image, "image" + to_string(*image));

	if (stagingCount > 0)
	{
		GARDEN_ASSERT(hasAnyFlag(bind, Image::Bind::TransferDst));
		auto stagingBuffer = GraphicsAPI::bufferPool.create(
			Buffer::Bind::TransferSrc, Buffer::Access::SequentialWrite,
			Buffer::Usage::Auto, Buffer::Strategy::Speed, stagingSize, 0);
		SET_THIS_RESOURCE_DEBUG_NAME(stagingBuffer,
			"buffer.imageStaging" + to_string(*stagingBuffer));
		
		ID<Image> targetImage; 
		if (format == dataFormat)
		{
			targetImage = image;
		}
		else
		{
			targetImage = GraphicsAPI::imagePool.create(type, dataFormat,
				Image::Bind::TransferDst | Image::Bind::TransferSrc,
				Image::Strategy::Speed, size, mipCount, layerCount, 0);
			SET_THIS_RESOURCE_DEBUG_NAME(targetImage,
				"image.staging" + to_string(*targetImage));
		}

		auto stagingBufferView = GraphicsAPI::bufferPool.get(stagingBuffer);
		auto stagingMap = stagingBufferView->getMap();
		vector<Image::CopyBufferRegion> regions(stagingCount);
		mipSize = size;
		uint64 stagingOffset = 0;
		uint32 copyIndex = 0;

		for (uint8 mip = 0; mip < mipCount; mip++)
		{
			auto& mipData = data[mip];
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

			mipSize = max(mipSize / 2, int3(1));
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
				mipSize = max(mipSize / 2, int3(1));
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
void GraphicsSystem::destroy(ID<Image> instance)
{
	if (instance)
		GARDEN_ASSERT(!GraphicsAPI::imagePool.get(instance)->isSwapchain());
	GraphicsAPI::imagePool.destroy(instance);
}
View<Image> GraphicsSystem::get(ID<Image> instance) const
{
	return GraphicsAPI::imagePool.get(instance);
}

//**********************************************************************************************************************
ID<ImageView> GraphicsSystem::createImageView(
	ID<Image> image, Image::Type type, Image::Format format,
	uint8 baseMip, uint8 mipCount, uint32 baseLayer, uint32 layerCount)
{
	GARDEN_ASSERT(image);
	GARDEN_ASSERT(mipCount > 0);
	GARDEN_ASSERT(layerCount > 0);
	
	#if GARDEN_DEBUG
	auto _image = GraphicsAPI::imagePool.get(image);
	GARDEN_ASSERT(ResourceExt::getInstance(**_image));
	GARDEN_ASSERT(mipCount + baseMip <= _image->getMipCount());
	GARDEN_ASSERT(layerCount + baseLayer <= _image->getLayerCount());

	if (type == Image::Type::Texture1DArray || type == Image::Type::Texture2DArray)
	{
		GARDEN_ASSERT(layerCount > 1);
	}
	else
	{
		GARDEN_ASSERT(layerCount == 1);
	}
	#endif

	if (format == Image::Format::Undefined)
	{
		auto imageView = GraphicsAPI::imagePool.get(image);
		format = imageView->getFormat();
	}

	auto imageView = GraphicsAPI::imageViewPool.create(false, image,
		type, format, baseMip, mipCount, baseLayer, layerCount);
	SET_THIS_RESOURCE_DEBUG_NAME(imageView, "imageView" + to_string(*imageView));
	return imageView;
}
void GraphicsSystem::destroy(ID<ImageView> instance)
{
	#if GARDEN_DEBUG
	if (instance)
		GARDEN_ASSERT(!GraphicsAPI::imageViewPool.get(instance)->isDefault());
	#endif
	GraphicsAPI::imageViewPool.destroy(instance);
}
View<ImageView> GraphicsSystem::get(ID<ImageView> instance) const
{
	return GraphicsAPI::imageViewPool.get(instance);
}

//**********************************************************************************************************************
// TODO: add checks if attachments do not overlaps and repeat.
ID<Framebuffer> GraphicsSystem::createFramebuffer(int2 size,
	vector<Framebuffer::OutputAttachment>&& colorAttachments,
	Framebuffer::OutputAttachment depthStencilAttachment)
{
	GARDEN_ASSERT(size > 0);
	GARDEN_ASSERT(!colorAttachments.empty() || depthStencilAttachment.imageView);

	// TODO: we can use attachments with different sizes, but should we?
	#if GARDEN_DEBUG
	for	(auto colorAttachment : colorAttachments)
	{
		GARDEN_ASSERT(colorAttachment.imageView);
		auto imageView = GraphicsAPI::imageViewPool.get(colorAttachment.imageView);
		GARDEN_ASSERT(isFormatColor(imageView->getFormat()));
		auto image = GraphicsAPI::imagePool.get(imageView->getImage());
		GARDEN_ASSERT(size == calcSizeAtMip((int2)image->getSize(), imageView->getBaseMip()));
		GARDEN_ASSERT(hasAnyFlag(image->getBind(), Image::Bind::ColorAttachment));
	}
	if (depthStencilAttachment.imageView)
	{
		auto imageView = GraphicsAPI::imageViewPool.get(depthStencilAttachment.imageView);
		GARDEN_ASSERT(isFormatDepthOrStencil(imageView->getFormat()));
		auto image = GraphicsAPI::imagePool.get(imageView->getImage());
		GARDEN_ASSERT(size == calcSizeAtMip((int2)image->getSize(), imageView->getBaseMip()));
		GARDEN_ASSERT(hasAnyFlag(image->getBind(), Image::Bind::DepthStencilAttachment));
	}
	#endif

	auto framebuffer = GraphicsAPI::framebufferPool.create(size,
		std::move(colorAttachments), depthStencilAttachment);
	SET_THIS_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer" + to_string(*framebuffer));
	return framebuffer;
}
ID<Framebuffer> GraphicsSystem::createFramebuffer(
	int2 size, vector<Framebuffer::Subpass>&& subpasses)
{
	GARDEN_ASSERT(size > 0);
	GARDEN_ASSERT(!subpasses.empty());

	#if GARDEN_DEBUG
	psize outputAttachmentCount = 0;
	for (auto& subpass : subpasses)
	{
		for	(auto inputAttachment : subpass.inputAttachments)
		{
			GARDEN_ASSERT(inputAttachment.imageView);
			GARDEN_ASSERT(inputAttachment.shaderStages != ShaderStage::None);
			auto imageView = GraphicsAPI::imageViewPool.get(inputAttachment.imageView);
			auto image = GraphicsAPI::imagePool.get(imageView->getImage());
			GARDEN_ASSERT(size == calcSizeAtMip((int2)image->getSize(), imageView->getBaseMip()));
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
			GARDEN_ASSERT(size == calcSizeAtMip((int2)image->getSize(), imageView->getBaseMip()));
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
	SET_THIS_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer" + to_string(*framebuffer));
	return framebuffer;
}
void GraphicsSystem::destroy(ID<Framebuffer> instance)
{
	#if GARDEN_DEBUG
	if (instance)
		GARDEN_ASSERT(!GraphicsAPI::framebufferPool.get(instance)->isSwapchainFramebuffer());
	#endif
	GraphicsAPI::framebufferPool.destroy(instance);
}
View<Framebuffer> GraphicsSystem::get(ID<Framebuffer> instance) const
{
	return GraphicsAPI::framebufferPool.get(instance);
}

//**********************************************************************************************************************
void GraphicsSystem::destroy(ID<GraphicsPipeline> instance)
{
	GraphicsAPI::graphicsPipelinePool.destroy(instance);
}
View<GraphicsPipeline> GraphicsSystem::get(ID<GraphicsPipeline> instance) const
{
	return GraphicsAPI::graphicsPipelinePool.get(instance);
}

void GraphicsSystem::destroy(ID<ComputePipeline> instance)
{
	GraphicsAPI::computePipelinePool.destroy(instance);
}
View<ComputePipeline> GraphicsSystem::get(ID<ComputePipeline> instance) const
{
	return GraphicsAPI::computePipelinePool.get(instance);
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
	SET_THIS_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet" + to_string(*descriptorSet));
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
	SET_THIS_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet" + to_string(*descriptorSet));
	return descriptorSet;
}
void GraphicsSystem::destroy(ID<DescriptorSet> instance)
{
	GraphicsAPI::descriptorSetPool.destroy(instance);
}
View<DescriptorSet> GraphicsSystem::get(ID<DescriptorSet> instance) const
{
	return GraphicsAPI::descriptorSetPool.get(instance);
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
void GraphicsSystem::drawAabb(const float4x4& mvp, const float4& color)
{
	// TODO:
	/*
	auto deferredSystem = getManager()->get<DeferredRenderSystem>();
	if (!aabbPipeline)
	{
		aabbPipeline = ResourceSystem::getInstance()->loadGraphicsPipeline(
			"editor/aabb-lines", deferredSystem->getEditorFramebuffer(), false, false);
	}

	auto pipelineView = get(aabbPipeline);
	pipelineView->bind();
	pipelineView->setViewportScissor(float4(float2(0), deferredSystem->getFramebufferSize()));
	auto pushConstants = pipelineView->getPushConstants<AabbPC>();
	pushConstants->mvp = mvp;
	pushConstants->color = color;
	pipelineView->pushConstants();
	pipelineView->draw({}, 24);
	*/
}
#endif