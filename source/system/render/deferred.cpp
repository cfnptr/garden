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

#include "garden/system/render/gpu-process.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/render/forward.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/camera.hpp"
#include "garden/profiler.hpp"
#include "math/brdf.hpp"

// TODO: allow to disable OIT and other buffers creation/usage.

using namespace garden;

//**********************************************************************************************************************
static void createGBuffers(GraphicsSystem* graphicsSystem, 
	vector<ID<Image>>& gBuffers, const DeferredRenderSystem::Options& options)
{
	constexpr auto usage = Image::Usage::ColorAttachment | Image::Usage::Sampled | 
		Image::Usage::TransferSrc | Image::Usage::TransferDst | Image::Usage::Fullscreen;
	constexpr auto strategy = Image::Strategy::Size;
	Image::Format formats[DeferredRenderSystem::gBufferCount]
	{
		DeferredRenderSystem::gBufferFormat0,
		DeferredRenderSystem::gBufferFormat1,
		DeferredRenderSystem::gBufferFormat2,
		options.useClearCoat ? DeferredRenderSystem::gBufferFormat3 : Image::Format::Undefined,
		options.useEmission ? DeferredRenderSystem::gBufferFormat4 : Image::Format::Undefined,
		options.useVelocity ? DeferredRenderSystem::gBufferFormat5 : Image::Format::Undefined
	};

	const Image::Mips mips = { { nullptr } };
	auto framebufferSize = graphicsSystem->getScaledFrameSize();
	gBuffers.resize(DeferredRenderSystem::gBufferCount);

	for (uint8 i = 0; i < DeferredRenderSystem::gBufferCount; i++)
	{
		if (formats[i] == Image::Format::Undefined)
		{
			gBuffers[i] = {};
			continue;
		}

		gBuffers[i] = graphicsSystem->createImage(formats[i], usage, mips, framebufferSize, strategy);
		SET_RESOURCE_DEBUG_NAME(gBuffers[i], "image.deferred.gBuffer" + to_string(i));
	}
}

static ID<Image> createDepthStencilBuffer(GraphicsSystem* graphicsSystem, bool useStencil, bool isCopy)
{
	auto image = graphicsSystem->createImage(useStencil ? DeferredRenderSystem::depthStencilFormat :
		DeferredRenderSystem::depthFormat, Image::Usage::DepthStencilAttachment | 
		Image::Usage::Sampled | Image::Usage::Fullscreen | Image::Usage::TransferSrc | Image::Usage::TransferDst, 
		{ { nullptr } }, graphicsSystem->getScaledFrameSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.depthStencil" + string(isCopy ? "Copy" : ""));
	return image;
}

//**********************************************************************************************************************
static ID<Image> createHdrBuffer(GraphicsSystem* graphicsSystem, bool isCopy, bool isFullSize)
{
	auto hdrBufferSize = isFullSize ? graphicsSystem->getFramebufferSize() : graphicsSystem->getScaledFrameSize();

	uint8 lodCount = 1, layerCount = 1;
	if (isCopy)
	{
		lodCount = brdf::calcGgxBlurLodCount(hdrBufferSize);
		layerCount = 2;
	}

	Image::Mips mips(lodCount);
	for (uint8 i = 0; i < lodCount; i++)
		mips[i].resize(layerCount);

	auto image = graphicsSystem->createImage(DeferredRenderSystem::hdrBufferFormat, Image::Usage::ColorAttachment | 
		Image::Usage::Sampled | Image::Usage::Storage | Image::Usage::Fullscreen | Image::Usage::TransferSrc | 
		Image::Usage::TransferDst, mips, hdrBufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.hdr" + 
		string(isCopy ? "Copy" : "") + string(isFullSize ? "Full" : ""));
	return image;
}
static ID<Image> createLdrBuffer(GraphicsSystem* graphicsSystem)
{
	auto image = graphicsSystem->createImage(DeferredRenderSystem::ldrBufferFormat, Image::Usage::ColorAttachment | 
		Image::Usage::Sampled | Image::Usage::Fullscreen | Image::Usage::TransferSrc | Image::Usage::TransferDst,
		{ { nullptr } }, graphicsSystem->getScaledFrameSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.ldr");
	return image;
}
static ID<Image> createUiBuffer(GraphicsSystem* graphicsSystem)
{
	auto image = graphicsSystem->createImage(DeferredRenderSystem::uiBufferFormat, Image::Usage::ColorAttachment | 
		Image::Usage::Sampled | Image::Usage::Fullscreen | Image::Usage::TransferSrc | Image::Usage::TransferDst,
		{ { nullptr } }, graphicsSystem->getFramebufferSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.ui");
	return image;
}

//**********************************************************************************************************************
static ID<Image> createOitAccumBuffer(GraphicsSystem* graphicsSystem)
{
	auto image = graphicsSystem->createImage(DeferredRenderSystem::oitAccumBufferFormat, 
		Image::Usage::ColorAttachment | Image::Usage::Sampled | Image::Usage::Fullscreen, 
		{ { nullptr } }, graphicsSystem->getScaledFrameSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.oitAccum");
	return image;
}
static ID<Image> createOitRevealBuffer(GraphicsSystem* graphicsSystem)
{
	auto image = graphicsSystem->createImage(DeferredRenderSystem::oitRevealBufferFormat, 
		Image::Usage::ColorAttachment | Image::Usage::Sampled | Image::Usage::Fullscreen, 
		{ { nullptr } }, graphicsSystem->getScaledFrameSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.oitReveal");
	return image;
}

static ID<Image> createTransBuffer(GraphicsSystem* graphicsSystem)
{
	auto image = graphicsSystem->createImage(DeferredRenderSystem::transBufferFormat, 
		Image::Usage::ColorAttachment | Image::Usage::Sampled | Image::Usage::Fullscreen, 
		{ { nullptr } }, graphicsSystem->getScaledFrameSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.trans");
	return image;
}

//**********************************************************************************************************************
static ID<ImageView> createDepthStencilIV(GraphicsSystem* graphicsSystem, ID<Image> depthStencilBuffer, bool useStencil)
{
	auto imageView = graphicsSystem->createImageView(depthStencilBuffer, Image::Type::Texture2D, 
		useStencil ? DeferredRenderSystem::depthStencilFormat : DeferredRenderSystem::depthFormat);
	SET_RESOURCE_DEBUG_NAME(imageView, "imageView.deferred.depthStencil");
	return imageView;
}
static ID<ImageView> createDepthCopyIV(GraphicsSystem* graphicsSystem, ID<Image> depthStencilBuffer)
{
	auto imageView = graphicsSystem->createImageView(depthStencilBuffer, 
		Image::Type::Texture2D, DeferredRenderSystem::depthFormat);
	SET_RESOURCE_DEBUG_NAME(imageView, "imageView.deferred.depthCopy");
	return imageView;
}
static ID<ImageView> createDepthImageView(GraphicsSystem* graphicsSystem, ID<Image> depthStencilBuffer)
{
	auto bufferView = graphicsSystem->get(depthStencilBuffer);
	if (isFormatStencilOnly(bufferView->getFormat()))
		return {};

	auto imageView = graphicsSystem->createImageView(depthStencilBuffer, 
		Image::Type::Texture2D, DeferredRenderSystem::depthFormat);
	SET_RESOURCE_DEBUG_NAME(imageView, "imageView.deferred.depth");
	return imageView;
}
static ID<ImageView> createStencilImageView(GraphicsSystem* graphicsSystem, ID<Image> depthStencilBuffer)
{
	auto bufferView = graphicsSystem->get(depthStencilBuffer);
	if (isFormatDepthOnly(bufferView->getFormat()))
		return {};

	auto imageView = graphicsSystem->createImageView(depthStencilBuffer, 
		Image::Type::Texture2D, DeferredRenderSystem::stencilFormat);
	SET_RESOURCE_DEBUG_NAME(imageView, "imageView.deferred.stencil");
	return imageView;
}

static ID<ImageView> createHdrCopyIV(GraphicsSystem* graphicsSystem, ID<Image> hdrCopyBuffer)
{
	auto imageView = graphicsSystem->createImageView(hdrCopyBuffer, 
		Image::Type::Texture2D, DeferredRenderSystem::hdrBufferFormat, 0, 0, 0, 1);
	SET_RESOURCE_DEBUG_NAME(imageView, "imageView.deferred.hdrCopy");
	return imageView;
}

//**********************************************************************************************************************
static ID<Framebuffer> createGFramebuffer(GraphicsSystem* graphicsSystem, 
	const vector<ID<Image>> gBuffers, ID<ImageView> depthStencilIV)
{
	vector<Framebuffer::OutputAttachment> colorAttachments(DeferredRenderSystem::gBufferCount);
	for (uint8 i = 0; i < DeferredRenderSystem::gBufferCount; i++)
	{
		if (!gBuffers[i])
		{
			colorAttachments[i] = {};
			continue;
		}

		auto gBufferView = graphicsSystem->get(gBuffers[i])->getDefaultView();
		colorAttachments[i] = Framebuffer::OutputAttachment(gBufferView, DeferredRenderSystem::gBufferFlags);
	}

	Framebuffer::OutputAttachment depthStencilAttachment(
		depthStencilIV, DeferredRenderSystem::gBufferDepthFlags);
	auto framebuffer = graphicsSystem->createFramebuffer(graphicsSystem->getScaledFrameSize(), 
		std::move(colorAttachments), depthStencilAttachment); 
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.gBuffer");
	return framebuffer;
}

static ID<Framebuffer> createHdrFramebuffer(GraphicsSystem* graphicsSystem, ID<Image> hdrBuffer, bool isFullSize)
{
	auto hdrBufferView = graphicsSystem->get(hdrBuffer)->getDefaultView();
	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(hdrBufferView, DeferredRenderSystem::hdrBufferFlags) };

	auto framebufferSize = isFullSize ? 
		graphicsSystem->getFramebufferSize() : graphicsSystem->getScaledFrameSize();
	auto framebuffer = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.hdr" + string(isFullSize ? "Full" : ""));
	return framebuffer;
}
static ID<Framebuffer> createDepthHdrFramebuffer(GraphicsSystem* graphicsSystem, 
	ID<Image> hdrBuffer, ID<ImageView> depthStencilIV)
{
	auto hdrBufferView = graphicsSystem->get(hdrBuffer)->getDefaultView();
	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(hdrBufferView, DeferredRenderSystem::hdrBufferFlags) };
	Framebuffer::OutputAttachment depthStencilAttachment(
		depthStencilIV, DeferredRenderSystem::hdrBufferDepthFlags);

	auto framebuffer = graphicsSystem->createFramebuffer(graphicsSystem->getScaledFrameSize(), 
		std::move(colorAttachments), depthStencilAttachment);
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.depthHdr");
	return framebuffer;
}
static ID<Framebuffer> createLdrFramebuffer(GraphicsSystem* graphicsSystem, ID<Image> ldrBuffer)
{
	auto ldrBufferView = graphicsSystem->get(ldrBuffer)->getDefaultView();
	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(ldrBufferView, DeferredRenderSystem::ldrBufferFlags) };

	auto framebuffer = graphicsSystem->createFramebuffer(
		graphicsSystem->getScaledFrameSize(), std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.ldr");
	return framebuffer;
}
static ID<Framebuffer> createDepthLdrFramebuffer(GraphicsSystem* graphicsSystem, 
	ID<Image> ldrBuffer, ID<ImageView> depthStencilIV)
{
	auto ldrBufferView = graphicsSystem->get(ldrBuffer)->getDefaultView();
	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(ldrBufferView, DeferredRenderSystem::ldrBufferFlags) };
	Framebuffer::OutputAttachment depthStencilAttachment(
		depthStencilIV, DeferredRenderSystem::ldrBufferDepthFlags);

	auto framebuffer = graphicsSystem->createFramebuffer(graphicsSystem->getScaledFrameSize(), 
		std::move(colorAttachments), depthStencilAttachment);
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.depthLdr");
	return framebuffer;
}

//**********************************************************************************************************************
static ID<Framebuffer> createUiFramebuffer(GraphicsSystem* graphicsSystem, ID<Image> uiBuffer)
{
	auto uiBufferView = graphicsSystem->get(uiBuffer)->getDefaultView();
	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(uiBufferView, DeferredRenderSystem::uiBufferFlags) };

	auto framebuffer = graphicsSystem->createFramebuffer(
		graphicsSystem->getFramebufferSize(), std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.ui");
	return framebuffer;
}
static ID<Framebuffer> createOitFramebuffer(GraphicsSystem* graphicsSystem, 
	ID<Image> oitAccumBuffer, ID<Image> oitRevealBuffer, ID<ImageView> depthStencilIV)
{
	auto oitAccumBufferView = graphicsSystem->get(oitAccumBuffer)->getDefaultView();
	auto oitRevealBufferView = graphicsSystem->get(oitRevealBuffer)->getDefaultView();

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{
		Framebuffer::OutputAttachment(oitAccumBufferView, DeferredRenderSystem::oitBufferFlags),
		Framebuffer::OutputAttachment(oitRevealBufferView, DeferredRenderSystem::oitBufferFlags)
	};
	Framebuffer::OutputAttachment depthStencilAttachment(
		depthStencilIV, DeferredRenderSystem::oitBufferDepthFlags);

	auto framebuffer = graphicsSystem->createFramebuffer(graphicsSystem->getScaledFrameSize(), 
		std::move(colorAttachments), depthStencilAttachment);
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.oit");
	return framebuffer;
}
static ID<Framebuffer> createTransDepthFramebuffer(GraphicsSystem* graphicsSystem, 
	ID<Image> transBuffer, ID<ImageView> depthStencilIV)
{
	auto transBufferView = graphicsSystem->get(transBuffer)->getDefaultView();
	GARDEN_ASSERT(graphicsSystem->get(transBuffer)->getFormat() == Image::Format::UnormR8);

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(transBufferView, DeferredRenderSystem::transBufferFlags) };
	Framebuffer::OutputAttachment depthStencilAttachment(
		depthStencilIV, DeferredRenderSystem::transBufferDepthFlags);

	auto framebuffer = graphicsSystem->createFramebuffer(graphicsSystem->getScaledFrameSize(), 
		std::move(colorAttachments), depthStencilAttachment);
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.transDepth");
	return framebuffer;
}

static ID<GraphicsPipeline> createVelocityPipeline(ID<Framebuffer> gFramebuffer, bool useAsyncRecording)
{
	ResourceSystem::GraphicsOptions options;
	options.useAsyncRecording = useAsyncRecording;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("velocity", gFramebuffer, options);
}
static DescriptorSet::Uniforms getVelocityUniforms(GraphicsSystem* graphicsSystem)
{
	return { { "cc", DescriptorSet::Uniform(graphicsSystem->getCommonConstantsBuffers()) } };
}

//**********************************************************************************************************************
DeferredRenderSystem::DeferredRenderSystem(Options options, 
	bool setSingleton) : Singleton(setSingleton), options(options)
{
	auto manager = Manager::Instance::get();
	manager->registerEvent("PreDeferredRender");
	manager->registerEvent("DeferredRender");
	manager->registerEvent("PreHdrRender");
	manager->registerEvent("HdrRender");
	manager->registerEvent("PreDepthHdrRender");
	manager->registerEvent("DepthHdrRender");
	manager->registerEvent("PreRefractedRender");
	manager->registerEvent("RefractedRender");
	manager->registerEvent("PreTranslucentRender");
	manager->registerEvent("TranslucentRender");
	manager->registerEvent("PreTransDepthRender");
	manager->registerEvent("TransDepthRender");
	manager->registerEvent("PreOitRender");
	manager->registerEvent("OitRender");
	manager->registerEvent("PreLdrRender");
	manager->registerEvent("LdrRender");
	manager->registerEvent("PreDepthLdrRender");
	manager->registerEvent("DepthLdrRender");
	manager->registerEvent("PostLdrToUI");
	manager->tryRegisterEvent("PreUiRender");
	manager->tryRegisterEvent("UiRender");
	manager->registerEvent("GBufferRecreate");

	ECSM_SUBSCRIBE_TO_EVENT("Init", DeferredRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", DeferredRenderSystem::deinit);
}
DeferredRenderSystem::~DeferredRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", DeferredRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", DeferredRenderSystem::deinit);

		auto manager = Manager::Instance::get();
		manager->unregisterEvent("PreDeferredRender");
		manager->unregisterEvent("DeferredRender");
		manager->unregisterEvent("PreHdrRender");
		manager->unregisterEvent("HdrRender");
		manager->unregisterEvent("PreDepthHdrRender");
		manager->unregisterEvent("DepthHdrRender");
		manager->unregisterEvent("PreRefractedRender");
		manager->unregisterEvent("RefractedRender");
		manager->unregisterEvent("PreTranslucentRender");
		manager->unregisterEvent("TranslucentRender");
		manager->unregisterEvent("PreTransDepthRender");
		manager->unregisterEvent("TransDepthRender");
		manager->unregisterEvent("PreOitRender");
		manager->unregisterEvent("OitRender");
		manager->unregisterEvent("PreLdrRender");
		manager->unregisterEvent("LdrRender");
		manager->unregisterEvent("PreDepthLdrRender");
		manager->unregisterEvent("DepthLdrRender");
		manager->unregisterEvent("PostLdrToUI");
		manager->tryUnregisterEvent("PreUiRender");
		manager->tryUnregisterEvent("UiRender");
		manager->unregisterEvent("GBufferRecreate");
	}

	unsetSingleton();
}

//**********************************************************************************************************************
void DeferredRenderSystem::init()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	GARDEN_ASSERT(options.useAsyncRecording == graphicsSystem->useAsyncRecording());

	ECSM_SUBSCRIBE_TO_EVENT("Render", DeferredRenderSystem::render);
	ECSM_SUBSCRIBE_TO_EVENT("SwapchainRecreate", DeferredRenderSystem::swapchainRecreate);
}
void DeferredRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(velocityDS);
		graphicsSystem->destroy(upscaleHdrFramebuffer);
		graphicsSystem->destroy(transDepthFramebuffer);
		graphicsSystem->destroy(oitFramebuffer);
		graphicsSystem->destroy(uiFramebuffer);
		graphicsSystem->destroy(depthLdrFramebuffer);
		graphicsSystem->destroy(ldrFramebuffer);
		graphicsSystem->destroy(depthHdrFramebuffer);
		graphicsSystem->destroy(hdrFramebuffer);
		graphicsSystem->destroy(gFramebuffer);
		graphicsSystem->destroy(hdrCopyIV);
		graphicsSystem->destroy(stencilImageView);
		graphicsSystem->destroy(depthImageView);
		graphicsSystem->destroy(depthCopyIV);
		graphicsSystem->destroy(depthStencilIV);
		if (upscaleHdrBuffer != hdrBuffer)
			graphicsSystem->destroy(upscaleHdrBuffer);
		graphicsSystem->destroy(transBuffer);
		graphicsSystem->destroy(depthCopyBuffer);
		graphicsSystem->destroy(depthStencilBuffer);
		graphicsSystem->destroy(oitRevealBuffer);
		graphicsSystem->destroy(oitAccumBuffer);
		if (uiBuffer != gBuffers[0])
			graphicsSystem->destroy(uiBuffer);
		graphicsSystem->destroy(ldrBuffer);
		graphicsSystem->destroy(hdrCopyBlurDSes);
		graphicsSystem->destroy(hdrCopyBlurFBs);
		graphicsSystem->destroy(hdrCopyBlurViews);
		graphicsSystem->destroy(hdrCopyBlurPipeline);
		graphicsSystem->destroy(velocityPipeline);
		graphicsSystem->destroy(hdrCopyBuffer);
		graphicsSystem->destroy(hdrBuffer);
		graphicsSystem->destroy(gBuffers);

		ECSM_UNSUBSCRIBE_FROM_EVENT("Render", DeferredRenderSystem::render);
		ECSM_UNSUBSCRIBE_FROM_EVENT("SwapchainRecreate", DeferredRenderSystem::swapchainRecreate);
	}
}

//**********************************************************************************************************************
void DeferredRenderSystem::render()
{
	SET_CPU_ZONE_SCOPED("Deferred Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!isEnabled || !graphicsSystem->canRender() || !graphicsSystem->camera)
		return;

	auto manager = Manager::Instance::get();
	auto cameraView = manager->tryGet<CameraComponent>(graphicsSystem->camera);
	auto transformView = manager->tryGet<TransformComponent>(graphicsSystem->camera);
	if (!cameraView || !transformView || !transformView->isActive())
		return;

	if (!velocityPipeline)
		velocityPipeline = createVelocityPipeline(getGFramebuffer(), options.useAsyncRecording);

	auto velocityPipelineView = graphicsSystem->get(velocityPipeline);
	if (!velocityPipelineView->isReady())
		return;

	#if GARDEN_DEBUG
	if (ForwardRenderSystem::Instance::tryGet())
	{
		GARDEN_ASSERT_MSG(!ForwardRenderSystem::Instance::get()->isEnabled, 
			"Can not use deferred and forward render system at the same time"); 
	}
	#endif

	auto event = &manager->getEvent("PreDeferredRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre Deferred Render");
		event->run();
	}
	event = &manager->getEvent("DeferredRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Deferred Render Pass");

		if (!velocityDS)
		{
			auto uniforms = getVelocityUniforms(graphicsSystem);
			velocityDS = graphicsSystem->createDescriptorSet(velocityPipeline, std::move(uniforms));
			SET_RESOURCE_DEBUG_NAME(velocityDS, "descriptorSet.deferred.velocity");
		}

		static const array<float4, gBufferCount> clearColors = 
		{ float4::zero, float4::zero, float4::zero, float4::zero, float4::zero, float4::zero };
		auto inFlightIndex = graphicsSystem->getInFlightIndex();

		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("Deferred Pass");
			{
				RenderPass renderPass(getGFramebuffer(), clearColors,
					0.0f, 0x00, int4::zero, options.useAsyncRecording);
				event->run();

				velocityPipelineView = graphicsSystem->get(velocityPipeline); // Note: do not move.
				if (options.useAsyncRecording)
				{
					auto threadIndex = graphicsSystem->getThreadCount() - 1;
					velocityPipelineView->bindAsync(0, threadIndex);
					velocityPipelineView->setViewportScissorAsync(float4::zero, threadIndex);
					velocityPipelineView->bindDescriptorSetAsync(velocityDS, inFlightIndex, threadIndex);
					velocityPipelineView->drawFullscreenAsync(threadIndex);
				}
				else
				{
					velocityPipelineView->bind();
					velocityPipelineView->setViewportScissor();
					velocityPipelineView->bindDescriptorSet(velocityDS, inFlightIndex);
					velocityPipelineView->drawFullscreen();
				}
			}
		}
		graphicsSystem->stopRecording();
	}

	event = &manager->getEvent("PreHdrRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre HDR Render");
		event->run();
	}
	event = &manager->getEvent("HdrRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("HDR Render Pass");
		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("HDR Pass");
			{
				RenderPass renderPass(getHdrFramebuffer(), float4::zero);
				event->run();
			}
		}
		graphicsSystem->stopRecording();
	}

	event = &manager->getEvent("PreDepthHdrRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre Depth HDR Render");
		event->run();
	}
	event = &manager->getEvent("DepthHdrRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Depth HDR Render Pass");
		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("Depth HDR Pass");
			{
				RenderPass renderPass(getDepthHdrFramebuffer(), float4::zero,
					0.0f, 0x00, int4::zero, options.useAsyncRecording);
				event->run();
			}
		}
		graphicsSystem->stopRecording();
	}

	event = &manager->getEvent("PreRefractedRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre Refracted Render");
		event->run();
	}
	event = &manager->getEvent("RefractedRender");
	if (hasAnyRefr && event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Refracted Render Pass");

		auto _hdrCopyBuffer = getHdrCopyBuffer();
		auto gpuProcessSystem = GpuProcessSystem::Instance::get();
		gpuProcessSystem->prepareGgxBlur(_hdrCopyBuffer, hdrCopyBlurViews, hdrCopyBlurFBs);
		
		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("Refracted Pass");
			{
				Image::copy(depthStencilBuffer, getDepthCopyBuffer());
				Image::copy(hdrBuffer, _hdrCopyBuffer);

				gpuProcessSystem->ggxBlur(_hdrCopyBuffer, hdrCopyBlurViews,
					hdrCopyBlurFBs, hdrCopyBlurPipeline, hdrCopyBlurDSes);

				RenderPass renderPass(getDepthHdrFramebuffer(), float4::zero,
					0.0f, 0x00, int4::zero, options.useAsyncRecording);
				event->run();
			}
		}
		graphicsSystem->stopRecording();
	}

	event = &manager->getEvent("PreTranslucentRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre Translucent Render");
		event->run();
	}
	event = &manager->getEvent("TranslucentRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Translucent Render Pass");
		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("Translucent Pass");
			{
				RenderPass renderPass(getDepthHdrFramebuffer(), float4::zero,
					0.0f, 0x00, int4::zero, options.useAsyncRecording);
				event->run();
			}
		}
		graphicsSystem->stopRecording();
	}

	event = &manager->getEvent("PreTransDepthRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre Translucent Depth Render");
		event->run();
	}
	event = &manager->getEvent("TransDepthRender");
	if (hasAnyTD && event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Translucent Depth Render Pass");
		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("Translucent Depth Pass");
			{
				Image::copy(depthStencilBuffer, getDepthCopyBuffer());
				RenderPass renderPass(getTransDepthFramebuffer(), float4::zero,
					0.0f, 0x00, int4::zero, options.useAsyncRecording);
				event->run();
			}
		}
		graphicsSystem->stopRecording();
	}

	event = &manager->getEvent("PreOitRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre OIT Render");
		event->run();
	}
	event = &manager->getEvent("OitRender");
	if (hasAnyOit && event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("OIT Render Pass");
		static const vector<float4> clearColors = { float4::zero, float4::one };

		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("OIT Pass");
			{
				RenderPass renderPass(getOitFramebuffer(), clearColors,
					0.0f, 0x00, int4::zero, options.useAsyncRecording);
				event->run();
			}
		}
		graphicsSystem->stopRecording();
	}

	event = &manager->getEvent("PreLdrRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre LDR Render");
		event->run();
	}
	event = &manager->getEvent("LdrRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("LDR Render Pass");
		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("LDR Pass");
			{
				RenderPass renderPass(getLdrFramebuffer(), float4::zero);
				event->run();
			}
		}
		graphicsSystem->stopRecording();
	}

	event = &manager->getEvent("PreDepthLdrRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre Depth LDR Render");
		event->run();
	}
	event = &manager->getEvent("DepthLdrRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Depth LDR Render Pass");
		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("Depth LDR Pass");
			{
				RenderPass renderPass(getDepthLdrFramebuffer(), float4::zero);
				event->run();
			}
		}
		graphicsSystem->stopRecording();
	}

	event = &manager->getEvent("PreUiRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre UI Render");
		event->run();
	}

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		SET_GPU_DEBUG_LABEL("Copy LDR to UI");
		auto _uiBuffer = getUiBuffer();
		if (_uiBuffer == gBuffers[0])
			Image::copy(ldrBuffer, _uiBuffer);
		else
			Image::blit(ldrBuffer, _uiBuffer, Sampler::Filter::Linear);
	}
	graphicsSystem->stopRecording();

	event = &manager->getEvent("PostLdrToUI");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Post LDR to UI Copy");
		event->run();
	}

	event = &manager->getEvent("UiRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("UI Render Pass");
		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("UI Pass");
			{
				RenderPass renderPass(getUiFramebuffer(), float4::zero,
					0.0f, 0x00, int4::zero, options.useAsyncRecording);
				event->run();
			}
		}
		graphicsSystem->stopRecording();
	}

	auto framebufferView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());
	const auto& colorAttachments = framebufferView->getColorAttachments();
	auto swapchainImageView = graphicsSystem->get(colorAttachments[0].imageView);

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		SET_GPU_DEBUG_LABEL("Copy UI to Swapchain");
		if (uiBufferFormat == swapchainImageView->getFormat())
			Image::copy(uiBuffer, swapchainImageView->getImage());
		else
			Image::blit(uiBuffer, swapchainImageView->getImage(), Sampler::Filter::Nearest);
	}
	graphicsSystem->stopRecording();

	hasAnyRefr = hasAnyOit = hasAnyTD = false;
}

//**********************************************************************************************************************
void DeferredRenderSystem::swapchainRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	const auto& swapchainChanges = graphicsSystem->getSwapchainChanges();

	if (swapchainChanges.framebufferSize)
	{
		if (uiBuffer != gBuffers[0])
			graphicsSystem->destroy(uiBuffer);
		if (upscaleHdrBuffer != hdrBuffer)
			graphicsSystem->destroy(upscaleHdrBuffer);

		graphicsSystem->destroy(hdrCopyIV);
		graphicsSystem->destroy(stencilImageView);
		graphicsSystem->destroy(depthImageView);
		graphicsSystem->destroy(depthCopyIV);
		graphicsSystem->destroy(depthStencilIV);
		depthStencilIV = depthCopyIV = depthImageView = stencilImageView = hdrCopyIV = {};

		graphicsSystem->destroy(hdrCopyBlurDSes);
		graphicsSystem->destroy(hdrCopyBlurFBs);
		graphicsSystem->destroy(hdrCopyBlurViews);
		hdrCopyBlurViews = {}; hdrCopyBlurFBs = {}; hdrCopyBlurDSes = {};
		
		graphicsSystem->destroy(transBuffer);
		graphicsSystem->destroy(depthCopyBuffer);
		graphicsSystem->destroy(depthStencilBuffer);
		graphicsSystem->destroy(oitRevealBuffer);
		graphicsSystem->destroy(oitAccumBuffer);
		graphicsSystem->destroy(ldrBuffer);
		graphicsSystem->destroy(hdrCopyBuffer);
		graphicsSystem->destroy(hdrBuffer);
		graphicsSystem->destroy(gBuffers);

		gBuffers = {}; hdrBuffer = hdrCopyBuffer = ldrBuffer = uiBuffer = oitAccumBuffer = 
			oitRevealBuffer = depthStencilBuffer = depthCopyBuffer = transBuffer = upscaleHdrBuffer = {};

		auto framebufferSize = graphicsSystem->getScaledFrameSize();
		Framebuffer::OutputAttachment colorAttachments[gBufferCount];
		Framebuffer::OutputAttachment depthStencilAttachment;
		depthStencilAttachment.imageView = getDepthStencilIV();

		if (gFramebuffer)
		{
			auto _gBuffers = getGBuffers();
			auto framebufferView = graphicsSystem->get(gFramebuffer);
			for (uint8 i = 0; i < DeferredRenderSystem::gBufferCount; i++)
			{
				if (!_gBuffers[i])
				{
					colorAttachments[i] = {};
					continue;
				}

				colorAttachments[i] = Framebuffer::OutputAttachment(graphicsSystem->get(
					_gBuffers[i])->getDefaultView(), DeferredRenderSystem::gBufferFlags);
			}
			depthStencilAttachment.setFlags(DeferredRenderSystem::gBufferDepthFlags);
			framebufferView->update(framebufferSize, colorAttachments, gBufferCount, depthStencilAttachment);
		}
		if (hdrFramebuffer)
		{
			auto framebufferView = graphicsSystem->get(hdrFramebuffer);
			colorAttachments[0] = Framebuffer::OutputAttachment(graphicsSystem->get(
				getHdrBuffer())->getDefaultView(), DeferredRenderSystem::hdrBufferFlags);
			framebufferView->update(framebufferSize, colorAttachments, 1);
		}
		if (depthHdrFramebuffer)
		{
			auto framebufferView = graphicsSystem->get(depthHdrFramebuffer);
			colorAttachments[0] = Framebuffer::OutputAttachment(graphicsSystem->get(
				getHdrBuffer())->getDefaultView(), DeferredRenderSystem::hdrBufferFlags);
			depthStencilAttachment.setFlags(DeferredRenderSystem::hdrBufferDepthFlags);
			framebufferView->update(framebufferSize, colorAttachments, 1, depthStencilAttachment);
		}
		if (ldrFramebuffer)
		{
			colorAttachments[0] = Framebuffer::OutputAttachment(graphicsSystem->get(
				getLdrBuffer())->getDefaultView(), DeferredRenderSystem::ldrBufferFlags);
			auto framebufferView = graphicsSystem->get(ldrFramebuffer);
			framebufferView->update(framebufferSize, colorAttachments, 1);
		}
		if (depthLdrFramebuffer)
		{
			auto framebufferView = graphicsSystem->get(depthLdrFramebuffer);
			colorAttachments[0] = Framebuffer::OutputAttachment(graphicsSystem->get(
				getLdrBuffer())->getDefaultView(), DeferredRenderSystem::ldrBufferFlags);
			depthStencilAttachment.setFlags(DeferredRenderSystem::ldrBufferDepthFlags);
			framebufferView->update(framebufferSize, colorAttachments, 1, depthStencilAttachment);
		}
		if (uiFramebuffer)
		{
			colorAttachments[0] = Framebuffer::OutputAttachment(graphicsSystem->get(
				getUiBuffer())->getDefaultView(), DeferredRenderSystem::uiBufferFlags);
			auto framebufferView = graphicsSystem->get(uiFramebuffer);
			framebufferView->update(graphicsSystem->getFramebufferSize(), colorAttachments, 1);
		}
		if (oitFramebuffer)
		{
			colorAttachments[0] = Framebuffer::OutputAttachment(graphicsSystem->get(
				getOitAccumBuffer())->getDefaultView(), DeferredRenderSystem::oitBufferFlags);
			colorAttachments[1] = Framebuffer::OutputAttachment(graphicsSystem->get(
				getOitRevealBuffer())->getDefaultView(), DeferredRenderSystem::oitBufferFlags);
			auto framebufferView = graphicsSystem->get(oitFramebuffer);
			depthStencilAttachment.setFlags(DeferredRenderSystem::oitBufferDepthFlags);
			framebufferView->update(framebufferSize, colorAttachments, 2, depthStencilAttachment);
		}
		if (transDepthFramebuffer)
		{
			colorAttachments[0] = Framebuffer::OutputAttachment(graphicsSystem->get(
				getTransBuffer())->getDefaultView(), DeferredRenderSystem::transBufferFlags);
			auto framebufferView = graphicsSystem->get(transDepthFramebuffer);
			depthStencilAttachment.setFlags(DeferredRenderSystem::transBufferDepthFlags);
			framebufferView->update(framebufferSize, colorAttachments, 1, depthStencilAttachment);
		}
		if (upscaleHdrFramebuffer)
		{
			colorAttachments[0] = Framebuffer::OutputAttachment(graphicsSystem->get(
				getUpscaleHdrBuffer())->getDefaultView(), DeferredRenderSystem::hdrBufferFlags);
			auto framebufferView = graphicsSystem->get(upscaleHdrFramebuffer);
			framebufferView->update(graphicsSystem->useUpscaling ? 
				graphicsSystem->getFramebufferSize() : framebufferSize, colorAttachments, 1);
		}
	}

	if (swapchainChanges.framebufferSize)
		Manager::Instance::get()->runEvent("GBufferRecreate");
}

void DeferredRenderSystem::setOptions(Options options)
{
	abort(); // TODO:
}

//**********************************************************************************************************************
const vector<ID<Image>>& DeferredRenderSystem::getGBuffers()
{
	if (gBuffers.empty())
		createGBuffers(GraphicsSystem::Instance::get(), gBuffers, options);
	return gBuffers;
}
ID<Image> DeferredRenderSystem::getHdrBuffer()
{
	if (!hdrBuffer)
		hdrBuffer = createHdrBuffer(GraphicsSystem::Instance::get(), false, false);
	return hdrBuffer;
}
ID<Image> DeferredRenderSystem::getHdrCopyBuffer()
{
	if (!hdrCopyBuffer)
		hdrCopyBuffer = createHdrBuffer(GraphicsSystem::Instance::get(), true, false);
	return hdrCopyBuffer;
}
ID<Image> DeferredRenderSystem::getLdrBuffer()
{
	if (!ldrBuffer)
		ldrBuffer = createLdrBuffer(GraphicsSystem::Instance::get());
	return ldrBuffer;
}
ID<Image> DeferredRenderSystem::getUiBuffer()
{
	if (!uiBuffer)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		uiBuffer = graphicsSystem->getScaledFrameSize() != graphicsSystem->getFramebufferSize() ? 
			createUiBuffer(graphicsSystem) : getGBuffers()[0];
	}
	return uiBuffer;
}
ID<Image> DeferredRenderSystem::getOitAccumBuffer()
{
	if (!oitAccumBuffer)
		oitAccumBuffer = createOitAccumBuffer(GraphicsSystem::Instance::get());
	return oitAccumBuffer;
}
ID<Image> DeferredRenderSystem::getOitRevealBuffer()
{
	if (!oitRevealBuffer)
		oitRevealBuffer = createOitRevealBuffer(GraphicsSystem::Instance::get());
	return oitRevealBuffer;
}
ID<Image> DeferredRenderSystem::getDepthStencilBuffer()
{
	if (!depthStencilBuffer)
		depthStencilBuffer = createDepthStencilBuffer(GraphicsSystem::Instance::get(), options.useStencil, false);
	return depthStencilBuffer;
}
ID<Image> DeferredRenderSystem::getDepthCopyBuffer()
{
	if (!depthCopyBuffer)
		depthCopyBuffer = createDepthStencilBuffer(GraphicsSystem::Instance::get(), options.useStencil, true);
	return depthCopyBuffer;
}
ID<Image> DeferredRenderSystem::getTransBuffer()
{
	if (!transBuffer)
		transBuffer = createTransBuffer(GraphicsSystem::Instance::get());
	return transBuffer;
}
ID<Image> DeferredRenderSystem::getUpscaleHdrBuffer()
{
	if (!upscaleHdrBuffer)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		upscaleHdrBuffer = graphicsSystem->useUpscaling ? 
			createHdrBuffer(graphicsSystem, false, true) : getHdrBuffer();
	}
	return upscaleHdrBuffer;
}

ID<ImageView> DeferredRenderSystem::getDepthStencilIV()
{
	if (!depthStencilIV)
	{
		depthStencilIV = createDepthStencilIV(GraphicsSystem::Instance::get(), 
			getDepthStencilBuffer(), options.useStencil);
	}
	return depthStencilIV;
}
ID<ImageView> DeferredRenderSystem::getDepthCopyIV()
{
	if (!depthCopyIV)
		depthCopyIV = createDepthCopyIV(GraphicsSystem::Instance::get(), getDepthCopyBuffer());
	return depthCopyIV;
}
ID<ImageView> DeferredRenderSystem::getDepthImageView()
{
	if (!depthImageView)
		depthImageView = createDepthImageView(GraphicsSystem::Instance::get(), getDepthStencilBuffer());
	return depthImageView;
}
ID<ImageView> DeferredRenderSystem::getStencilImageView()
{
	if (!stencilImageView)
		stencilImageView = createStencilImageView(GraphicsSystem::Instance::get(), getDepthStencilBuffer());
	return stencilImageView;
}
ID<ImageView> DeferredRenderSystem::getHdrCopyIV()
{
	if (!hdrCopyIV)
		hdrCopyIV = createHdrCopyIV(GraphicsSystem::Instance::get(), getHdrCopyBuffer());
	return hdrCopyIV;
}
const vector<ID<ImageView>>& DeferredRenderSystem::getHdrCopyBlurViews()
{
	if (hdrCopyBlurViews.empty())
		GpuProcessSystem::Instance::get()->prepareGgxBlur(getHdrCopyBuffer(), hdrCopyBlurViews, hdrCopyBlurFBs);
	return hdrCopyBlurViews;
}

ID<Framebuffer> DeferredRenderSystem::getGFramebuffer()
{
	if (!gFramebuffer)
		gFramebuffer = createGFramebuffer(GraphicsSystem::Instance::get(), getGBuffers(), getDepthStencilIV());
	return gFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getHdrFramebuffer()
{
	if (!hdrFramebuffer)
		hdrFramebuffer = createHdrFramebuffer(GraphicsSystem::Instance::get(),getHdrBuffer(), false);
	return hdrFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getDepthHdrFramebuffer()
{
	if (!depthHdrFramebuffer)
	{
		depthHdrFramebuffer = createDepthHdrFramebuffer(GraphicsSystem::Instance::get(), 
			getHdrBuffer(), getDepthStencilIV());
	}
	return depthHdrFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getLdrFramebuffer()
{
	if (!ldrFramebuffer)
		ldrFramebuffer = createLdrFramebuffer(GraphicsSystem::Instance::get(),getLdrBuffer());
	return ldrFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getDepthLdrFramebuffer()
{
	if (!depthLdrFramebuffer)
	{
		depthLdrFramebuffer = createDepthLdrFramebuffer(GraphicsSystem::Instance::get(), 
			getLdrBuffer(), getDepthStencilIV());
	}
	return depthLdrFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getUiFramebuffer()
{
	if (!uiFramebuffer)
		uiFramebuffer = createUiFramebuffer(GraphicsSystem::Instance::get(), getUiBuffer());
	return uiFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getOitFramebuffer()
{
	if (!oitFramebuffer)
	{
		oitFramebuffer = createOitFramebuffer(GraphicsSystem::Instance::get(), 
			getOitAccumBuffer(), getOitRevealBuffer(), getDepthStencilIV());
	}
	return oitFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getTransDepthFramebuffer()
{
	if (!transDepthFramebuffer)
	{
		transDepthFramebuffer = createTransDepthFramebuffer(GraphicsSystem::Instance::get(), 
			getTransBuffer(), getDepthStencilIV());
	}
	return transDepthFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getUpscaleHdrFramebuffer()
{
	if (!upscaleHdrFramebuffer)
		upscaleHdrFramebuffer = createHdrFramebuffer(GraphicsSystem::Instance::get(), getUpscaleHdrBuffer(), true);
	return upscaleHdrFramebuffer;
}
const vector<ID<Framebuffer>>& DeferredRenderSystem::getHdrCopyBlurFBs()
{
	if (hdrCopyBlurFBs.empty())
		GpuProcessSystem::Instance::get()->prepareGgxBlur(getHdrCopyBuffer(), hdrCopyBlurViews, hdrCopyBlurFBs);
	return hdrCopyBlurFBs;
}