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

using namespace garden;

//**********************************************************************************************************************
static void createGBuffers(vector<ID<Image>>& gBuffers)
{
	constexpr auto binds = Image::Bind::ColorAttachment | Image::Bind::Sampled | Image::Bind::Fullscreen;
	constexpr auto strategy = Image::Strategy::Size;
	constexpr Image::Format formats[DeferredRenderSystem::gBufferCount]
	{
		Image::Format::SrgbR8G8B8A8,
		Image::Format::UnormR8G8B8A8,
		Image::Format::UnormA2B10G10R10,
		Image::Format::SrgbR8G8B8A8,
		Image::Format::SrgbR8G8B8A8,
	};

	const Image::Mips mips = { { nullptr } };
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	gBuffers.resize(DeferredRenderSystem::gBufferCount);

	for (uint8 i = 0; i < DeferredRenderSystem::gBufferCount; i++)
	{
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

//**********************************************************************************************************************
static ID<Framebuffer> createGFramebuffer(const vector<ID<Image>> gBuffers, ID<Image> depthStencilBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	auto mainDepthStencilBuffer = graphicsSystem->getDepthStencilBuffer();
	Framebuffer::OutputAttachment depthStencilAttachment(mainDepthStencilBuffer, true, false, true);

	vector<Framebuffer::OutputAttachment> colorAttachments(DeferredRenderSystem::gBufferCount);
	for (uint8 i = 0; i < DeferredRenderSystem::gBufferCount; i++)
	{
		auto gBufferView = graphicsSystem->get(gBuffers[i]);
		colorAttachments[i] = Framebuffer::OutputAttachment(gBufferView->getDefaultView(), false, false, true);
	}

	if (depthStencilBuffer)
		depthStencilAttachment.imageView = graphicsSystem->get(depthStencilBuffer)->getDefaultView();
	auto framebuffer = graphicsSystem->createFramebuffer(
		framebufferSize, std::move(colorAttachments), depthStencilAttachment); 
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.gBuffer");
	return framebuffer;
}

static ID<Framebuffer> createHdrFramebuffer(ID<Image> hdrBuffer, ID<Image> depthStencilBuffer)
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
static ID<Framebuffer> createTranslucentFramebuffer(ID<Image> hdrBuffer, ID<Image> depthStencilBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	auto hdrBufferView = graphicsSystem->get(hdrBuffer);
	auto mainDepthStencilBuffer = graphicsSystem->getDepthStencilBuffer();

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(hdrBufferView->getDefaultView(), false, true, true) };
	Framebuffer::OutputAttachment depthStencilAttachment(mainDepthStencilBuffer, false, true, true);

	if (depthStencilBuffer)
		depthStencilAttachment.imageView = graphicsSystem->get(depthStencilBuffer)->getDefaultView();
	auto framebuffer = graphicsSystem->createFramebuffer(
		framebufferSize, std::move(colorAttachments), depthStencilAttachment);
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.translucent");
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

//**********************************************************************************************************************
DeferredRenderSystem::DeferredRenderSystem(bool useAsyncRecording, bool setSingleton) : 
	Singleton(setSingleton), asyncRecording(useAsyncRecording)
{
	auto manager = Manager::Instance::get();
	manager->registerEvent("PreDeferredRender");
	manager->registerEvent("DeferredRender");
	manager->registerEvent("PreHdrRender");
	manager->registerEvent("HdrRender");
	manager->registerEvent("PreLdrRender");
	manager->registerEvent("LdrRender");
	manager->registerEvent("PreTranslucentRender");
	manager->registerEvent("TranslucentRender");
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
		manager->unregisterEvent("PreTranslucentRender");
		manager->unregisterEvent("TranslucentRender");
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
		createGBuffers(gBuffers);
	if (!hdrBuffer)
		hdrBuffer = createHdrBuffer();
	if (!ldrBuffer)
		ldrBuffer = createLdrBuffer();
	if (!depthStencilBuffer && graphicsSystem->getRenderScale() != 1.0f)
		depthStencilBuffer = createDepthStencilBuffer();
	
	if (!gFramebuffer)
		gFramebuffer = createGFramebuffer(gBuffers, depthStencilBuffer);
	if (!hdrFramebuffer)
		hdrFramebuffer = createHdrFramebuffer(hdrBuffer, depthStencilBuffer);
	if (!translucentFramebuffer)
		translucentFramebuffer = createTranslucentFramebuffer(hdrBuffer, depthStencilBuffer);
	if (!ldrFramebuffer)
		ldrFramebuffer = createLdrFramebuffer(ldrBuffer);
}
void DeferredRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(ldrFramebuffer);
		graphicsSystem->destroy(translucentFramebuffer);
		graphicsSystem->destroy(hdrFramebuffer);
		graphicsSystem->destroy(gFramebuffer);
		graphicsSystem->destroy(depthStencilBuffer);
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

	{
		SET_CPU_ZONE_SCOPED("Pre Deferred Render");
		SET_GPU_DEBUG_LABEL("Pre Deferred", Color::transparent);
		manager->runEvent("PreDeferredRender");
	}

	auto framebufferView = graphicsSystem->get(gFramebuffer);
	{
		SET_CPU_ZONE_SCOPED("Deferred Render Pass");
		SET_GPU_DEBUG_LABEL("Deferred Pass", Color::transparent);
		constexpr float4 clearColors[gBufferCount] = 
		{ float4(0.0f), float4(0.0f), float4(0.0f), float4(0.0f), float4(0.0f) };
		framebufferView->beginRenderPass(clearColors, gBufferCount, 0.0f, 0x00, int4(0), asyncRecording);
		manager->runEvent("DeferredRender");
		framebufferView->endRenderPass();
	}

	{
		SET_CPU_ZONE_SCOPED("Pre HDR Render");
		SET_GPU_DEBUG_LABEL("Pre HDR", Color::transparent);
		manager->runEvent("PreHdrRender");
	}

	{
		SET_CPU_ZONE_SCOPED("HDR Render Pass");
		SET_GPU_DEBUG_LABEL("HDR Pass", Color::transparent);
		framebufferView = graphicsSystem->get(hdrFramebuffer);
		framebufferView->beginRenderPass(float4(0.0f));
		manager->runEvent("HdrRender");
		framebufferView->endRenderPass();
	}

	{
		SET_CPU_ZONE_SCOPED("Pre Translucent Render");
		SET_GPU_DEBUG_LABEL("Pre Translucent", Color::transparent);
		manager->runEvent("PreTranslucentRender");
	}

	{
		SET_CPU_ZONE_SCOPED("Translucent Render Pass");
		SET_GPU_DEBUG_LABEL("Translucent Pass", Color::transparent);
		framebufferView = graphicsSystem->get(translucentFramebuffer);
		framebufferView->beginRenderPass(float4(0.0f), 0.0f, 0, int4(0), asyncRecording);
		manager->runEvent("TranslucentRender");
		framebufferView->endRenderPass();
	}

	{
		SET_CPU_ZONE_SCOPED("Pre LDR Render");
		SET_GPU_DEBUG_LABEL("Pre LDR", Color::transparent);
		manager->runEvent("PreLdrRender");
	}

	{
		SET_CPU_ZONE_SCOPED("LDR Render Pass");
		SET_GPU_DEBUG_LABEL("LDR Pass", Color::transparent);
		framebufferView = graphicsSystem->get(ldrFramebuffer);
		framebufferView->beginRenderPass(float4(0.0f));
		manager->runEvent("LdrRender");
		framebufferView->endRenderPass();
	}

	{
		SET_CPU_ZONE_SCOPED("Pre Swapchain Render");
		SET_GPU_DEBUG_LABEL("Pre Swapchain", Color::transparent);
		manager->runEvent("PreSwapchainRender");
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
		framebufferView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());
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
		createGBuffers(gBuffers);
		graphicsSystem->destroy(hdrBuffer);
		hdrBuffer = createHdrBuffer();
		graphicsSystem->destroy(ldrBuffer);
		ldrBuffer = createLdrBuffer();

		graphicsSystem->destroy(depthStencilBuffer);
		if (graphicsSystem->getRenderScale() != 1.0f)
			depthStencilBuffer = createDepthStencilBuffer();
		else
			depthStencilBuffer = {};
		
		auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
		auto ldrBufferView = graphicsSystem->get(ldrBuffer);
		Framebuffer::OutputAttachment colorAttachment(ldrBufferView->getDefaultView(), false, false, true);
		auto framebufferView = graphicsSystem->get(ldrFramebuffer);
		framebufferView->update(framebufferSize, &colorAttachment, 1);

		framebufferView = graphicsSystem->get(hdrFramebuffer);
		auto hdrBufferView = graphicsSystem->get(hdrBuffer);
		colorAttachment.imageView = hdrBufferView->getDefaultView();
		framebufferView->update(framebufferSize, &colorAttachment, 1);

		framebufferView = graphicsSystem->get(translucentFramebuffer);
		colorAttachment.load = true;
		auto mainDepthStencilBuffer = graphicsSystem->getDepthStencilBuffer();
		Framebuffer::OutputAttachment depthStencilAttachment(mainDepthStencilBuffer, false, true, true);
		if (depthStencilBuffer)
			depthStencilAttachment.imageView = graphicsSystem->get(depthStencilBuffer)->getDefaultView();
		framebufferView->update(framebufferSize, &colorAttachment, 1, depthStencilAttachment);

		framebufferView = graphicsSystem->get(gFramebuffer);
		Framebuffer::OutputAttachment colorAttachments[gBufferCount];
		for (uint8 i = 0; i < DeferredRenderSystem::gBufferCount; i++)
		{
			auto gBufferView = graphicsSystem->get(gBuffers[i]);
			colorAttachments[i] = Framebuffer::OutputAttachment(gBufferView->getDefaultView(), false, false, true);
		}
		depthStencilAttachment.clear = true;
		depthStencilAttachment.load = false;
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
		createGBuffers(gBuffers);
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
		hdrFramebuffer = createHdrFramebuffer(getHdrBuffer(), getDepthStencilBuffer());
	return hdrFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getTranslucentFramebuffer()
{
	if (!translucentFramebuffer)
		translucentFramebuffer = createTranslucentFramebuffer(getHdrBuffer(), getDepthStencilBuffer());
	return translucentFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getLdrFramebuffer()
{
	if (!ldrFramebuffer)
		ldrFramebuffer = createLdrFramebuffer(getLdrBuffer());
	return ldrFramebuffer;
}