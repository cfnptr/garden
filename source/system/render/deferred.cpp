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
static void createGBuffers(vector<ID<Image>>& gBuffers, bool useEmissive, bool useSSS)
{
	constexpr auto binds = Image::Bind::ColorAttachment | Image::Bind::Sampled | Image::Bind::Fullscreen;
	constexpr auto strategy = Image::Strategy::Size;
	Image::Format formats[DeferredRenderSystem::gBufferCount]
	{
		Image::Format::SrgbR8G8B8A8,
		Image::Format::UnormR8G8B8A8,
		Image::Format::UnormR8G8B8A8,
		Image::Format::UnormA2B10G10R10,
		useEmissive ? Image::Format::SrgbR8G8B8A8 : Image::Format::Undefined,
		useSSS ? Image::Format::SrgbR8G8B8A8 : Image::Format::Undefined,
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

		gBuffers[i] = graphicsSystem->createImage(formats[i], binds, mips, framebufferSize, strategy);
		SET_RESOURCE_DEBUG_NAME(gBuffers[i], "image.deferred.gBuffer" + to_string(i));
	}
}

static ID<Image> createDepthStencilBuffer()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto depthBufferView = graphicsSystem->get(graphicsSystem->getDepthStencilBuffer());
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	auto bind = Image::Bind::DepthStencilAttachment | Image::Bind::Sampled | Image::Bind::Fullscreen;

	#if GARDEN_EDITOR
	bind |= Image::Bind::TransferSrc;
	#endif

	auto image = graphicsSystem->createImage(depthBufferView->getFormat(), 
		bind, { { nullptr } }, framebufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.forward.depth");
	return image;
}

static ID<Image> createHdrBuffer()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	auto image = graphicsSystem->createImage(Image::Format::SfloatR16G16B16A16, Image::Bind::ColorAttachment | 
		Image::Bind::Sampled | Image::Bind::Fullscreen, { { nullptr } }, framebufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.hdr");
	return image;
}
static ID<Image> createLdrBuffer()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	auto swapchainView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());
	auto swapchainImageView = graphicsSystem->get(swapchainView->getColorAttachments()[0].imageView);
	auto image = graphicsSystem->createImage(swapchainImageView->getFormat(), 
		Image::Bind::ColorAttachment | Image::Bind::Sampled | Image::Bind::Fullscreen |
		Image::Bind::TransferSrc, { { nullptr } }, framebufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.ldr");
	return image;
}

static ID<Image> createOitAccumBuffer()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	auto image = graphicsSystem->createImage(Image::Format::SfloatR16G16B16A16, Image::Bind::ColorAttachment | 
		Image::Bind::Sampled | Image::Bind::Fullscreen, { { nullptr } }, framebufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.oitAccum");
	return image;
}
static ID<Image> createOitRevealBuffer()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	auto image = graphicsSystem->createImage(Image::Format::UnormR8, Image::Bind::ColorAttachment | 
		Image::Bind::Sampled | Image::Bind::Fullscreen, { { nullptr } }, framebufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.oitReveal");
	return image;
}

//**********************************************************************************************************************
static ID<Framebuffer> createGFramebuffer(const vector<ID<Image>> gBuffers, ID<Image> depthStencilBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();

	vector<Framebuffer::OutputAttachment> colorAttachments(DeferredRenderSystem::gBufferCount);
	for (uint8 i = 0; i < DeferredRenderSystem::gBufferCount; i++)
	{
		if (!gBuffers[i])
		{
			colorAttachments[i] = {};
			continue;
		}

		auto gBufferView = graphicsSystem->get(gBuffers[i]);
		colorAttachments[i] = Framebuffer::OutputAttachment(gBufferView->getDefaultView(), false, false, true);
	}

	Framebuffer::OutputAttachment depthStencilAttachment(depthStencilBuffer ?
		graphicsSystem->get(depthStencilBuffer)->getDefaultView() :
		graphicsSystem->getDepthStencilBuffer(), true, false, true);
	auto framebuffer = graphicsSystem->createFramebuffer(framebufferSize, 
		std::move(colorAttachments), depthStencilAttachment); 
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.gBuffer");
	return framebuffer;
}

static ID<Framebuffer> createHdrFramebuffer(ID<Image> hdrBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	auto hdrBufferView = graphicsSystem->get(hdrBuffer);

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(hdrBufferView->getDefaultView(), false, false, true) };

	auto framebuffer = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.hdr");
	return framebuffer;
}
static ID<Framebuffer> createMetaHdrFramebuffer(ID<Image> hdrBuffer, ID<Image> depthStencilBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	auto hdrBufferView = graphicsSystem->get(hdrBuffer);

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(hdrBufferView->getDefaultView(), false, true, true) };
	Framebuffer::OutputAttachment depthStencilAttachment(depthStencilBuffer ?
		graphicsSystem->get(depthStencilBuffer)->getDefaultView() :
		graphicsSystem->getDepthStencilBuffer(), false, true, true);
	auto framebuffer = graphicsSystem->createFramebuffer(framebufferSize, 
		std::move(colorAttachments), depthStencilAttachment);
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.metaHdr");
	return framebuffer;
}
static ID<Framebuffer> createLdrFramebuffer(ID<Image> ldrBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	auto ldrBufferView = graphicsSystem->get(ldrBuffer);

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(ldrBufferView->getDefaultView(), false, false, true) };

	auto framebuffer = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.ldr");
	return framebuffer;
}

static ID<Framebuffer> createOitFramebuffer(ID<Image> oitAccumBuffer, 
	ID<Image> oitRevealBuffer, ID<Image> depthStencilBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	auto oitAccumBufferView = graphicsSystem->get(oitAccumBuffer);
	auto oitRevealBufferView = graphicsSystem->get(oitRevealBuffer);

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{
		Framebuffer::OutputAttachment(oitAccumBufferView->getDefaultView(), true, false, true),
		Framebuffer::OutputAttachment(oitRevealBufferView->getDefaultView(), true, false, true)
	};
	Framebuffer::OutputAttachment depthStencilAttachment(depthStencilBuffer ?
		graphicsSystem->get(depthStencilBuffer)->getDefaultView() :
		graphicsSystem->getDepthStencilBuffer(), false, true, false);
	auto framebuffer = graphicsSystem->createFramebuffer(framebufferSize, 
		std::move(colorAttachments), depthStencilAttachment);
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.oit");
	return framebuffer;
}

//**********************************************************************************************************************
DeferredRenderSystem::DeferredRenderSystem(bool useEmissive, bool useSSS, bool useAsyncRecording, bool setSingleton) : 
	Singleton(setSingleton), emissive(useEmissive), sss(useSSS), asyncRecording(useAsyncRecording)
{
	auto manager = Manager::Instance::get();
	manager->registerEvent("PreDeferredRender");
	manager->registerEvent("DeferredRender");
	manager->registerEvent("PreHdrRender");
	manager->registerEvent("HdrRender");
	manager->registerEvent("PreMetaHdrRender");
	manager->registerEvent("MetaHdrRender");
	manager->registerEvent("PreOitRender");
	manager->registerEvent("OitRender");
	manager->registerEvent("PreLdrRender");
	manager->registerEvent("LdrRender");
	manager->tryRegisterEvent("PreSwapchainRender");
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
		manager->unregisterEvent("PreMetaHdrRender");
		manager->unregisterEvent("MetaHdrRender");
		manager->unregisterEvent("PreOitRender");
		manager->unregisterEvent("OitRender");
		manager->unregisterEvent("PreLdrRender");
		manager->unregisterEvent("LdrRender");
		if (manager->hasEvent("PreSwapchainRender") && !manager->has<ForwardRenderSystem>())
			manager->unregisterEvent("PreSwapchainRender");
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
		createGBuffers(gBuffers, emissive, sss);
	if (!hdrBuffer)
		hdrBuffer = createHdrBuffer();
	if (!ldrBuffer)
		ldrBuffer = createLdrBuffer();
	if (!oitAccumBuffer)
		oitAccumBuffer = createOitAccumBuffer();
	if (!oitRevealBuffer)
		oitRevealBuffer = createOitRevealBuffer();
	if (!depthStencilBuffer && graphicsSystem->getRenderScale() != 1.0f)
		depthStencilBuffer = createDepthStencilBuffer();
	
	if (!gFramebuffer)
		gFramebuffer = createGFramebuffer(gBuffers, depthStencilBuffer);
	if (!hdrFramebuffer)
		hdrFramebuffer = createHdrFramebuffer(hdrBuffer);
	if (!metaHdrFramebuffer)
		metaHdrFramebuffer = createMetaHdrFramebuffer(hdrBuffer, depthStencilBuffer);
	if (!ldrFramebuffer)
		ldrFramebuffer = createLdrFramebuffer(ldrBuffer);
	if (!oitFramebuffer)
		oitFramebuffer = createOitFramebuffer(oitAccumBuffer, oitRevealBuffer, depthStencilBuffer);
}
void DeferredRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(oitFramebuffer);
		graphicsSystem->destroy(ldrFramebuffer);
		graphicsSystem->destroy(metaHdrFramebuffer);
		graphicsSystem->destroy(hdrFramebuffer);
		graphicsSystem->destroy(gFramebuffer);
		graphicsSystem->destroy(depthStencilBuffer);
		graphicsSystem->destroy(oitRevealBuffer);
		graphicsSystem->destroy(oitAccumBuffer);
		graphicsSystem->destroy(ldrBuffer);
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

	#if GARDEN_DEBUG
	auto forwardSystem = ForwardRenderSystem::Instance::tryGet();
	if (forwardSystem)
	{
		// Can not use deferred and forward render system at the same time.
		GARDEN_ASSERT(!forwardSystem->isEnabled); 
	}
	#endif

	auto graphicsSystem = GraphicsSystem::Instance::get();
	graphicsSystem->startRecording(CommandBufferType::Frame);

	auto event = &manager->getEvent("PreDeferredRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre Deferred Render");
		SET_GPU_DEBUG_LABEL("Pre Deferred", Color::transparent);
		event->run();
	}

	event = &manager->getEvent("DeferredRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Deferred Render Pass");
		SET_GPU_DEBUG_LABEL("Deferred Pass", Color::transparent);
		const f32x4 clearColors[gBufferCount] = 
		{ f32x4::zero, f32x4::zero, f32x4::zero, f32x4::zero, f32x4::zero };
		auto framebufferView = graphicsSystem->get(gFramebuffer);
		framebufferView->beginRenderPass(clearColors, gBufferCount, 0.0f, 0x00, i32x4::zero, asyncRecording);
		event->run();
		framebufferView->endRenderPass();
	}

	event = &manager->getEvent("PreHdrRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre HDR Render");
		SET_GPU_DEBUG_LABEL("Pre HDR", Color::transparent);
		event->run();
	}

	event = &manager->getEvent("HdrRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("HDR Render Pass");
		SET_GPU_DEBUG_LABEL("HDR Pass", Color::transparent);
		auto framebufferView = graphicsSystem->get(hdrFramebuffer);
		framebufferView->beginRenderPass(f32x4::zero);
		event->run();
		framebufferView->endRenderPass();
	}

	event = &manager->getEvent("PreMetaHdrRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre Meta HDR Render");
		SET_GPU_DEBUG_LABEL("Pre Meta HDR", Color::transparent);
		event->run();
	}

	event = &manager->getEvent("MetaHdrRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Meta HDR Render Pass");
		SET_GPU_DEBUG_LABEL("Meta HDR Pass", Color::transparent);
		auto framebufferView = graphicsSystem->get(metaHdrFramebuffer);
		framebufferView->beginRenderPass(f32x4::zero, 0.0f, 0, i32x4::zero, asyncRecording);
		event->run();
		framebufferView->endRenderPass();
	}

	event = &manager->getEvent("PreOitRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre OIT Render");
		SET_GPU_DEBUG_LABEL("Pre OIT", Color::transparent);
		event->run();
	}

	event = &manager->getEvent("OitRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("OIT Render Pass");
		SET_GPU_DEBUG_LABEL("OIT Pass", Color::transparent);
		auto framebufferView = graphicsSystem->get(oitFramebuffer);
		static const f32x4 clearColors[2] = { f32x4::zero, f32x4::one };
		framebufferView->beginRenderPass(clearColors, 2, 0.0f, 0, i32x4::zero, asyncRecording);
		event->run();
		framebufferView->endRenderPass();
	}

	event = &manager->getEvent("PreLdrRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre LDR Render");
		SET_GPU_DEBUG_LABEL("Pre LDR", Color::transparent);
		event->run();
	}

	event = &manager->getEvent("LdrRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("LDR Render Pass");
		SET_GPU_DEBUG_LABEL("LDR Pass", Color::transparent);
		auto framebufferView = graphicsSystem->get(ldrFramebuffer);
		framebufferView->beginRenderPass(f32x4::zero);
		event->run();
		framebufferView->endRenderPass();
	}

	event = &manager->getEvent("PreSwapchainRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre Swapchain Render");
		SET_GPU_DEBUG_LABEL("Pre Swapchain", Color::transparent);
		event->run();
	}

	#if GARDEN_EDITOR
	if (depthStencilBuffer)
	{
		SET_GPU_DEBUG_LABEL("Copy Swapchain Depth", Color::transparent);
		auto mainDepthStencilView = graphicsSystem->get(graphicsSystem->getDepthStencilBuffer());
		Image::blit(depthStencilBuffer, mainDepthStencilView->getImage(), SamplerFilter::Nearest);
	}
	#endif

	if (runSwapchainPass)
	{
		SET_GPU_DEBUG_LABEL("Swapchain Pass", Color::transparent);
		auto framebufferView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());
		const auto& colorAttachments = framebufferView->getColorAttachments();
		auto colorImageView = graphicsSystem->get(colorAttachments[0].imageView);

		if (graphicsSystem->getRenderScale() == 1.0f)
			Image::copy(ldrBuffer, colorImageView->getImage());
		else
			Image::blit(ldrBuffer, colorImageView->getImage(), SamplerFilter::Linear);
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
		graphicsSystem->destroy(gBuffers);
		createGBuffers(gBuffers, emissive, sss);
		graphicsSystem->destroy(hdrBuffer);
		hdrBuffer = createHdrBuffer();
		graphicsSystem->destroy(ldrBuffer);
		ldrBuffer = createLdrBuffer();
		graphicsSystem->destroy(oitAccumBuffer);
		oitAccumBuffer = createOitAccumBuffer();
		graphicsSystem->destroy(oitRevealBuffer);
		oitRevealBuffer = createOitRevealBuffer();

		graphicsSystem->destroy(depthStencilBuffer);
		if (graphicsSystem->getRenderScale() != 1.0f)
			depthStencilBuffer = createDepthStencilBuffer();
		else
			depthStencilBuffer = {};
		
		auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
		Framebuffer::OutputAttachment colorAttachments[gBufferCount];
		Framebuffer::OutputAttachment depthStencilAttachment(depthStencilBuffer ?
			graphicsSystem->get(depthStencilBuffer)->getDefaultView() :
			graphicsSystem->getDepthStencilBuffer(), false, true, false);

		auto bufferView = graphicsSystem->get(oitAccumBuffer);
		colorAttachments[0] = Framebuffer::OutputAttachment(bufferView->getDefaultView(), true, false, true);
		bufferView = graphicsSystem->get(oitRevealBuffer);
		colorAttachments[1] = Framebuffer::OutputAttachment(bufferView->getDefaultView(), true, false, true);
		auto framebufferView = graphicsSystem->get(oitFramebuffer);
		framebufferView->update(framebufferSize, colorAttachments, 2, depthStencilAttachment);

		bufferView = graphicsSystem->get(ldrBuffer);
		colorAttachments[0] = Framebuffer::OutputAttachment(bufferView->getDefaultView(), false, false, true);
		framebufferView = graphicsSystem->get(ldrFramebuffer);
		framebufferView->update(framebufferSize, colorAttachments, 1);

		framebufferView = graphicsSystem->get(hdrFramebuffer);
		bufferView = graphicsSystem->get(hdrBuffer);
		colorAttachments[0] = Framebuffer::OutputAttachment(bufferView->getDefaultView(), false, false, true);
		framebufferView->update(framebufferSize, colorAttachments, 1);

		framebufferView = graphicsSystem->get(metaHdrFramebuffer);
		colorAttachments[0] = Framebuffer::OutputAttachment(bufferView->getDefaultView(), false, true, true);
		depthStencilAttachment.setFlags(false, true, true);
		framebufferView->update(framebufferSize, colorAttachments, 1, depthStencilAttachment);

		framebufferView = graphicsSystem->get(gFramebuffer);
		for (uint8 i = 0; i < DeferredRenderSystem::gBufferCount; i++)
		{
			if (!gBuffers[i])
			{
				colorAttachments[i] = {};
				continue;
			}

			auto gBufferView = graphicsSystem->get(gBuffers[i]);
			colorAttachments[i] = Framebuffer::OutputAttachment(gBufferView->getDefaultView(), false, false, true);
		}
		depthStencilAttachment.setFlags(true, false, true);
		framebufferView->update(framebufferSize, colorAttachments, gBufferCount, depthStencilAttachment);
	}

	// Deferred system notifies both framebufferSize and bufferCount changes!
	if (swapchainChanges.framebufferSize || swapchainChanges.bufferCount)
		Manager::Instance::get()->runEvent("GBufferRecreate");
}

//**********************************************************************************************************************
const vector<ID<Image>>& DeferredRenderSystem::getGBuffers()
{
	if (gBuffers.empty())
		createGBuffers(gBuffers, emissive, sss);
	return gBuffers;
}
ID<Image> DeferredRenderSystem::getHdrBuffer()
{
	if (!hdrBuffer)
		hdrBuffer = createHdrBuffer();
	return hdrBuffer;
}
ID<Image> DeferredRenderSystem::getLdrBuffer()
{
	if (!ldrBuffer)
		ldrBuffer = createLdrBuffer();
	return ldrBuffer;
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
	{
		if (GraphicsSystem::Instance::get()->getRenderScale() != 1.0f)
			depthStencilBuffer = createDepthStencilBuffer();
	}
	return depthStencilBuffer;
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
ID<Framebuffer> DeferredRenderSystem::getMetaHdrFramebuffer()
{
	if (!metaHdrFramebuffer)
		metaHdrFramebuffer = createMetaHdrFramebuffer(getHdrBuffer(), getDepthStencilBuffer());
	return metaHdrFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getLdrFramebuffer()
{
	if (!ldrFramebuffer)
		ldrFramebuffer = createLdrFramebuffer(getLdrBuffer());
	return ldrFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getOitFramebuffer()
{
	if (!oitFramebuffer)
		oitFramebuffer = createOitFramebuffer(getOitAccumBuffer(), getOitRevealBuffer(), getDepthStencilBuffer());
	return oitFramebuffer;
}