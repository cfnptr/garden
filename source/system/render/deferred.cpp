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
#include "garden/profiler.hpp"
#include "math/brdf.hpp"

// TODO: allow to disable OIT and other buffers creation/usage.

using namespace garden;

//**********************************************************************************************************************
static void createGBuffers(vector<ID<Image>>& gBuffers, const DeferredRenderSystem::Options& options)
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
	auto graphicsSystem = GraphicsSystem::Instance::get();
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

static ID<Image> createDepthStencilBuffer(bool useStencil, bool isCopy)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto image = graphicsSystem->createImage(useStencil ? DeferredRenderSystem::depthStencilFormat :
		DeferredRenderSystem::depthFormat, Image::Usage::DepthStencilAttachment | 
		Image::Usage::Sampled | Image::Usage::Fullscreen | Image::Usage::TransferSrc | Image::Usage::TransferDst, 
		{ { nullptr } }, graphicsSystem->getScaledFrameSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.depthStencil" + string(isCopy ? "Copy" : ""));
	return image;
}

static ID<Image> createHdrBuffer(bool isCopy, bool isFullSize)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
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
static ID<Image> createLdrBuffer()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto image = graphicsSystem->createImage(DeferredRenderSystem::ldrBufferFormat, Image::Usage::ColorAttachment | 
		Image::Usage::Sampled | Image::Usage::Fullscreen | Image::Usage::TransferSrc | Image::Usage::TransferDst,
		{ { nullptr } }, graphicsSystem->getScaledFrameSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.ldr");
	return image;
}
static ID<Image> createUiBuffer()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto image = graphicsSystem->createImage(DeferredRenderSystem::uiBufferFormat, Image::Usage::ColorAttachment | 
		Image::Usage::Sampled | Image::Usage::Fullscreen | Image::Usage::TransferSrc | Image::Usage::TransferDst,
		{ { nullptr } }, graphicsSystem->getFramebufferSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.ui");
	return image;
}

static ID<Image> createOitAccumBuffer()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto image = graphicsSystem->createImage(DeferredRenderSystem::oitAccumBufferFormat, 
		Image::Usage::ColorAttachment | Image::Usage::Sampled | Image::Usage::Fullscreen, 
		{ { nullptr } }, graphicsSystem->getScaledFrameSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.oitAccum");
	return image;
}
static ID<Image> createOitRevealBuffer()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto image = graphicsSystem->createImage(DeferredRenderSystem::oitRevealBufferFormat, 
		Image::Usage::ColorAttachment | Image::Usage::Sampled | Image::Usage::Fullscreen, 
		{ { nullptr } }, graphicsSystem->getScaledFrameSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.oitReveal");
	return image;
}

static ID<Image> createTransBuffer()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto image = graphicsSystem->createImage(DeferredRenderSystem::transBufferFormat, 
		Image::Usage::ColorAttachment | Image::Usage::Sampled | Image::Usage::Fullscreen, 
		{ { nullptr } }, graphicsSystem->getScaledFrameSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.trans");
	return image;
}

static ID<ImageView> createDepthStencilIV(ID<Image> depthStencilBuffer, bool useStencil)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto imageView = graphicsSystem->createImageView(depthStencilBuffer, Image::Type::Texture2D, 
		useStencil ? DeferredRenderSystem::depthStencilFormat : DeferredRenderSystem::depthFormat);
	SET_RESOURCE_DEBUG_NAME(imageView, "imageView.deferred.depthStencil");
	return imageView;
}
static ID<ImageView> createDepthCopyIV(ID<Image> depthStencilBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto imageView = graphicsSystem->createImageView(depthStencilBuffer, 
		Image::Type::Texture2D, DeferredRenderSystem::depthFormat);
	SET_RESOURCE_DEBUG_NAME(imageView, "imageView.deferred.depthCopy");
	return imageView;
}
static ID<ImageView> createDepthImageView(ID<Image> depthStencilBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto bufferView = graphicsSystem->get(depthStencilBuffer);
	if (isFormatStencilOnly(bufferView->getFormat()))
		return {};

	auto imageView = graphicsSystem->createImageView(depthStencilBuffer, 
		Image::Type::Texture2D, DeferredRenderSystem::depthFormat);
	SET_RESOURCE_DEBUG_NAME(imageView, "imageView.deferred.depth");
	return imageView;
}
static ID<ImageView> createStencilImageView(ID<Image> depthStencilBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto bufferView = graphicsSystem->get(depthStencilBuffer);
	if (isFormatDepthOnly(bufferView->getFormat()))
		return {};

	auto imageView = graphicsSystem->createImageView(depthStencilBuffer, 
		Image::Type::Texture2D, DeferredRenderSystem::stencilFormat);
	SET_RESOURCE_DEBUG_NAME(imageView, "imageView.deferred.stencil");
	return imageView;
}
static ID<ImageView> createHdrCopyIV(ID<Image> hdrCopyBuffer)
{
	auto imageView = GraphicsSystem::Instance::get()->createImageView(hdrCopyBuffer, 
		Image::Type::Texture2D, DeferredRenderSystem::hdrBufferFormat, 0, 0, 0, 1);
	SET_RESOURCE_DEBUG_NAME(imageView, "imageView.deferred.hdrCopy");
	return imageView;
}

//**********************************************************************************************************************
static ID<Framebuffer> createGFramebuffer(const vector<ID<Image>> gBuffers, ID<ImageView> depthStencilIV)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
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

	colorAttachments[DeferredRenderSystem::gBufferVelocity].flags = DeferredRenderSystem::velocityFlags;
	Framebuffer::OutputAttachment depthStencilAttachment(
		depthStencilIV, DeferredRenderSystem::gBufferDepthFlags);
	auto framebuffer = graphicsSystem->createFramebuffer(graphicsSystem->getScaledFrameSize(), 
		std::move(colorAttachments), depthStencilAttachment); 
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.gBuffer");
	return framebuffer;
}

static ID<Framebuffer> createHdrFramebuffer(ID<Image> hdrBuffer, bool isFullSize)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto hdrBufferView = graphicsSystem->get(hdrBuffer)->getDefaultView();

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(hdrBufferView, DeferredRenderSystem::hdrBufferFlags) };

	auto framebufferSize = isFullSize ? 
		graphicsSystem->getFramebufferSize() : graphicsSystem->getScaledFrameSize();
	auto framebuffer = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.hdr" + string(isFullSize ? "Full" : ""));
	return framebuffer;
}
static ID<Framebuffer> createDepthHdrFramebuffer(ID<Image> hdrBuffer, ID<ImageView> depthStencilIV)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
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
static ID<Framebuffer> createLdrFramebuffer(ID<Image> ldrBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto ldrBufferView = graphicsSystem->get(ldrBuffer)->getDefaultView();

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(ldrBufferView, DeferredRenderSystem::ldrBufferFlags) };

	auto framebuffer = graphicsSystem->createFramebuffer(
		graphicsSystem->getScaledFrameSize(), std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.ldr");
	return framebuffer;
}
static ID<Framebuffer> createDepthLdrFramebuffer(ID<Image> ldrBuffer, ID<ImageView> depthStencilIV)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
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
static ID<Framebuffer> createUiFramebuffer(ID<Image> uiBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto uiBufferView = graphicsSystem->get(uiBuffer)->getDefaultView();

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(uiBufferView, DeferredRenderSystem::uiBufferFlags) };

	auto framebuffer = graphicsSystem->createFramebuffer(
		graphicsSystem->getFramebufferSize(), std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.ui");
	return framebuffer;
}
static ID<Framebuffer> createOitFramebuffer(ID<Image> oitAccumBuffer, 
	ID<Image> oitRevealBuffer, ID<ImageView> depthStencilIV)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
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
static ID<Framebuffer> createTransDepthFramebuffer(ID<Image> transBuffer, ID<ImageView> depthStencilIV)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
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
		if (upscaleHdrFramebuffer != hdrFramebuffer)
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

	if (!isEnabled || !GraphicsSystem::Instance::get()->canRender())
		return;
	
	auto manager = Manager::Instance::get();
	auto graphicsSystem = GraphicsSystem::Instance::get();

	if (graphicsSystem->useUpscaling)
	{
		if (upscaleHdrFramebuffer == hdrFramebuffer)
		{
			upscaleHdrFramebuffer = {}; upscaleHdrBuffer = {};
			upscaleHdrFramebuffer = getUpscaleHdrFramebuffer();
		}
	}
	else
	{
		if (upscaleHdrFramebuffer != hdrFramebuffer)
		{
			graphicsSystem->destroy(upscaleHdrFramebuffer);
			graphicsSystem->destroy(upscaleHdrBuffer);
			upscaleHdrFramebuffer = {}; upscaleHdrBuffer = {};
			upscaleHdrFramebuffer = getUpscaleHdrFramebuffer();
		}
	}

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
		static const array<float4, gBufferCount> clearColors = 
		{ float4::zero, float4::zero, float4::zero, float4::zero, float4::zero, float4::zero };
		auto framebufferView = graphicsSystem->get(getGFramebuffer());

		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("Deferred Pass", Color::transparent);
			framebufferView->beginRenderPass(clearColors, 0.0f, 0, int4::zero, options.useAsyncRecording);
			event->run();
			framebufferView->endRenderPass();
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
		auto framebufferView = graphicsSystem->get(getHdrFramebuffer());

		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("HDR Pass", Color::transparent);
			framebufferView->beginRenderPass(float4::zero);
			event->run();
			framebufferView->endRenderPass();
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
		auto framebufferView = graphicsSystem->get(getDepthHdrFramebuffer());

		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("Depth HDR Pass", Color::transparent);
			framebufferView->beginRenderPass(float4::zero, 0.0f, 0, int4::zero, options.useAsyncRecording);
			event->run();
			framebufferView->endRenderPass();
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
		auto framebufferView = graphicsSystem->get(getDepthHdrFramebuffer());
		
		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("Refracted Pass", Color::transparent);

			Image::copy(depthStencilBuffer, getDepthCopyBuffer());
			Image::copy(hdrBuffer, _hdrCopyBuffer);

			gpuProcessSystem->ggxBlur(_hdrCopyBuffer, hdrCopyBlurViews, 
				hdrCopyBlurFBs, hdrCopyBlurPipeline, hdrCopyBlurDSes);

			framebufferView->beginRenderPass(float4::zero, 0.0f, 0, int4::zero, options.useAsyncRecording);
			event->run();
			framebufferView->endRenderPass();
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
		auto framebufferView = graphicsSystem->get(getDepthHdrFramebuffer());

		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("Translucent Pass", Color::transparent);
			framebufferView->beginRenderPass(float4::zero, 0.0f, 0, int4::zero, options.useAsyncRecording);
			event->run();
			framebufferView->endRenderPass();
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
		auto framebufferView = graphicsSystem->get(getTransDepthFramebuffer());

		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("Translucent Depth Pass", Color::transparent);
			Image::copy(depthStencilBuffer, getDepthCopyBuffer());
			framebufferView->beginRenderPass(float4::zero, 0.0f, 0, int4::zero, options.useAsyncRecording);
			event->run();
			framebufferView->endRenderPass();
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
		auto framebufferView = graphicsSystem->get(getOitFramebuffer());
		static const vector<float4> clearColors = { float4::zero, float4::one };

		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("OIT Pass", Color::transparent);
			framebufferView->beginRenderPass(clearColors, 0.0f, 0, int4::zero, options.useAsyncRecording);
			event->run();
			framebufferView->endRenderPass();
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
		auto framebufferView = graphicsSystem->get(getLdrFramebuffer());

		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("LDR Pass", Color::transparent);
			framebufferView->beginRenderPass(float4::zero);
			event->run();
			framebufferView->endRenderPass();
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
		auto framebufferView = graphicsSystem->get(getDepthLdrFramebuffer());

		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("Depth LDR Pass", Color::transparent);
			framebufferView->beginRenderPass(float4::zero);
			event->run();
			framebufferView->endRenderPass();
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
		SET_GPU_DEBUG_LABEL("Copy LDR to UI", Color::transparent);

		auto _uiBuffer = getUiBuffer();
		if (_uiBuffer == gBuffers[0])
			Image::copy(ldrBuffer, _uiBuffer);
		else
			Image::blit(ldrBuffer, _uiBuffer, Sampler::Filter::Linear);
	}
	graphicsSystem->stopRecording();

	event = &manager->getEvent("UiRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("UI Render Pass");
		auto framebufferView = graphicsSystem->get(getUiFramebuffer());

		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("UI Pass", Color::transparent);
			framebufferView->beginRenderPass(float4::zero);
			event->run();
			framebufferView->endRenderPass();
		}
		graphicsSystem->stopRecording();
	}

	auto framebufferView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());
	const auto& colorAttachments = framebufferView->getColorAttachments();
	auto swapchainImageView = graphicsSystem->get(colorAttachments[0].imageView);

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		SET_GPU_DEBUG_LABEL("Copy UI to Swapchain", Color::transparent);
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
			colorAttachments[DeferredRenderSystem::gBufferVelocity].flags = DeferredRenderSystem::velocityFlags;
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
		if (upscaleHdrFramebuffer && upscaleHdrFramebuffer != hdrFramebuffer)
		{
			colorAttachments[0] = Framebuffer::OutputAttachment(graphicsSystem->get(
				getUpscaleHdrBuffer())->getDefaultView(), DeferredRenderSystem::upscaleHdrFlags);
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
		createGBuffers(gBuffers, options);
	return gBuffers;
}
ID<Image> DeferredRenderSystem::getHdrBuffer()
{
	if (!hdrBuffer)
		hdrBuffer = createHdrBuffer(false, false);
	return hdrBuffer;
}
ID<Image> DeferredRenderSystem::getHdrCopyBuffer()
{
	if (!hdrCopyBuffer)
		hdrCopyBuffer = createHdrBuffer(true, false);
	return hdrCopyBuffer;
}
ID<Image> DeferredRenderSystem::getLdrBuffer()
{
	if (!ldrBuffer)
		ldrBuffer = createLdrBuffer();
	return ldrBuffer;
}
ID<Image> DeferredRenderSystem::getUiBuffer()
{
	if (!uiBuffer)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		uiBuffer = graphicsSystem->getScaledFrameSize() != 
			graphicsSystem->getFramebufferSize() ? createUiBuffer() : getGBuffers()[0];
	}
	return uiBuffer;
}
ID<Image> DeferredRenderSystem::getOitAccumBuffer()
{
	if (!oitAccumBuffer)
		oitAccumBuffer = createOitAccumBuffer();
	return oitAccumBuffer;
}
ID<Image> DeferredRenderSystem::getOitRevealBuffer()
{
	if (!oitRevealBuffer)
		oitRevealBuffer = createOitRevealBuffer();
	return oitRevealBuffer;
}
ID<Image> DeferredRenderSystem::getDepthStencilBuffer()
{
	if (!depthStencilBuffer)
		depthStencilBuffer = createDepthStencilBuffer(options.useStencil, false);
	return depthStencilBuffer;
}
ID<Image> DeferredRenderSystem::getDepthCopyBuffer()
{
	if (!depthCopyBuffer)
		depthCopyBuffer = createDepthStencilBuffer(options.useStencil, true);
	return depthCopyBuffer;
}
ID<Image> DeferredRenderSystem::getTransBuffer()
{
	if (!transBuffer)
		transBuffer = createTransBuffer();
	return transBuffer;
}
ID<Image> DeferredRenderSystem::getUpscaleHdrBuffer()
{
	if (!upscaleHdrBuffer)
	{
		upscaleHdrBuffer = GraphicsSystem::Instance::get()->useUpscaling ? 
			createHdrBuffer(false, true) : getHdrBuffer();
	}
	return upscaleHdrBuffer;
}

ID<ImageView> DeferredRenderSystem::getDepthStencilIV()
{
	if (!depthStencilIV)
		depthStencilIV = createDepthStencilIV(getDepthStencilBuffer(), options.useStencil);
	return depthStencilIV;
}
ID<ImageView> DeferredRenderSystem::getDepthCopyIV()
{
	if (!depthCopyIV)
		depthCopyIV = createDepthCopyIV(getDepthCopyBuffer());
	return depthCopyIV;
}
ID<ImageView> DeferredRenderSystem::getDepthImageView()
{
	if (!depthImageView)
		depthImageView = createDepthImageView(getDepthStencilBuffer());
	return depthImageView;
}
ID<ImageView> DeferredRenderSystem::getStencilImageView()
{
	if (!stencilImageView)
		stencilImageView = createStencilImageView(getDepthStencilBuffer());
	return stencilImageView;
}
ID<ImageView> DeferredRenderSystem::getHdrCopyIV()
{
	if (!hdrCopyIV)
		hdrCopyIV = createHdrCopyIV(getHdrCopyBuffer());
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
		gFramebuffer = createGFramebuffer(getGBuffers(), getDepthStencilIV());
	return gFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getHdrFramebuffer()
{
	if (!hdrFramebuffer)
		hdrFramebuffer = createHdrFramebuffer(getHdrBuffer(), false);
	return hdrFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getDepthHdrFramebuffer()
{
	if (!depthHdrFramebuffer)
		depthHdrFramebuffer = createDepthHdrFramebuffer(getHdrBuffer(), getDepthStencilIV());
	return depthHdrFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getLdrFramebuffer()
{
	if (!ldrFramebuffer)
		ldrFramebuffer = createLdrFramebuffer(getLdrBuffer());
	return ldrFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getDepthLdrFramebuffer()
{
	if (!depthLdrFramebuffer)
		depthLdrFramebuffer = createDepthLdrFramebuffer(getLdrBuffer(), getDepthStencilIV());
	return depthLdrFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getUiFramebuffer()
{
	if (!uiFramebuffer)
		uiFramebuffer = createUiFramebuffer(getUiBuffer());
	return uiFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getOitFramebuffer()
{
	if (!oitFramebuffer)
		oitFramebuffer = createOitFramebuffer(getOitAccumBuffer(), getOitRevealBuffer(), getDepthStencilIV());
	return oitFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getTransDepthFramebuffer()
{
	if (!transDepthFramebuffer)
		transDepthFramebuffer = createTransDepthFramebuffer(getTransBuffer(), getDepthStencilIV());
	return transDepthFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getUpscaleHdrFramebuffer()
{
	if (!upscaleHdrFramebuffer)
	{
		upscaleHdrFramebuffer = GraphicsSystem::Instance::get()->useUpscaling ? 
			createHdrFramebuffer(getUpscaleHdrBuffer(), true) : getHdrFramebuffer();
	}
	return upscaleHdrFramebuffer;
}
const vector<ID<Framebuffer>>& DeferredRenderSystem::getHdrCopyBlurFBs()
{
	if (hdrCopyBlurFBs.empty())
		GpuProcessSystem::Instance::get()->prepareGgxBlur(getHdrCopyBuffer(), hdrCopyBlurViews, hdrCopyBlurFBs);
	return hdrCopyBlurFBs;
}