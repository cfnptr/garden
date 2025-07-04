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

#include "garden/system/render/deferred.hpp"
#include "garden/system/render/forward.hpp"
#include "garden/profiler.hpp"

// TODO: allow to disable OIT and other buffers creation/usage.

using namespace garden;

//**********************************************************************************************************************
static void createGBuffers(vector<ID<Image>>& gBuffers, bool useEmission, bool useGI)
{
	constexpr auto usage = Image::Usage::ColorAttachment | Image::Usage::Sampled | 
		Image::Usage::TransferSrc | Image::Usage::TransferDst | Image::Usage::Fullscreen;
	constexpr auto strategy = Image::Strategy::Size;
	Image::Format formats[DeferredRenderSystem::gBufferCount]
	{
		DeferredRenderSystem::gBufferFormat0,
		DeferredRenderSystem::gBufferFormat1,
		DeferredRenderSystem::gBufferFormat2,
		DeferredRenderSystem::gBufferFormat3,
		useEmission ? DeferredRenderSystem::gBufferFormat4 : Image::Format::Undefined,
		useGI ? DeferredRenderSystem::gBufferFormat5 : Image::Format::Undefined,
	};

	const Image::Mips mips = { { nullptr } };
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
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

static ID<Image> createDepthStencilBuffer(bool isCopy)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto image = graphicsSystem->createImage(
		DeferredRenderSystem::depthStencilFormat, Image::Usage::DepthStencilAttachment | 
		Image::Usage::Sampled | Image::Usage::Fullscreen | Image::Usage::TransferSrc | Image::Usage::TransferDst, 
		{ { nullptr } }, graphicsSystem->getScaledFramebufferSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.depthStencil" + string(isCopy ? "Copy" : ""));
	return image;
}

static ID<Image> createHdrBuffer(bool isCopy)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto image = graphicsSystem->createImage(DeferredRenderSystem::hdrBufferFormat, Image::Usage::ColorAttachment | 
		Image::Usage::Sampled | Image::Usage::Fullscreen | Image::Usage::TransferSrc | Image::Usage::TransferDst, 
		{ { nullptr } }, graphicsSystem->getScaledFramebufferSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.hdr" + string(isCopy ? "Copy" : ""));
	return image;
}
static ID<Image> createLdrBuffer()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto image = graphicsSystem->createImage(DeferredRenderSystem::ldrBufferFormat, Image::Usage::ColorAttachment | 
		Image::Usage::Sampled | Image::Usage::Fullscreen | Image::Usage::TransferSrc | Image::Usage::TransferDst,
		{ { nullptr } }, graphicsSystem->getScaledFramebufferSize(), Image::Strategy::Size);
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
		{ { nullptr } }, graphicsSystem->getScaledFramebufferSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.oitAccum");
	return image;
}
static ID<Image> createOitRevealBuffer()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto image = graphicsSystem->createImage(DeferredRenderSystem::oitRevealBufferFormat, 
		Image::Usage::ColorAttachment | Image::Usage::Sampled | Image::Usage::Fullscreen, 
		{ { nullptr } }, graphicsSystem->getScaledFramebufferSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.oitReveal");
	return image;
}

//**********************************************************************************************************************
static ID<Framebuffer> createGFramebuffer(const vector<ID<Image>> gBuffers, ID<Image> depthStencilBuffer)
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

		auto gBufferView = graphicsSystem->get(gBuffers[i]);
		colorAttachments[i] = Framebuffer::OutputAttachment(
			gBufferView->getDefaultView(), DeferredRenderSystem::gBufferFlags);
	}

	auto depthStencilBufferView = graphicsSystem->get(depthStencilBuffer);
	Framebuffer::OutputAttachment depthStencilAttachment(
		depthStencilBufferView->getDefaultView(), DeferredRenderSystem::gBufferDepthFlags);
	auto framebuffer = graphicsSystem->createFramebuffer(graphicsSystem->getScaledFramebufferSize(), 
		std::move(colorAttachments), depthStencilAttachment); 
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.gBuffer");
	return framebuffer;
}

static ID<Framebuffer> createHdrFramebuffer(ID<Image> hdrBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto hdrBufferView = graphicsSystem->get(hdrBuffer);

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(hdrBufferView->getDefaultView(), DeferredRenderSystem::hdrBufferFlags) };

	auto framebuffer = graphicsSystem->createFramebuffer(
		graphicsSystem->getScaledFramebufferSize(), std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.hdr");
	return framebuffer;
}
static ID<Framebuffer> createDepthHdrFramebuffer(ID<Image> hdrBuffer, ID<Image> depthStencilBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto hdrBufferView = graphicsSystem->get(hdrBuffer);
	auto depthStencilBufferView = graphicsSystem->get(depthStencilBuffer);

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(hdrBufferView->getDefaultView(), DeferredRenderSystem::hdrBufferFlags) };
	Framebuffer::OutputAttachment depthStencilAttachment(
		depthStencilBufferView->getDefaultView(), DeferredRenderSystem::hdrBufferDepthFlags);

	auto framebuffer = graphicsSystem->createFramebuffer(graphicsSystem->getScaledFramebufferSize(), 
		std::move(colorAttachments), depthStencilAttachment);
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.depthHdr");
	return framebuffer;
}
static ID<Framebuffer> createLdrFramebuffer(ID<Image> ldrBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto ldrBufferView = graphicsSystem->get(ldrBuffer);

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(ldrBufferView->getDefaultView(), DeferredRenderSystem::ldrBufferFlags) };

	auto framebuffer = graphicsSystem->createFramebuffer(
		graphicsSystem->getScaledFramebufferSize(), std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.ldr");
	return framebuffer;
}
static ID<Framebuffer> createDepthLdrFramebuffer(ID<Image> ldrBuffer, ID<Image> depthStencilBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto ldrBufferView = graphicsSystem->get(ldrBuffer);
	auto depthStencilBufferView = graphicsSystem->get(depthStencilBuffer);

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(ldrBufferView->getDefaultView(), DeferredRenderSystem::ldrBufferFlags) };
	Framebuffer::OutputAttachment depthStencilAttachment(
		depthStencilBufferView->getDefaultView(), DeferredRenderSystem::ldrBufferDepthFlags);

	auto framebuffer = graphicsSystem->createFramebuffer(graphicsSystem->getScaledFramebufferSize(), 
		std::move(colorAttachments), depthStencilAttachment);
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.depthLdr");
	return framebuffer;
}
static ID<Framebuffer> createUiFramebuffer(ID<Image> uiBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto uiBufferView = graphicsSystem->get(uiBuffer);

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(uiBufferView->getDefaultView(), DeferredRenderSystem::uiBufferFlags) };

	auto framebuffer = graphicsSystem->createFramebuffer(
		graphicsSystem->getFramebufferSize(), std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.ui");
	return framebuffer;
}
static ID<Framebuffer> createRefractedFramebuffer(ID<Image> hdrBuffer, 
	ID<Image> normalsBuffer, ID<Image> depthStencilBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto hdrBufferView = graphicsSystem->get(hdrBuffer);
	auto normalsBufferView = graphicsSystem->get(normalsBuffer);
	auto depthStencilBufferView = graphicsSystem->get(depthStencilBuffer);

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{
		Framebuffer::OutputAttachment(hdrBufferView->getDefaultView(), DeferredRenderSystem::hdrBufferFlags),
		Framebuffer::OutputAttachment(normalsBufferView->getDefaultView(), DeferredRenderSystem::normalsBufferFlags),
	};

	Framebuffer::OutputAttachment depthStencilAttachment(
		depthStencilBufferView->getDefaultView(), DeferredRenderSystem::hdrBufferDepthFlags);

	auto framebuffer = graphicsSystem->createFramebuffer(graphicsSystem->getScaledFramebufferSize(), 
		std::move(colorAttachments), depthStencilAttachment);
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.refracted");
	return framebuffer;
}
static ID<Framebuffer> createOitFramebuffer(ID<Image> oitAccumBuffer, 
	ID<Image> oitRevealBuffer, ID<Image> depthStencilBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto oitAccumBufferView = graphicsSystem->get(oitAccumBuffer);
	auto oitRevealBufferView = graphicsSystem->get(oitRevealBuffer);
	auto depthStencilBufferView = graphicsSystem->get(depthStencilBuffer);

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{
		Framebuffer::OutputAttachment(oitAccumBufferView->getDefaultView(), DeferredRenderSystem::oitBufferFlags),
		Framebuffer::OutputAttachment(oitRevealBufferView->getDefaultView(), DeferredRenderSystem::oitBufferFlags)
	};
	Framebuffer::OutputAttachment depthStencilAttachment(
		depthStencilBufferView->getDefaultView(), DeferredRenderSystem::oitBufferDepthFlags);

	auto framebuffer = graphicsSystem->createFramebuffer(graphicsSystem->getScaledFramebufferSize(), 
		std::move(colorAttachments), depthStencilAttachment);
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.oit");
	return framebuffer;
}

//**********************************************************************************************************************
DeferredRenderSystem::DeferredRenderSystem(bool useEmission, bool useGI, bool useAsyncRecording, bool setSingleton) : 
	Singleton(setSingleton), emission(useEmission), gi(useGI), asyncRecording(useAsyncRecording)
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
	GARDEN_ASSERT(asyncRecording == graphicsSystem->useAsyncRecording());

	ECSM_SUBSCRIBE_TO_EVENT("Render", DeferredRenderSystem::render);
	ECSM_SUBSCRIBE_TO_EVENT("SwapchainRecreate", DeferredRenderSystem::swapchainRecreate);

	if (gBuffers.empty())
		createGBuffers(gBuffers, emission, gi);
	if (!hdrBuffer)
		hdrBuffer = createHdrBuffer(false);
	if (!ldrBuffer)
		ldrBuffer = createLdrBuffer();
	if (!uiBuffer)
		uiBuffer = graphicsSystem->getRenderScale() != 1.0f ? createUiBuffer() : gBuffers[0];
	if (!depthStencilBuffer)
		depthStencilBuffer = createDepthStencilBuffer(false);
	
	if (!gFramebuffer)
		gFramebuffer = createGFramebuffer(gBuffers, depthStencilBuffer);
	if (!hdrFramebuffer)
		hdrFramebuffer = createHdrFramebuffer(hdrBuffer);
	if (!depthHdrFramebuffer)
		depthHdrFramebuffer = createDepthHdrFramebuffer(hdrBuffer, depthStencilBuffer);
	if (!ldrFramebuffer)
		ldrFramebuffer = createLdrFramebuffer(ldrBuffer);
	if (!depthLdrFramebuffer)
		depthLdrFramebuffer = createDepthLdrFramebuffer(ldrBuffer, depthStencilBuffer);
	if (!uiFramebuffer)
		uiFramebuffer = createUiFramebuffer(uiBuffer);
	if (!refractedFramebuffer)
		refractedFramebuffer = createRefractedFramebuffer(hdrBuffer, gBuffers[gBufferNormals], depthStencilBuffer);
}
void DeferredRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(oitFramebuffer);
		graphicsSystem->destroy(refractedFramebuffer);
		graphicsSystem->destroy(uiFramebuffer);
		graphicsSystem->destroy(depthLdrFramebuffer);
		graphicsSystem->destroy(ldrFramebuffer);
		graphicsSystem->destroy(depthHdrFramebuffer);
		graphicsSystem->destroy(hdrFramebuffer);
		graphicsSystem->destroy(gFramebuffer);
		graphicsSystem->destroy(depthCopyBuffer);
		graphicsSystem->destroy(depthStencilBuffer);
		graphicsSystem->destroy(oitRevealBuffer);
		graphicsSystem->destroy(oitAccumBuffer);
		if (uiBuffer != gBuffers[0])
			graphicsSystem->destroy(uiBuffer);
		graphicsSystem->destroy(ldrBuffer);
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
		auto framebufferView = graphicsSystem->get(gFramebuffer);

		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("Deferred Pass", Color::transparent);
			framebufferView->beginRenderPass(clearColors, 0.0f, 0x00, int4::zero, asyncRecording);
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
		auto framebufferView = graphicsSystem->get(hdrFramebuffer);

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
		auto framebufferView = graphicsSystem->get(depthHdrFramebuffer);

		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("Depth HDR Pass", Color::transparent);
			framebufferView->beginRenderPass(float4::zero, 0.0f, 0, int4::zero, asyncRecording);
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
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Refracted Render Pass");
		static const array<float4, 2> clearColors = { float4::zero, float4::zero };
		auto framebufferView = graphicsSystem->get(refractedFramebuffer);
		
		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("Refracted Pass", Color::transparent);

			if (!hdrCopyBuffer)
				hdrCopyBuffer = createHdrBuffer(true);
			if (!depthCopyBuffer)
				depthCopyBuffer = createDepthStencilBuffer(true);

			Image::copy(hdrBuffer, hdrCopyBuffer);
			Image::copy(depthStencilBuffer, depthCopyBuffer);
			// TODO: generate blurry HDR chain. (GGX based)
			// TODO: also detect if no one uses it and skip copy.

			framebufferView->beginRenderPass(clearColors, 0.0f, 0, int4::zero, asyncRecording);
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
		auto framebufferView = graphicsSystem->get(depthHdrFramebuffer);

		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("Translucent Pass", Color::transparent);
			framebufferView->beginRenderPass(float4::zero, 0.0f, 0, int4::zero, asyncRecording);
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
	if (event->hasSubscribers())
	{
		if (!oitAccumBuffer)
			oitAccumBuffer = createOitAccumBuffer();
		if (!oitRevealBuffer)
			oitRevealBuffer = createOitRevealBuffer();
		if (!oitFramebuffer)
			oitFramebuffer = createOitFramebuffer(oitAccumBuffer, oitRevealBuffer, depthStencilBuffer);

		SET_CPU_ZONE_SCOPED("OIT Render Pass");
		auto framebufferView = graphicsSystem->get(oitFramebuffer);
		static const vector<float4> clearColors = { float4::zero, float4::one };

		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("OIT Pass", Color::transparent);
			framebufferView->beginRenderPass(clearColors, 0.0f, 0, int4::zero, asyncRecording);
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
		auto framebufferView = graphicsSystem->get(ldrFramebuffer);

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
		auto framebufferView = graphicsSystem->get(depthLdrFramebuffer);

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
		SET_CPU_ZONE_SCOPED("Copy LDR to UI");
		if (graphicsSystem->getRenderScale() == 1.0f)
			Image::copy(ldrBuffer, uiBuffer);
		else
			Image::blit(ldrBuffer, uiBuffer, Sampler::Filter::Linear);
	}
	graphicsSystem->stopRecording();

	event = &manager->getEvent("UiRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("UI Render Pass");
		auto framebufferView = graphicsSystem->get(uiFramebuffer);

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
}

//**********************************************************************************************************************
void DeferredRenderSystem::swapchainRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	const auto& swapchainChanges = graphicsSystem->getSwapchainChanges();

	if (swapchainChanges.framebufferSize)
	{
		auto destroyUI = uiBuffer != gBuffers[0];
		graphicsSystem->destroy(gBuffers);
		createGBuffers(gBuffers, emission, gi);
		graphicsSystem->destroy(hdrBuffer);
		hdrBuffer = createHdrBuffer(false);
		if (hdrCopyBuffer)
		{
			graphicsSystem->destroy(hdrCopyBuffer);
			hdrCopyBuffer = createHdrBuffer(true);
		}
		graphicsSystem->destroy(ldrBuffer);
		ldrBuffer = createLdrBuffer();
		if (destroyUI)
			graphicsSystem->destroy(uiBuffer);
		uiBuffer = graphicsSystem->getRenderScale() != 1.0f ? createUiBuffer() : gBuffers[0];
		if (oitAccumBuffer)
		{
			graphicsSystem->destroy(oitAccumBuffer);
			oitAccumBuffer = createOitAccumBuffer();
		}
		if (oitRevealBuffer)
		{
			graphicsSystem->destroy(oitRevealBuffer);
			oitRevealBuffer = createOitRevealBuffer();
		}
		graphicsSystem->destroy(depthStencilBuffer);
		depthStencilBuffer = createDepthStencilBuffer(false);
		if (depthCopyBuffer)
		{
			graphicsSystem->destroy(depthCopyBuffer);
			depthCopyBuffer = createDepthStencilBuffer(true);
		}
		
		auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
		Framebuffer::OutputAttachment colorAttachments[gBufferCount];
		Framebuffer::OutputAttachment depthStencilAttachment;
		auto bufferView = graphicsSystem->get(depthStencilBuffer);
		depthStencilAttachment.imageView = bufferView->getDefaultView();

		if (oitFramebuffer)
		{
			bufferView = graphicsSystem->get(oitAccumBuffer);
			colorAttachments[0] = Framebuffer::OutputAttachment(
				bufferView->getDefaultView(), DeferredRenderSystem::oitBufferFlags);
			bufferView = graphicsSystem->get(oitRevealBuffer);
			colorAttachments[1] = Framebuffer::OutputAttachment(
				bufferView->getDefaultView(), DeferredRenderSystem::oitBufferFlags);
			auto framebufferView = graphicsSystem->get(oitFramebuffer);
			depthStencilAttachment.setFlags(DeferredRenderSystem::oitBufferDepthFlags);
			framebufferView->update(framebufferSize, colorAttachments, 2, depthStencilAttachment);
		}

		bufferView = graphicsSystem->get(hdrBuffer);
		auto framebufferView = graphicsSystem->get(refractedFramebuffer);
		colorAttachments[0] = Framebuffer::OutputAttachment(
			bufferView->getDefaultView(), DeferredRenderSystem::hdrBufferFlags);
		bufferView = graphicsSystem->get(gBuffers[gBufferNormals]);
		colorAttachments[1] = Framebuffer::OutputAttachment(
			bufferView->getDefaultView(), DeferredRenderSystem::normalsBufferFlags);
		depthStencilAttachment.setFlags(DeferredRenderSystem::hdrBufferDepthFlags);
		framebufferView->update(framebufferSize, colorAttachments, 2, depthStencilAttachment);

		bufferView = graphicsSystem->get(uiBuffer);
		colorAttachments[0] = Framebuffer::OutputAttachment(
			bufferView->getDefaultView(), DeferredRenderSystem::uiBufferFlags);
		framebufferView = graphicsSystem->get(uiFramebuffer);
		framebufferView->update(graphicsSystem->getFramebufferSize(), colorAttachments, 1);

		bufferView = graphicsSystem->get(ldrBuffer);
		framebufferView = graphicsSystem->get(depthLdrFramebuffer);
		colorAttachments[0] = Framebuffer::OutputAttachment(
			bufferView->getDefaultView(), DeferredRenderSystem::ldrBufferFlags);
		depthStencilAttachment.setFlags(DeferredRenderSystem::ldrBufferDepthFlags);
		framebufferView->update(framebufferSize, colorAttachments, 1, depthStencilAttachment);

		colorAttachments[0] = Framebuffer::OutputAttachment(
			bufferView->getDefaultView(), DeferredRenderSystem::ldrBufferFlags);
		framebufferView = graphicsSystem->get(ldrFramebuffer);
		framebufferView->update(framebufferSize, colorAttachments, 1);

		bufferView = graphicsSystem->get(hdrBuffer);
		framebufferView = graphicsSystem->get(depthHdrFramebuffer);
		colorAttachments[0] = Framebuffer::OutputAttachment(
			bufferView->getDefaultView(), DeferredRenderSystem::hdrBufferFlags);
		depthStencilAttachment.setFlags(DeferredRenderSystem::hdrBufferDepthFlags);
		framebufferView->update(framebufferSize, colorAttachments, 1, depthStencilAttachment);

		framebufferView = graphicsSystem->get(hdrFramebuffer);
		colorAttachments[0] = Framebuffer::OutputAttachment(
			bufferView->getDefaultView(), DeferredRenderSystem::hdrBufferFlags);
		framebufferView->update(framebufferSize, colorAttachments, 1);	

		framebufferView = graphicsSystem->get(gFramebuffer);
		for (uint8 i = 0; i < DeferredRenderSystem::gBufferCount; i++)
		{
			if (!gBuffers[i])
			{
				colorAttachments[i] = {};
				continue;
			}

			auto gBufferView = graphicsSystem->get(gBuffers[i]);
			colorAttachments[i] = Framebuffer::OutputAttachment(
				gBufferView->getDefaultView(), DeferredRenderSystem::gBufferFlags);
		}
		depthStencilAttachment.setFlags(DeferredRenderSystem::gBufferDepthFlags);
		framebufferView->update(framebufferSize, colorAttachments, gBufferCount, depthStencilAttachment);
	}

	if (swapchainChanges.framebufferSize)
		Manager::Instance::get()->runEvent("GBufferRecreate");
}

//**********************************************************************************************************************
const vector<ID<Image>>& DeferredRenderSystem::getGBuffers()
{
	if (gBuffers.empty())
		createGBuffers(gBuffers, emission, gi);
	return gBuffers;
}
ID<Image> DeferredRenderSystem::getHdrBuffer()
{
	if (!hdrBuffer)
		hdrBuffer = createHdrBuffer(false);
	return hdrBuffer;
}
ID<Image> DeferredRenderSystem::getHdrCopyBuffer()
{
	if (!hdrCopyBuffer)
		hdrCopyBuffer = createHdrBuffer(true);
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
		uiBuffer = GraphicsSystem::Instance::get()->getRenderScale() != 1.0f ? createUiBuffer() : getGBuffers()[0];
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
		depthStencilBuffer = createDepthStencilBuffer(false);
	return depthStencilBuffer;
}
ID<Image> DeferredRenderSystem::getDepthCopyBuffer()
{
	if (!depthCopyBuffer)
		depthCopyBuffer = createDepthStencilBuffer(true);
	return depthCopyBuffer;
}

ID<Framebuffer> DeferredRenderSystem::getGFramebuffer()
{
	if (!gFramebuffer)
		gFramebuffer = createGFramebuffer(getGBuffers(), getDepthStencilBuffer());
	return gFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getHdrFramebuffer()
{
	if (!hdrFramebuffer)
		hdrFramebuffer = createHdrFramebuffer(getHdrBuffer());
	return hdrFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getDepthHdrFramebuffer()
{
	if (!depthHdrFramebuffer)
		depthHdrFramebuffer = createDepthHdrFramebuffer(getHdrBuffer(), getDepthStencilBuffer());
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
		depthLdrFramebuffer = createDepthLdrFramebuffer(getLdrBuffer(), getDepthStencilBuffer());
	return depthLdrFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getUiFramebuffer()
{
	if (!uiFramebuffer)
		uiFramebuffer = createUiFramebuffer(getUiBuffer());
	return uiFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getRefractedFramebuffer()
{
	if (!refractedFramebuffer)
	{
		refractedFramebuffer = createRefractedFramebuffer(getHdrBuffer(), 
			getGBuffers()[gBufferNormals], getDepthStencilBuffer());
	}
	return refractedFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getOitFramebuffer()
{
	if (!oitFramebuffer)
		oitFramebuffer = createOitFramebuffer(getOitAccumBuffer(), getOitRevealBuffer(), getDepthStencilBuffer());
	return oitFramebuffer;
}