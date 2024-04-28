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

#include "garden/system/render/deferred.hpp"
#include "garden/system/render/forward.hpp"

using namespace garden;

//**********************************************************************************************************************
static void createGBuffers(ID<Image>* gBuffers)
{
	const auto binds = Image::Bind::ColorAttachment | Image::Bind::Sampled | Image::Bind::Fullscreen;
	const auto strategy = Image::Strategy::Size;
	const Image::Mips mips = { { nullptr } };

	auto graphicsSystem = GraphicsSystem::getInstance();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	gBuffers[0] = graphicsSystem->createImage(Image::Format::SrgbR8G8B8A8, binds, mips, framebufferSize, strategy);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, gBuffers[0], "image.deferred.gBuffer0");
	gBuffers[1] = graphicsSystem->createImage(Image::Format::UnormA2R10G10B10, binds, mips, framebufferSize, strategy);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, gBuffers[1], "image.deferred.gBuffer1");
	gBuffers[2] = graphicsSystem->createImage(Image::Format::UnormR8G8B8A8, binds, mips, framebufferSize, strategy);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, gBuffers[2], "image.deferred.gBuffer2");
}
static void destroyGBuffers(const ID<Image>* gBuffers)
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	for (uint32 i = 0; i < gBufferCount; i++)
		graphicsSystem->destroy(gBuffers[i]);
}

static ID<Image> createDepthBuffer()
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	auto image = graphicsSystem->createImage(Image::Format::SfloatD32, Image::Bind::DepthStencilAttachment |
		Image::Bind::Sampled | Image::Bind::Fullscreen, { { nullptr } }, framebufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, image, "image.deferred.depth");
	return image;
}
static ID<Image> createHdrBuffer()
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	auto image = graphicsSystem->createImage(Image::Format::SfloatR16G16B16A16, Image::Bind::ColorAttachment | 
		Image::Bind::Sampled | Image::Bind::Fullscreen, { { nullptr } }, framebufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, image, "image.deferred.hdr");
	return image;
}
static ID<Image> createLdrBuffer()
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	auto swapchainView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());
	auto swapchainImageView = graphicsSystem->get(swapchainView->getColorAttachments()[0].imageView);
	auto image = graphicsSystem->createImage(swapchainImageView->getFormat(), 
		Image::Bind::ColorAttachment | Image::Bind::Sampled | Image::Bind::Fullscreen |
		Image::Bind::TransferSrc, { { nullptr } }, framebufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, image, "image.deferred.ldr");
	return image;
}

//**********************************************************************************************************************
static ID<Framebuffer> createGFramebuffer(const ID<Image>* gBuffers)
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	auto gBuffer0View = graphicsSystem->get(gBuffers[0]);
	auto gBuffer1View = graphicsSystem->get(gBuffers[1]);
	auto gBuffer2View = graphicsSystem->get(gBuffers[2]);
	auto depthStencilBuffer = graphicsSystem->getDepthStencilBuffer();

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{
		Framebuffer::OutputAttachment(gBuffer0View->getDefaultView(), false, false, true),
		Framebuffer::OutputAttachment(gBuffer1View->getDefaultView(), false, false, true),
		Framebuffer::OutputAttachment(gBuffer2View->getDefaultView(), false, false, true),
	};
	Framebuffer::OutputAttachment depthStencilAttachment(depthStencilBuffer, true, false, true);

	auto framebuffer = graphicsSystem->createFramebuffer(
		framebufferSize, std::move(colorAttachments), depthStencilAttachment); 
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, framebuffer, "framebuffer.deferred.gBuffer");
	return framebuffer;
}

static ID<Framebuffer> createHdrFramebuffer(ID<Image> hdrBuffer)
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	auto hdrBufferView = graphicsSystem->get(hdrBuffer);
	auto depthStencilBuffer = graphicsSystem->getDepthStencilBuffer();

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(hdrBufferView->getDefaultView(), false, true, true) };
	Framebuffer::OutputAttachment depthStencilAttachment(depthStencilBuffer, false, true, true);

	auto framebuffer = graphicsSystem->createFramebuffer(
		framebufferSize, std::move(colorAttachments), depthStencilAttachment);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, framebuffer, "framebuffer.deferred.hdr");
	return framebuffer;
}
static ID<Framebuffer> createLdrFramebuffer(ID<Image> ldrBuffer)
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	auto ldrBufferView = graphicsSystem->get(ldrBuffer);

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(ldrBufferView->getDefaultView(), false, true, true) };

	auto framebuffer = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, framebuffer, "framebuffer.deferred.ldr");
	return framebuffer;
}

//**********************************************************************************************************************
DeferredRenderSystem* DeferredRenderSystem::instance = nullptr;

DeferredRenderSystem::DeferredRenderSystem(Manager* manager, bool useAsyncRecording) : System(manager)
{
	this->asyncRecording = useAsyncRecording;

	manager->registerEvent("PreDeferredRender");
	manager->registerEvent("DeferredRender");
	manager->registerEvent("PreHdrRender");
	manager->registerEvent("HdrRender");
	manager->registerEvent("PreLdrRender");
	manager->registerEvent("LdrRender");
	manager->tryRegisterEvent("PreSwapchainRender"); // Note: can be shared with forward system.
	manager->registerEvent("GBufferRecreate");

	SUBSCRIBE_TO_EVENT("Init", DeferredRenderSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", DeferredRenderSystem::deinit);

	GARDEN_ASSERT(!instance); // More than one system instance detected.
	instance = this;
}
DeferredRenderSystem::~DeferredRenderSystem()
{
	auto manager = getManager();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", DeferredRenderSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", DeferredRenderSystem::deinit);

		manager->unregisterEvent("PreDeferredRender");
		manager->unregisterEvent("DeferredRender");
		manager->unregisterEvent("PreHdrRender");
		manager->unregisterEvent("HdrRender");
		manager->unregisterEvent("PreLdrRender");
		manager->unregisterEvent("LdrRender");
		manager->tryUnregisterEvent("PreSwapchainRender");
		manager->unregisterEvent("GBufferRecreate");
	}

	GARDEN_ASSERT(instance); // More than one system instance detected.
	instance = nullptr;
}

//**********************************************************************************************************************
void DeferredRenderSystem::init()
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	GARDEN_ASSERT(asyncRecording == graphicsSystem->useAsyncRecording());

	auto manager = getManager();
	SUBSCRIBE_TO_EVENT("Render", DeferredRenderSystem::render);
	SUBSCRIBE_TO_EVENT("SwapchainRecreate", DeferredRenderSystem::swapchainRecreate);

	if (!gBuffers[0])
		createGBuffers(gBuffers);
	if (!hdrBuffer)
		hdrBuffer = createHdrBuffer();
	if (!ldrBuffer)
		ldrBuffer = createLdrBuffer();
	
	if (!gFramebuffer)
		gFramebuffer = createGFramebuffer(gBuffers);
	if (!hdrFramebuffer)
		hdrFramebuffer = createHdrFramebuffer(hdrBuffer);
	if (!ldrFramebuffer)
		ldrFramebuffer = createLdrFramebuffer(ldrBuffer);
}
void DeferredRenderSystem::deinit()
{
	auto manager = getManager();
	if (manager->isRunning())
	{
		auto graphicsSystem = GraphicsSystem::getInstance();
		graphicsSystem->destroy(ldrFramebuffer);
		graphicsSystem->destroy(hdrFramebuffer);
		graphicsSystem->destroy(gFramebuffer);

		graphicsSystem->destroy(ldrBuffer);
		graphicsSystem->destroy(hdrBuffer);
		destroyGBuffers(gBuffers);

		UNSUBSCRIBE_FROM_EVENT("Render", DeferredRenderSystem::render);
		UNSUBSCRIBE_FROM_EVENT("SwapchainRecreate", DeferredRenderSystem::swapchainRecreate);
	}
}

//**********************************************************************************************************************
void DeferredRenderSystem::render()
{
	if (!isEnabled || !GraphicsSystem::getInstance()->canRender())
		return;
	
	#if GARDEN_DEBUG
	auto forwardSystem = getManager()->tryGet<ForwardRenderSystem>();
	if (forwardSystem)
	{
		// Can not use deferred and forward render system at the same time.
		GARDEN_ASSERT(!forwardSystem->isEnabled); 
	}
	#endif

	auto manager = getManager();
	auto graphicsSystem = GraphicsSystem::getInstance();
	graphicsSystem->startRecording(CommandBufferType::Frame);

	{
		SET_GPU_DEBUG_LABEL("Pre Deferred", Color::transparent);
		manager->runEvent("PreDeferredRender");
	}

	auto framebufferView = graphicsSystem->get(gFramebuffer);
	{
		SET_GPU_DEBUG_LABEL("Deferred Pass", Color::transparent);
		float4 clearColors[gBufferCount] = { float4(0.0f), float4(0.0f), float4(0.0f) };
		framebufferView->beginRenderPass(clearColors, gBufferCount, 0.0f, 0x00, int4(0), asyncRecording);
		manager->runEvent("DeferredRender");
		framebufferView->endRenderPass();
	}

	{
		SET_GPU_DEBUG_LABEL("Pre HDR", Color::transparent);
		manager->runEvent("PreHdrRender");
	}

	{
		SET_GPU_DEBUG_LABEL("HDR Pass", Color::transparent);
		framebufferView = graphicsSystem->get(hdrFramebuffer);
		framebufferView->beginRenderPass(float4(0.0f), 0.0f, 0, int4(0), asyncRecording);
		manager->runEvent("HdrRender");
		framebufferView->endRenderPass();
	}

	{
		SET_GPU_DEBUG_LABEL("Pre LDR", Color::transparent);
		manager->runEvent("PreLdrRender");
	}

	{
		SET_GPU_DEBUG_LABEL("LDR Pass", Color::transparent);
		framebufferView = graphicsSystem->get(ldrFramebuffer);
		framebufferView->beginRenderPass(float4(0.0f));
		manager->runEvent("LdrRender");
		framebufferView->endRenderPass();
	}

	{
		SET_GPU_DEBUG_LABEL("Pre Swapchain", Color::transparent);
		manager->runEvent("PreSwapchainRender");
	}

	if (runSwapchainPass)
	{
		SET_GPU_DEBUG_LABEL("Swapchain Pass", Color::transparent);
		framebufferView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());
		auto& colorAttachments = framebufferView->getColorAttachments();
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
	auto graphicsSystem = GraphicsSystem::getInstance();
	auto& swapchainChanges = graphicsSystem->getSwapchainChanges();

	if (swapchainChanges.framebufferSize)
	{
		destroyGBuffers(gBuffers);
		createGBuffers(gBuffers);
		graphicsSystem->destroy(hdrBuffer);
		hdrBuffer = createHdrBuffer();
		graphicsSystem->destroy(ldrBuffer);
		ldrBuffer = createLdrBuffer();
		
		auto framebufferView = graphicsSystem->get(gFramebuffer);
		auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
		auto ldrBufferView = graphicsSystem->get(ldrBuffer);
		Framebuffer::OutputAttachment colorAttachment(ldrBufferView->getDefaultView(), false, true, true);
		framebufferView = graphicsSystem->get(ldrFramebuffer);
		framebufferView->update(framebufferSize, &colorAttachment, 1);

		framebufferView = graphicsSystem->get(hdrFramebuffer);
		auto hdrBufferView = graphicsSystem->get(hdrBuffer);
		colorAttachment.imageView = hdrBufferView->getDefaultView();
		auto depthStencilBuffer = graphicsSystem->getDepthStencilBuffer();
		Framebuffer::OutputAttachment depthStencilAttachment(depthStencilBuffer, false, true, true);
		framebufferView->update(framebufferSize, &colorAttachment, 1, depthStencilAttachment);

		framebufferView = graphicsSystem->get(gFramebuffer);
		auto gBuffer0View = graphicsSystem->get(gBuffers[0]);
		auto gBuffer1View = graphicsSystem->get(gBuffers[1]);
		auto gBuffer2View = graphicsSystem->get(gBuffers[2]);
		Framebuffer::OutputAttachment colorAttachments[gBufferCount];
		colorAttachments[0] = Framebuffer::OutputAttachment(gBuffer0View->getDefaultView(), false, false, true);
		colorAttachments[1] = Framebuffer::OutputAttachment(gBuffer1View->getDefaultView(), false, false, true);
		colorAttachments[2] = Framebuffer::OutputAttachment(gBuffer2View->getDefaultView(), false, false, true);
		depthStencilAttachment.clear = true;
		depthStencilAttachment.load = false;
		framebufferView->update(framebufferSize, colorAttachments, gBufferCount, depthStencilAttachment);

		getManager()->runEvent("GBufferRecreate");
	}
}

//**********************************************************************************************************************
ID<Image>* DeferredRenderSystem::getGBuffers()
{
	if (!gBuffers[0])
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

ID<Framebuffer> DeferredRenderSystem::getGFramebuffer()
{
	if (!gFramebuffer)
		gFramebuffer = createGFramebuffer(getGBuffers());
	return gFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getHdrFramebuffer()
{
	if (!hdrFramebuffer)
		hdrFramebuffer = createHdrFramebuffer(getHdrBuffer());
	return hdrFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getLdrFramebuffer()
{
	if (!ldrFramebuffer)
		ldrFramebuffer = createLdrFramebuffer(getLdrBuffer());
	return ldrFramebuffer;
}