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

#include "garden/system/graphics.hpp"
#include "garden/graphics/framebuffer.hpp"
#include "garden/graphics/sampler.hpp"
#include "garden/system/log.hpp"
#include "garden/system/input.hpp"
#include "garden/system/thread.hpp"
#include "garden/system/camera.hpp"
#include "garden/system/settings.hpp"
#include "garden/system/app-info.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/transform.hpp"
#include "garden/graphics/vulkan/api.hpp"
#include "garden/graphics/glfw.hpp" // Note: Do not move it.
#include "garden/resource/primitive.hpp"
#include "garden/profiler.hpp"

#include "math/matrix/projection.hpp"
#include "math/matrix/transform.hpp"
#include "garden/os.hpp"

using namespace garden;
using namespace garden::primitive;

namespace garden::graphics
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

//**********************************************************************************************************************
GraphicsSystem::GraphicsSystem(uint2 windowSize, bool isFullscreen, bool isDecorated, bool useVsync, 
	bool useTripleBuffering, bool useAsyncRecording, bool _setSingleton) : Singleton(false),
	asyncRecording(useAsyncRecording), useVsync(useVsync), useTripleBuffering(useTripleBuffering)
{
	auto manager = Manager::Instance::get();
	manager->registerEventAfter("Render", "Update");
	manager->registerEventAfter("Present", "Render");
	manager->registerEvent("SwapchainRecreate");

	ECSM_SUBSCRIBE_TO_EVENT("PreInit", GraphicsSystem::preInit);
	ECSM_SUBSCRIBE_TO_EVENT("PreDeinit", GraphicsSystem::preDeinit);
	ECSM_SUBSCRIBE_TO_EVENT("Update", GraphicsSystem::update);

	if (_setSingleton)
		setSingleton();

	auto appInfoSystem = AppInfoSystem::Instance::get();
	auto threadCount = getBestForegroundThreadCount();

	GraphicsAPI::initialize(GraphicsBackend::VulkanAPI, appInfoSystem->getName(), appInfoSystem->getAppDataName(),
		appInfoSystem->getVersion(), windowSize, threadCount, useVsync, useTripleBuffering, isFullscreen, isDecorated);

	auto graphicsAPI = GraphicsAPI::get();
	auto swapchainImage = graphicsAPI->imagePool.get(graphicsAPI->swapchain->getCurrentImage());
	auto swapchainImageView = swapchainImage->getDefaultView();
	auto framebufferSize = (uint2)swapchainImage->getSize();

	swapchainFramebuffer = graphicsAPI->framebufferPool.create(framebufferSize, swapchainImageView);
	SET_RESOURCE_DEBUG_NAME(swapchainFramebuffer, "framebuffer.swapchain");

	setShadowColor(float3::one, 0.25f);
	setSkyColor(float3::one);
	setEmissiveCoeff(100.0f);

	cameraConstantsBuffers.resize(inFlightCount);
	for (uint32 i = 0; i < inFlightCount; i++)
	{
		auto constantsBuffer = createBuffer(Buffer::Usage::Uniform, Buffer::CpuAccess::SequentialWrite, 
			sizeof(CameraConstants), Buffer::Location::Auto, Buffer::Strategy::Size);
		SET_RESOURCE_DEBUG_NAME(constantsBuffer, "buffer.uniform.cameraConstants" + to_string(i));
		cameraConstantsBuffers[i].resize(1); cameraConstantsBuffers[i][0] = constantsBuffer;
	}
}
GraphicsSystem::~GraphicsSystem()
{
	GraphicsAPI::get()->storePipelineCache();

	if (Manager::Instance::get()->isRunning)
	{
		// Note: Constants buffers and other resources will destroyed by terminating graphics API.

		ECSM_UNSUBSCRIBE_FROM_EVENT("PreInit", GraphicsSystem::preInit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreDeinit", GraphicsSystem::preDeinit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Update", GraphicsSystem::update);

		auto manager = Manager::Instance::get();
		manager->unregisterEvent("Render");
		manager->unregisterEvent("Present");
		manager->unregisterEvent("SwapchainRecreate");

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
	GARDEN_LOG_INFO(vulkanAPI->isCacheLoaded ? 
		"Loaded existing pipeline cache." : "Created a new pipeline cache.");
	GARDEN_LOG_INFO("Has ray tracing support: " + to_string(vulkanAPI->features.rayTracing));
}

void GraphicsSystem::preInit()
{
	ECSM_SUBSCRIBE_TO_EVENT("Input", GraphicsSystem::input);
	ECSM_SUBSCRIBE_TO_EVENT("Present", GraphicsSystem::present);

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		logVkGpuInfo();
	else abort();

	auto settingsSystem = SettingsSystem::Instance::tryGet();
	if (settingsSystem)
	{
		settingsSystem->getBool("useVsync", useVsync);
		settingsSystem->getInt("maxFPS", maxFPS);
	}
}
void GraphicsSystem::preDeinit()
{
	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		VulkanAPI::get()->device.waitIdle();
	else abort();

	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Input", GraphicsSystem::input);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Present", GraphicsSystem::present);
	}
}

//**********************************************************************************************************************
static f32x4x4 calcView(const TransformComponent* transform) noexcept
{
	return rotate(normalize(transform->getRotation())) * translate(
		scale(transform->getScale()), -transform->getPosition());
}
static f32x4x4 calcRelativeView(const TransformComponent* transform)
{
	auto view = calcView(transform);
	auto nextParent = transform->getParent();
	auto transformSystem = TransformSystem::Instance::get();

	while (nextParent)
	{
		auto nextTransformView = transformSystem->getComponent(nextParent);
		auto parentModel = calcModel(nextTransformView->getPosition(),
			nextTransformView->getRotation(), nextTransformView->getScale());
		view = parentModel * view;
		nextParent = nextTransformView->getParent();
	}

	return view;
}

static void updateCurrentFramebuffer(ID<Framebuffer> swapchainFramebuffer, uint2 framebufferSize)
{
	auto graphicsAPI = GraphicsAPI::get();
	auto framebufferView = graphicsAPI->framebufferPool.get(swapchainFramebuffer);
	auto swapchainView = graphicsAPI->imagePool.get(graphicsAPI->swapchain->getCurrentImage());
	FramebufferExt::getSize(**framebufferView) = framebufferSize;
	FramebufferExt::getColorAttachments(**framebufferView)[0].imageView = swapchainView->getDefaultView();
}

//**********************************************************************************************************************
static void prepareCameraConstants(ID<Entity> camera, ID<Entity> directionalLight,
	uint2 scaledFramebufferSize, CameraConstants& cameraConstants)
{
	auto manager = Manager::Instance::get();
	auto inputSystem = InputSystem::Instance::get();
	cameraConstants.currentTime = inputSystem->getCurrentTime();
	cameraConstants.deltaTime = inputSystem->getDeltaTime();

	auto transformView = manager->tryGet<TransformComponent>(camera);
	if (transformView)
	{
		cameraConstants.view = calcRelativeView(*transformView);
		setTranslation(cameraConstants.view, f32x4::zero);
		cameraConstants.cameraPos = transformView->getPosition();
		cameraConstants.cameraPos.fixW();
	}
	else
	{
		cameraConstants.view = f32x4x4::identity;
		cameraConstants.cameraPos = f32x4::zero;
	}

	auto cameraView = manager->tryGet<CameraComponent>(camera);
	if (cameraView)
	{
		cameraConstants.projection = cameraView->calcProjection();
		cameraConstants.nearPlane = cameraView->getNearPlane();

		if (cameraView->type == ProjectionType::Perspective)
		{
			cameraConstants.anglePerPixel = calcAnglePerPixel(
				cameraView->p.perspective.fieldOfView, scaledFramebufferSize.y);
		}
		else
		{
			cameraConstants.anglePerPixel = 0.0f;
		}
	}
	else
	{
		cameraConstants.projection = f32x4x4::identity;
		cameraConstants.nearPlane = defaultHmdDepth;
		cameraConstants.anglePerPixel = 0.0f;
	}

	cameraConstants.viewProj = cameraConstants.projection * cameraConstants.view;
	cameraConstants.inverseView = inverse4x4(cameraConstants.view);
	cameraConstants.inverseProj = inverse4x4(cameraConstants.projection);
	cameraConstants.invViewProj = inverse4x4(cameraConstants.viewProj);
	cameraConstants.viewDir = normalize3(cameraConstants.inverseView * f32x4(f32x4::front, 1.0f));
	cameraConstants.viewDir.fixW();

	if (directionalLight)
	{
		auto lightTransformView = manager->tryGet<TransformComponent>(directionalLight);
		if (lightTransformView)
			cameraConstants.lightDir = normalize3(lightTransformView->getRotation() * f32x4::front);
		else
			cameraConstants.lightDir = f32x4::bottom;
	}
	else
	{
		cameraConstants.lightDir = f32x4::bottom;
	}
	cameraConstants.lightDir.fixW();

	cameraConstants.frameSize = scaledFramebufferSize;
	cameraConstants.invFrameSize = 1.0f / (float2)scaledFramebufferSize;
	cameraConstants.invFrameSizeSq = cameraConstants.invFrameSize * 2.0f;
}

static void limitFrameRate(double beginSleepClock, uint16 maxFPS)
{
	auto endClock = mpio::OS::getCurrentClock();
	auto deltaClock = (endClock - beginSleepClock) * 1000.0;
	auto delayTime = 1000 / (int)maxFPS - (int)deltaClock - 1;
	if (delayTime > 0)
		this_thread::sleep_for(chrono::milliseconds(delayTime));
	// TODO: use loop with empty cycles to improve sleep precision.
}

//**********************************************************************************************************************
void GraphicsSystem::input()
{
	auto inputSystem = InputSystem::Instance::get();
	auto windowSize = inputSystem->getWindowSize();
	auto framebufferSize = inputSystem->getFramebufferSize();
	isFramebufferSizeValid = windowSize.x > 0 && windowSize.y > 0 && framebufferSize.x > 0 && framebufferSize.y > 0;
	beginSleepClock = mpio::OS::getCurrentClock();
}
void GraphicsSystem::update()
{
	SET_CPU_ZONE_SCOPED("Graphics Update");

	auto graphicsAPI = GraphicsAPI::get();
	auto swapchain = graphicsAPI->swapchain;
	auto inputSystem = InputSystem::Instance::get();

	SwapchainChanges newSwapchainChanges;
	newSwapchainChanges.framebufferSize = inputSystem->getFramebufferSize() != swapchain->getFramebufferSize();
	newSwapchainChanges.imageCount = useTripleBuffering != swapchain->useTripleBuffering();
	newSwapchainChanges.vsyncState = useVsync != swapchain->useVsync();

	auto swapchainRecreated = isFramebufferSizeValid && (newSwapchainChanges.framebufferSize ||
		newSwapchainChanges.imageCount || newSwapchainChanges.vsyncState || outOfDateSwapchain);

	swapchainChanges.framebufferSize |= newSwapchainChanges.framebufferSize;
	swapchainChanges.imageCount |= newSwapchainChanges.imageCount;
	swapchainChanges.vsyncState |= newSwapchainChanges.vsyncState;
	
	if (swapchainRecreated)
	{
		SET_CPU_ZONE_SCOPED("Swapchain Recreate");

		swapchain->recreate(inputSystem->getFramebufferSize(), useVsync, useTripleBuffering);
		auto framebufferSize = swapchain->getFramebufferSize();
		outOfDateSwapchain = false;

		GARDEN_LOG_INFO("Recreated swapchain. (" + to_string(framebufferSize.x) + "x" +
			to_string(framebufferSize.y) + " px, " + to_string(swapchain->getImageCount()) + "I)");
	}

	if (isFramebufferSizeValid)
	{
		auto threadSystem = ThreadSystem::Instance::tryGet();
		if (!swapchain->acquireNextImage(&threadSystem->getForegroundPool()))
		{
			isFramebufferSizeValid = false;
			outOfDateSwapchain = true;
			GARDEN_LOG_DEBUG("Out of date swapchain. [Acquire]");
		}

		updateCurrentFramebuffer(swapchainFramebuffer, swapchain->getFramebufferSize());
	}
	
	if (swapchainRecreated || forceRecreateSwapchain)
	{
		SET_CPU_ZONE_SCOPED("Swapchain Recreate");
		Manager::Instance::get()->runEvent("SwapchainRecreate");
		swapchainChanges = {};
		forceRecreateSwapchain = false;
	}

	if (camera && isFramebufferSizeValid)
	{
		prepareCameraConstants(camera, directionalLight,
			getScaledFramebufferSize(), currentCameraConstants);
		auto cameraBuffer = graphicsAPI->bufferPool.get(
			cameraConstantsBuffers[swapchain->getInFlightIndex()][0]);
		cameraBuffer->writeData(&currentCameraConstants);
	}
}

//**********************************************************************************************************************
void GraphicsSystem::present()
{
	SET_CPU_ZONE_SCOPED("Frame Present");

	auto graphicsAPI = GraphicsAPI::get();
	if (isFramebufferSizeValid)
	{
		graphicsAPI->graphicsCommandBuffer->submit();
		graphicsAPI->transferCommandBuffer->submit();
		graphicsAPI->computeCommandBuffer->submit();
		graphicsAPI->frameCommandBuffer->submit();
	}
	
	graphicsAPI->tlasPool.dispose();
	graphicsAPI->blasPool.dispose();
	graphicsAPI->descriptorSetPool.dispose();
	graphicsAPI->rayTracingPipelinePool.dispose();
	graphicsAPI->computePipelinePool.dispose();
	graphicsAPI->graphicsPipelinePool.dispose();
	graphicsAPI->samplerPool.dispose();
	graphicsAPI->framebufferPool.dispose();
	graphicsAPI->imageViewPool.dispose();
	graphicsAPI->imagePool.dispose();
	graphicsAPI->bufferPool.dispose();

	if (isFramebufferSizeValid)
	{
		graphicsAPI->flushDestroyBuffer();

		if (graphicsAPI->swapchain->present())
		{
			if (!useVsync)
				limitFrameRate(beginSleepClock, maxFPS);
		}
		else
		{
			isFramebufferSizeValid = false;
			outOfDateSwapchain = true;
			GARDEN_LOG_DEBUG("Out of date swapchain. [Present]");
		}
		frameIndex++;
	}
	else
	{
		limitFrameRate(beginSleepClock, maxFPS);
	}

	tickIndex++;

	#if GARDEN_TRACY_PROFILER
	FrameMark;
	#endif
}

//**********************************************************************************************************************
float GraphicsSystem::getRenderScale()
{
	if (renderScale == 0.0f)
	{
		// Note: renderScale should be the same across init stages.
		auto settingsSystem = SettingsSystem::Instance::tryGet();
		if (settingsSystem)
			settingsSystem->getFloat("renderScale", renderScale);
	}
	return renderScale;
}
void GraphicsSystem::setRenderScale(float renderScale)
{
	GARDEN_ASSERT(renderScale > 0.0f);
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
	return max((uint2)(float2(framebufferSize) * renderScale), uint2::one);
}

ID<Framebuffer> GraphicsSystem::getCurrentFramebuffer() const noexcept
{
	return GraphicsAPI::get()->currentFramebuffer;
}
uint8 GraphicsSystem::getCurrentSubpassIndex() const noexcept
{
	return GraphicsAPI::get()->currentSubpassIndex;
}
bool GraphicsSystem::isCurrentRenderPassAsync() const noexcept
{
	return GraphicsAPI::get()->isCurrentRenderPassAsync;
}

bool GraphicsSystem::hasRayTracing() const noexcept
{
	return GraphicsAPI::get()->hasRayTracing();
}
bool GraphicsSystem::hasRayQuery() const noexcept
{
	return GraphicsAPI::get()->hasRayQuery();
}

uint32 GraphicsSystem::getInFlightCount() const noexcept
{
	return inFlightCount;
}
uint32 GraphicsSystem::getInFlightIndex() const noexcept
{
	return GraphicsAPI::get()->swapchain->getInFlightIndex();
}

uint32 GraphicsSystem::getThreadCount() const noexcept
{
	return GraphicsAPI::get()->threadCount;
}

//**********************************************************************************************************************
ID<Buffer> GraphicsSystem::getCubeVertexBuffer()
{
	if (!cubeVertexBuffer)
	{
		cubeVertexBuffer = createBuffer(Buffer::Usage::Vertex | Buffer::Usage::TransferDst |
			Buffer::Usage::TransferQ, Buffer::CpuAccess::None, cubeVertices);
		SET_RESOURCE_DEBUG_NAME(cubeVertexBuffer, "buffer.vertex.cube");
	}
	return cubeVertexBuffer;
}
ID<Buffer> GraphicsSystem::getQuadVertexBuffer()
{
	if (!quadVertexBuffer)
	{
		quadVertexBuffer = createBuffer(Buffer::Usage::Vertex | Buffer::Usage::TransferDst |
			Buffer::Usage::TransferQ, Buffer::CpuAccess::None, quadVertices);
		SET_RESOURCE_DEBUG_NAME(quadVertexBuffer, "buffer.vertex.quad");
	}
	return quadVertexBuffer;
}

ID<ImageView> GraphicsSystem::getEmptyTexture()
{
	if (!emptyTexture)
	{
		const Color data[1] = { Color::transparent };
		auto texture = createImage(Image::Format::UnormB8G8R8A8,
			Image::Usage::Sampled | Image::Usage::TransferDst, { { data } }, uint2::one);
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
		auto texture = createImage(Image::Format::UnormB8G8R8A8,
			Image::Usage::Sampled | Image::Usage::TransferDst, { { data } }, uint2::one);
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
		auto texture = createImage(Image::Format::UnormB8G8R8A8,
			Image::Usage::Sampled | Image::Usage::TransferDst, { { data } }, uint2::one);
		SET_RESOURCE_DEBUG_NAME(texture, "image.greenTexture");
		greenTexture = GraphicsAPI::get()->imagePool.get(texture)->getDefaultView();
	}
	return greenTexture;
}
ID<ImageView> GraphicsSystem::getNormalMapTexture()
{
	if (!normalMapTexture)
	{
		const Color data[1] = { Color(255, 127, 127, 255) }; // Note: R/B flipped.
		auto texture = createImage(Image::Format::UnormB8G8R8A8,
			Image::Usage::Sampled | Image::Usage::TransferDst, { { data } }, uint2::one);
		SET_RESOURCE_DEBUG_NAME(texture, "image.normalMapTexture");
		normalMapTexture = GraphicsAPI::get()->imagePool.get(texture)->getDefaultView();
	}
	return normalMapTexture;
}

//**********************************************************************************************************************
void GraphicsSystem::recreateSwapchain(const SwapchainChanges& changes)
{
	GARDEN_ASSERT_MSG(changes.framebufferSize || changes.imageCount || changes.vsyncState, 
		"Redundant recreate swapchain call, no swapchain changes reported");
	swapchainChanges.framebufferSize |= changes.framebufferSize;
	swapchainChanges.imageCount |= changes.imageCount;
	swapchainChanges.vsyncState |= changes.vsyncState;
	forceRecreateSwapchain = true;
}

//**********************************************************************************************************************
ID<Buffer> GraphicsSystem::createBuffer(Buffer::Usage usage, Buffer::CpuAccess cpuAccess,
	const void* data, uint64 size, Buffer::Location location, Buffer::Strategy strategy)
{
	GARDEN_ASSERT(size > 0);

	auto graphicsAPI = GraphicsAPI::get();
	auto buffer = graphicsAPI->bufferPool.create(usage, cpuAccess, location, strategy, size, 0);
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
			GARDEN_ASSERT(hasAnyFlag(usage, Buffer::Usage::TransferDst));
			auto stagingBuffer = graphicsAPI->bufferPool.create(Buffer::Usage::TransferSrc, 
				Buffer::CpuAccess::SequentialWrite, Buffer::Location::Auto, Buffer::Strategy::Speed, size, 0);
			SET_RESOURCE_DEBUG_NAME(stagingBuffer, "buffer.staging" + to_string(*stagingBuffer));
			auto stagingBufferView = graphicsAPI->bufferPool.get(stagingBuffer);
			stagingBufferView->writeData(data, size);

			#if GARDEN_DEBUG // Hack: skips queue ownership asserts.
			BufferExt::getUsage(**stagingBufferView) |= Buffer::Usage::TransferQ | Buffer::Usage::ComputeQ;
			#endif

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
ID<Image> GraphicsSystem::createImage(Image::Type type, Image::Format format, Image::Usage usage,
	const Image::Mips& data, uint3 size, Image::Strategy strategy, Image::Format dataFormat)
{
	GARDEN_ASSERT(format != Image::Format::Undefined);
	GARDEN_ASSERT(!data.empty());
	GARDEN_ASSERT(areAllTrue(size > uint3::zero));

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

	auto mipSize = (uint3)size;
	auto formatBinarySize = (uint64)toBinarySize(dataFormat);
	uint64 stagingSize = 0; uint32 stagingCount = 0;

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

		mipSize = max(mipSize / 2u, uint3::one);
	}

	auto imageSize = u32x4(u32x4(size), mipCount);
	if (type != Image::Type::Texture3D)
		imageSize.setZ(layerCount);

	auto graphicsAPI = GraphicsAPI::get();
	auto image = graphicsAPI->imagePool.create(type, format, usage, strategy, imageSize, 0);
	SET_RESOURCE_DEBUG_NAME(image, "image" + to_string(*image));

	if (stagingCount > 0)
	{
		GARDEN_ASSERT(hasAnyFlag(usage, Image::Usage::TransferDst));
		auto stagingBuffer = graphicsAPI->bufferPool.create(Buffer::Usage::TransferSrc, 
			Buffer::CpuAccess::SequentialWrite, Buffer::Location::Auto, Buffer::Strategy::Speed, stagingSize, 0);
		SET_RESOURCE_DEBUG_NAME(stagingBuffer, "buffer.imageStaging" + to_string(*stagingBuffer));
		
		ID<Image> targetImage; 
		if (format == dataFormat)
		{
			targetImage = image;
		}
		else
		{
			targetImage = graphicsAPI->imagePool.create(type, dataFormat, Image::Usage::TransferSrc | 
				Image::Usage::TransferDst, Image::Strategy::Speed, imageSize, 0);
			SET_RESOURCE_DEBUG_NAME(targetImage, "image.staging" + to_string(*targetImage));

			#if GARDEN_DEBUG // Hack: skips queue ownership asserts.
			auto targetImageView = graphicsAPI->imagePool.get(targetImage);
			ImageExt::getUsage(**targetImageView) |= Image::Usage::TransferQ | Image::Usage::ComputeQ;
			#endif
		}

		auto stagingBufferView = graphicsAPI->bufferPool.get(stagingBuffer);
		auto stagingMap = stagingBufferView->getMap();
		vector<Image::CopyBufferRegion> regions(stagingCount);
		uint64 stagingOffset = 0; uint32 copyIndex = 0;
		mipSize = (uint3)size;

		for (uint8 mip = 0; mip < mipCount; mip++)
		{
			const auto& mipData = data[mip].data();
			auto mipLayerCount = (uint32)data[mip].size();
			auto binarySize = formatBinarySize * mipSize.x * mipSize.y * mipSize.z;

			for (uint32 layer = 0; layer < mipLayerCount; layer++)
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

			mipSize = max(mipSize / 2u, uint3::one);
		}

		GARDEN_ASSERT(stagingCount == copyIndex);
		GARDEN_ASSERT(stagingSize == stagingOffset);

		stagingBufferView->flush();

		#if GARDEN_DEBUG // Hack: skips queue ownership asserts.
		BufferExt::getUsage(**stagingBufferView) |= Buffer::Usage::TransferQ | Buffer::Usage::ComputeQ;
		#endif

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
			mipSize = (uint3)size;

			for (uint8 i = 0; i < mipCount; i++)
			{
				Image::BlitRegion region;
				region.srcExtent = mipSize;
				region.dstExtent = mipSize;
				region.layerCount = layerCount;
				region.srcMipLevel = i;
				region.dstMipLevel = i;
				blitRegions[i] = region;
				mipSize = max(mipSize / 2u, uint3::one);
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
		GARDEN_ASSERT_MSG(!imageView->isSwapchain(), "Can not destroy swapchain image");

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
	GARDEN_ASSERT_MSG(ResourceExt::getInstance(**_image), 
		"Image [" + _image->getDebugName() + "] is not ready");
	GARDEN_ASSERT(mipCount + baseMip <= _image->getMipCount());
	GARDEN_ASSERT(layerCount + baseLayer <= _image->getLayerCount());

	if (format == Image::Format::Undefined)
		format = _image->getFormat();
	if (mipCount == 0)
		mipCount = _image->getMipCount();
	if (layerCount == 0)
		layerCount = _image->getLayerCount();

	if (type != Image::Type::Texture1DArray && type != Image::Type::Texture2DArray)
		GARDEN_ASSERT_MSG(layerCount == 1, "Texture array can not have layers");

	auto imageView = graphicsAPI->imageViewPool.create(false, image,
		type, format, baseLayer, layerCount, baseMip, mipCount);
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
ID<Framebuffer> GraphicsSystem::createFramebuffer(uint2 size,
	vector<Framebuffer::OutputAttachment>&& colorAttachments, Framebuffer::OutputAttachment depthStencilAttachment)
{
	GARDEN_ASSERT(areAllTrue(size > uint2::zero));
	GARDEN_ASSERT(!colorAttachments.empty() || depthStencilAttachment.imageView);
	auto graphicsAPI = GraphicsAPI::get();

	// TODO: add checks if attachments do not overlaps and repeat.
	// TODO: we can use attachments with different sizes, but should we?

	#if GARDEN_DEBUG
	uint32 validColorAttachCount = 0;
	for	(uint32 i = 0; i < (uint32)colorAttachments.size(); i++)
	{
		auto colorAttachment = colorAttachments[i];
		if (!colorAttachment.imageView)
			continue;

		auto imageView = graphicsAPI->imageViewPool.get(colorAttachment.imageView);
		GARDEN_ASSERT_MSG(isFormatColor(imageView->getFormat()), "Incorrect framebuffer color "
			"attachment [" + to_string(i) + "] image view [" + imageView->getDebugName() + "] format");
		auto image = graphicsAPI->imagePool.get(imageView->getImage());
		GARDEN_ASSERT_MSG(size == imageView->calcSize(), "Incorrect framebuffer color attachment [" + 
			to_string(i) + "] image view [" + imageView->getDebugName() + "] size at mip");
		GARDEN_ASSERT_MSG(hasAnyFlag(image->getUsage(), Image::Usage::ColorAttachment), "Missing framebuffer "
			"color attachment [" + to_string(i) + "] image view [" + imageView->getDebugName() + "] flag");
		validColorAttachCount++;
	}
	GARDEN_ASSERT((!colorAttachments.empty() && validColorAttachCount > 0) || colorAttachments.empty());

	if (depthStencilAttachment.imageView)
	{
		auto imageView = graphicsAPI->imageViewPool.get(depthStencilAttachment.imageView);
		GARDEN_ASSERT_MSG(isFormatDepthOrStencil(imageView->getFormat()), "Incorrect framebuffer depth/stencil " 
			"attachment image view [" + imageView->getDebugName() + "] format");
		auto image = graphicsAPI->imagePool.get(imageView->getImage());
		GARDEN_ASSERT_MSG(size == imageView->calcSize(), "Incorrect framebuffer depth/stencil "
			"attachment image view [" + imageView->getDebugName() + "] size at mip");
		GARDEN_ASSERT_MSG(hasAnyFlag(image->getUsage(), Image::Usage::DepthStencilAttachment), "Missing "
			"framebuffer depth/stencil attachment image view [" + imageView->getDebugName() + "] flag");
	}
	#endif

	auto framebuffer = graphicsAPI->framebufferPool.create(size,
		std::move(colorAttachments), depthStencilAttachment);
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer" + to_string(*framebuffer));
	return framebuffer;
}

//**********************************************************************************************************************
ID<Framebuffer> GraphicsSystem::createFramebuffer(uint2 size, vector<Framebuffer::Subpass>&& subpasses)
{
	GARDEN_ASSERT(areAllTrue(size > uint2::zero));
	GARDEN_ASSERT(!subpasses.empty());
	auto graphicsAPI = GraphicsAPI::get();

	#if GARDEN_DEBUG
	psize outputAttachmentCount = 0;
	for (const auto& subpass : subpasses)
	{
		const auto& inputAttachments = subpass.inputAttachments;
		for	(uint32 i = 0; i < (uint32)inputAttachments.size(); i++)
		{
			auto inputAttachment = inputAttachments[i];
			auto imageView = graphicsAPI->imageViewPool.get(inputAttachment.imageView);
			GARDEN_ASSERT_MSG(inputAttachment.imageView, "Incorrect framebuffer input "
				"attachment [" + to_string(i) + "] image view [" + imageView->getDebugName() + "] format");
			GARDEN_ASSERT_MSG(inputAttachment.shaderStages != ShaderStage::None, "No framebuffer input "
				"attachment [" + to_string(i) + "] image view [" + imageView->getDebugName() + "] shader stages");
			
			auto image = graphicsAPI->imagePool.get(imageView->getImage());
			GARDEN_ASSERT_MSG(size == imageView->calcSize(), "Incorrect framebuffer input attachment [" + 
				to_string(i) + "] image view [" + imageView->getDebugName() + "] size at mip");
			GARDEN_ASSERT_MSG(hasAnyFlag(image->getUsage(), Image::Usage::InputAttachment), "Missing framebuffer "
				"input attachment [" + to_string(i) + "] image view [" + imageView->getDebugName() + "] flag");
		}

		const auto& outputAttachments = subpass.outputAttachments;
		for	(uint32 i = 0; i < (uint32)outputAttachments.size(); i++)
		{
			auto outputAttachment = outputAttachments[i];
			auto imageView = graphicsAPI->imageViewPool.get(outputAttachment.imageView);
			GARDEN_ASSERT_MSG(outputAttachment.imageView, "Framebuffer "
				"output attachment [" + to_string(i) + "] image view is null");
			GARDEN_ASSERT_MSG((!outputAttachment.flags.clear && !outputAttachment.flags.load) ||
				(outputAttachment.flags.clear && !outputAttachment.flags.load) ||
				(!outputAttachment.flags.clear && outputAttachment.flags.load),
				"Incorrect framebuffer output attachment [" + to_string(i) + 
				"] image view [" + imageView->getDebugName() + "] flags");
			
			auto image = graphicsAPI->imagePool.get(imageView->getImage());
			GARDEN_ASSERT_MSG(size == imageView->calcSize(), "Incorrect framebuffer output attachment [" + 
				to_string(i) + "] image view [" + imageView->getDebugName() + "] size at mip");
			#if GARDEN_DEBUG
			if (isFormatColor(imageView->getFormat()))
			{
				GARDEN_ASSERT_MSG(hasAnyFlag(image->getUsage(), Image::Usage::ColorAttachment), "Missing framebuffer "
					"output attachment [" + to_string(i) + "] image view [" + imageView->getDebugName() + "] color flag");
			}
			#endif
			outputAttachmentCount++;

			for	(auto inputAttachment : subpass.inputAttachments)
			{
				GARDEN_ASSERT_MSG(outputAttachment.imageView != inputAttachment.imageView, 
					"Framebuffer output attachment [" + to_string(i) +  "] image view [" + 
					imageView->getDebugName() + "] is also used as an input attachment");
			}
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
ID<Sampler> GraphicsSystem::createSampler(const Sampler::State& state)
{
	auto graphicsAPI = GraphicsAPI::get();
	auto sampler = graphicsAPI->samplerPool.create(state);
	SET_RESOURCE_DEBUG_NAME(sampler, "sampler" + to_string(*sampler));
	return sampler;
}
void GraphicsSystem::destroy(ID<Sampler> sampler)
{
	GraphicsAPI::get()->samplerPool.destroy(sampler);
}

View<Sampler> GraphicsSystem::get(ID<Sampler> sampler) const
{
	return GraphicsAPI::get()->samplerPool.get(sampler);
}

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

void GraphicsSystem::destroy(ID<RayTracingPipeline> rayTracingPipeline)
{
	GraphicsAPI::get()->rayTracingPipelinePool.destroy(rayTracingPipeline);
}
View<RayTracingPipeline> GraphicsSystem::get(ID<RayTracingPipeline> rayTracingPipeline) const
{
	return GraphicsAPI::get()->rayTracingPipelinePool.get(rayTracingPipeline);
}

//**********************************************************************************************************************
ID<DescriptorSet> GraphicsSystem::createDescriptorSet(ID<GraphicsPipeline> graphicsPipeline,
	DescriptorSet::Uniforms&& uniforms, DescriptorSet::Samplers&& samplers, uint8 index)
{
	GARDEN_ASSERT(graphicsPipeline);
	GARDEN_ASSERT(!uniforms.empty());

	#if GARDEN_DEBUG
	auto pipelineView = GraphicsAPI::get()->graphicsPipelinePool.get(graphicsPipeline);
	GARDEN_ASSERT_MSG(ResourceExt::getInstance(**pipelineView), "Pipeline [" + 
		pipelineView->getDebugName() + "] is not ready");
	GARDEN_ASSERT_MSG(index < PipelineExt::getDescriptorSetLayouts(**pipelineView).size(),
		"Out of pipeline [" + pipelineView->getDebugName() + "] descriptor set count bounds");

	// TODO: check if all items initialized if not using bindless.
	#endif

	auto descriptorSet = GraphicsAPI::get()->descriptorSetPool.create(ID<Pipeline>(graphicsPipeline), 
		PipelineType::Graphics, std::move(uniforms), std::move(samplers), index);
	SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet" + to_string(*descriptorSet));
	return descriptorSet;
}
ID<DescriptorSet> GraphicsSystem::createDescriptorSet(ID<ComputePipeline> computePipeline,
	DescriptorSet::Uniforms&& uniforms, DescriptorSet::Samplers&& samplers, uint8 index)
{
	GARDEN_ASSERT(computePipeline);
	GARDEN_ASSERT(!uniforms.empty());

	#if GARDEN_DEBUG
	auto pipelineView = GraphicsAPI::get()->computePipelinePool.get(computePipeline);
	GARDEN_ASSERT_MSG(ResourceExt::getInstance(**pipelineView), "Pipeline [" + 
		pipelineView->getDebugName() + "] is not ready");
	GARDEN_ASSERT_MSG(index < PipelineExt::getDescriptorSetLayouts(**pipelineView).size(),
		"Out of pipeline [" + pipelineView->getDebugName() + "] descriptor set count bounds");

	// TODO: check if all items initialized if not using bindless.
	#endif

	auto descriptorSet = GraphicsAPI::get()->descriptorSetPool.create(ID<Pipeline>(computePipeline), 
		PipelineType::Compute, std::move(uniforms), std::move(samplers), index);
	SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet" + to_string(*descriptorSet));
	return descriptorSet;
}
ID<DescriptorSet> GraphicsSystem::createDescriptorSet(ID<RayTracingPipeline> rayTracingPipeline,
	DescriptorSet::Uniforms&& uniforms, DescriptorSet::Samplers&& samplers, uint8 index)
{
	GARDEN_ASSERT(rayTracingPipeline);
	GARDEN_ASSERT(!uniforms.empty());

	#if GARDEN_DEBUG
	auto pipelineView = GraphicsAPI::get()->rayTracingPipelinePool.get(rayTracingPipeline);
	GARDEN_ASSERT_MSG(ResourceExt::getInstance(**pipelineView), "Pipeline [" + 
		pipelineView->getDebugName() + "] is not ready");
	GARDEN_ASSERT_MSG(index < PipelineExt::getDescriptorSetLayouts(**pipelineView).size(),
		"Out of pipeline [" + pipelineView->getDebugName() + "] descriptor set count bounds");

	// TODO: check if all items initialized if not using bindless.
	#endif

	auto descriptorSet = GraphicsAPI::get()->descriptorSetPool.create(ID<Pipeline>(rayTracingPipeline), 
		PipelineType::RayTracing, std::move(uniforms), std::move(samplers), index);
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
ID<Blas> GraphicsSystem::createBlas(const Blas::TrianglesBuffer* geometryArray, 
	uint32 geometryCount, BuildFlagsAS flags)
{
	GARDEN_ASSERT(geometryArray);
	GARDEN_ASSERT(geometryCount > 0);

	auto graphicsAPI = GraphicsAPI::get();
	auto blas = graphicsAPI->blasPool.create(geometryArray, geometryCount, flags);
	SET_RESOURCE_DEBUG_NAME(blas, "blas" + to_string(*blas));
	return blas;
}
ID<Blas> GraphicsSystem::createBlas(const Blas::AabbsBuffer* geometryArray, 
	uint32 geometryCount, BuildFlagsAS flags)
{
	GARDEN_ASSERT(geometryArray);
	GARDEN_ASSERT(geometryCount > 0);

	auto graphicsAPI = GraphicsAPI::get();
	auto blas = graphicsAPI->blasPool.create(geometryArray, geometryCount, flags);
	SET_RESOURCE_DEBUG_NAME(blas, "blas" + to_string(*blas));
	return blas;
}
void GraphicsSystem::destroy(ID<Blas> blas)
{
	GraphicsAPI::get()->blasPool.destroy(blas);
}
View<Blas> GraphicsSystem::get(ID<Blas> blas) const
{
	return GraphicsAPI::get()->blasPool.get(blas);
}

ID<Tlas> GraphicsSystem::createTlas(vector<Tlas::InstanceData>&& instances, 
	ID<Buffer> instanceBuffer, BuildFlagsAS flags)
{
	GARDEN_ASSERT(!instances.empty());
	GARDEN_ASSERT(instanceBuffer);

	auto graphicsAPI = GraphicsAPI::get();
	auto tlas = graphicsAPI->tlasPool.create(std::move(instances), instanceBuffer, flags);
	SET_RESOURCE_DEBUG_NAME(tlas, "tlas" + to_string(*tlas));
	return tlas;
}
void GraphicsSystem::destroy(ID<Tlas> tlas)
{
	return GraphicsAPI::get()->tlasPool.destroy(tlas);
}
View<Tlas> GraphicsSystem::get(ID<Tlas> tlas) const
{
	return GraphicsAPI::get()->tlasPool.get(tlas);
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
	case CommandBufferType::Compute:
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
bool GraphicsSystem::isBusy(CommandBufferType commandBufferType)
{
	switch (commandBufferType)
	{
	case CommandBufferType::Frame: return false;
	case CommandBufferType::Graphics:
		return GraphicsAPI::get()->graphicsCommandBuffer->isBusy();
	case CommandBufferType::TransferOnly:
		return GraphicsAPI::get()->transferCommandBuffer->isBusy();
	case CommandBufferType::Compute:
		return GraphicsAPI::get()->computeCommandBuffer->isBusy();
	default: abort();
	}
}

#if GARDEN_DEBUG || GARDEN_EDITOR
//**********************************************************************************************************************
void GraphicsSystem::drawLine(const f32x4x4& mvp, f32x4 startPoint, f32x4 endPoint, f32x4 color)
{
	if (!linePipeline)
	{
		ResourceSystem::GraphicsOptions options;
		options.loadAsync = false;

		linePipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline(
			"editor/wireframe-line", swapchainFramebuffer, options);
	}

	auto pipelineView = GraphicsAPI::get()->graphicsPipelinePool.get(linePipeline);
	pipelineView->updateFramebuffer(GraphicsAPI::get()->currentFramebuffer);

	LinePC pc;
	pc.mvp = (float4x4)mvp;
	pc.color = (float4)color;
	pc.startPoint = float4((float3)startPoint, 1.0f);
	pc.endPoint = float4((float3)endPoint, 1.0f);

	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->pushConstants(&pc);
	pipelineView->draw({}, 2);
}
void GraphicsSystem::drawAabb(const f32x4x4& mvp, f32x4 color)
{
	if (!aabbPipeline)
	{
		ResourceSystem::GraphicsOptions options;
		options.loadAsync = false;

		aabbPipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline(
			"editor/aabb-lines", GraphicsAPI::get()->currentFramebuffer, options);
	}

	auto pipelineView = GraphicsAPI::get()->graphicsPipelinePool.get(aabbPipeline);
	pipelineView->updateFramebuffer(GraphicsAPI::get()->currentFramebuffer);

	AabbPC pc;
	pc.mvp = (float4x4)mvp;
	pc.color = (float4)color;

	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->pushConstants(&pc);
	pipelineView->draw({}, 24);
}

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
void GraphicsSystem::setDebugName(ID<Sampler> sampler, const string& name)
{
	auto resource = GraphicsAPI::get()->samplerPool.get(sampler);
	resource->setDebugName(name);
}
void GraphicsSystem::setDebugName(ID<DescriptorSet> descriptorSet, const string& name)
{
	auto resource = GraphicsAPI::get()->descriptorSetPool.get(descriptorSet);
	resource->setDebugName(name);
}
void GraphicsSystem::setDebugName(ID<Blas> blas, const string& name)
{
	auto resource = GraphicsAPI::get()->blasPool.get(blas);
	resource->setDebugName(name);
}
void GraphicsSystem::setDebugName(ID<Tlas> tlas, const string& name)
{
	auto resource = GraphicsAPI::get()->tlasPool.get(tlas);
	resource->setDebugName(name);
}
#endif