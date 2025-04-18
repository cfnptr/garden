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

#include "garden/system/render/forward.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/profiler.hpp"

using namespace garden;

//**********************************************************************************************************************
static ID<Image> createColorBuffer(bool useHdrColorBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto image = graphicsSystem->createImage(useHdrColorBuffer ? 
		ForwardRenderSystem::hdrBufferFormat : ForwardRenderSystem::colorBufferFormat, 
		Image::Bind::ColorAttachment | Image::Bind::Sampled | Image::Bind::Fullscreen | Image::Bind::TransferSrc, 
		{ { nullptr } }, graphicsSystem->getScaledFramebufferSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.forward.color");
	return image;
}
static ID<Image> createDepthStencilBuffer()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto bind = Image::Bind::DepthStencilAttachment | Image::Bind::Fullscreen;

	#if GARDEN_EDITOR
	bind |= Image::Bind::TransferSrc;
	#endif

	auto image = graphicsSystem->createImage(ForwardRenderSystem::depthStencilFormat, bind, 
		{ { nullptr } }, graphicsSystem->getScaledFramebufferSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.forward.depthStencil");
	return image;
}

static ID<Framebuffer> createFramebuffer(ID<Image> colorBuffer, ID<Image> depthStencilBuffer, bool clearColorBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto colorBufferView = graphicsSystem->get(colorBuffer);
	auto depthStencilBufferView = graphicsSystem->get(depthStencilBuffer);

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(colorBufferView->getDefaultView(), clearColorBuffer, false, true), };
	Framebuffer::OutputAttachment depthStencilAttachment(depthStencilBufferView->getDefaultView(), true, false, true);

	auto framebuffer = graphicsSystem->createFramebuffer(graphicsSystem->getScaledFramebufferSize(), 
		std::move(colorAttachments), depthStencilAttachment); 
	SET_RESOURCE_DEBUG_NAME(framebuffer,"framebuffer.forward");
	return framebuffer;
}

//**********************************************************************************************************************
ForwardRenderSystem::ForwardRenderSystem(bool clearColorBuffer,
	bool useAsyncRecording, bool useHdrColorBuffer, bool setSingleton) : Singleton(setSingleton), 
	clearColorBuffer(clearColorBuffer), asyncRecording(useAsyncRecording), hdrColorBuffer(useHdrColorBuffer)
{
	auto manager = Manager::Instance::get();
	manager->registerEvent("PreForwardRender");
	manager->registerEvent("ForwardRender");
	manager->tryRegisterEvent("PreSwapchainRender");
	manager->registerEvent("ColorBufferRecreate");

	ECSM_SUBSCRIBE_TO_EVENT("Init", ForwardRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", ForwardRenderSystem::deinit);
}
ForwardRenderSystem::~ForwardRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", ForwardRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", ForwardRenderSystem::deinit);
		
		auto manager = Manager::Instance::get();
		manager->unregisterEvent("PreForwardRender");
		manager->unregisterEvent("ForwardRender");
		if (manager->hasEvent("PreSwapchainRender") && !manager->has<DeferredRenderSystem>())
			manager->unregisterEvent("PreSwapchainRender");
		manager->unregisterEvent("ColorBufferRecreate");
	}

	unsetSingleton();
}

//**********************************************************************************************************************
void ForwardRenderSystem::init()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	GARDEN_ASSERT(asyncRecording == graphicsSystem->useAsyncRecording());

	ECSM_SUBSCRIBE_TO_EVENT("Render", ForwardRenderSystem::render);
	ECSM_SUBSCRIBE_TO_EVENT("SwapchainRecreate", ForwardRenderSystem::swapchainRecreate);

	if (!colorBuffer)
		colorBuffer = createColorBuffer(hdrColorBuffer);
	if (!depthStencilBuffer)
		depthStencilBuffer = createDepthStencilBuffer();
	if (!framebuffer)
		framebuffer = createFramebuffer(colorBuffer, depthStencilBuffer, clearColorBuffer);
}
void ForwardRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(framebuffer);
		graphicsSystem->destroy(depthStencilBuffer);
		graphicsSystem->destroy(colorBuffer);

		ECSM_UNSUBSCRIBE_FROM_EVENT("Render", ForwardRenderSystem::render);
		ECSM_UNSUBSCRIBE_FROM_EVENT("SwapchainRecreate", ForwardRenderSystem::swapchainRecreate);
	}
}

//**********************************************************************************************************************
void ForwardRenderSystem::render()
{
	SET_CPU_ZONE_SCOPED("Forward Render");

	if (!isEnabled || !GraphicsSystem::Instance::get()->canRender())
		return;

	auto manager = Manager::Instance::get();
	
	#if GARDEN_DEBUG
	auto deferredSystem = DeferredRenderSystem::Instance::tryGet();
	if (deferredSystem)
	{
		// Can not use forward and deferred render system at the same time.
		GARDEN_ASSERT(!deferredSystem->isEnabled); 
	}
	#endif

	auto graphicsSystem = GraphicsSystem::Instance::get();
	graphicsSystem->startRecording(CommandBufferType::Frame);

	auto event = &manager->getEvent("PreForwardRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre Forward Render");
		SET_GPU_DEBUG_LABEL("Pre Forward", Color::transparent);
		event->run();
	}

	event = &manager->getEvent("ForwardRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Forward Render Pass");
		SET_GPU_DEBUG_LABEL("Forward Pass", Color::transparent);
		auto framebufferView = graphicsSystem->get(framebuffer);
		framebufferView->beginRenderPass(float4::zero, 0.0f, 0x00, int4::zero, asyncRecording);
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

	{
		SET_GPU_DEBUG_LABEL("Swapchain Pass", Color::transparent);
		auto framebufferView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());
		auto swapchainImageView = graphicsSystem->get(framebufferView->getColorAttachments()[0].imageView);

		if (graphicsSystem->getRenderScale() == 1.0f)
		{
			if (hdrColorBuffer)
			{
				abort(); // TODO: tonemapping.
			}
			else
			{
				if (colorBufferFormat == swapchainImageView->getFormat())
					Image::copy(colorBuffer, swapchainImageView->getImage());
				else
					Image::blit(colorBuffer, swapchainImageView->getImage(), Sampler::Filter::Nearest);
			}
		}
		else
		{
			if (hdrColorBuffer)
				abort(); // TODO: tonemapping.
			else
				Image::blit(colorBuffer, swapchainImageView->getImage(), Sampler::Filter::Linear);
		}
	}

	graphicsSystem->stopRecording();
}

//**********************************************************************************************************************
void ForwardRenderSystem::swapchainRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	const auto& swapchainChanges = graphicsSystem->getSwapchainChanges();

	if (swapchainChanges.framebufferSize)
	{
		graphicsSystem->destroy(colorBuffer);
		colorBuffer = createColorBuffer(hdrColorBuffer);

		graphicsSystem->destroy(depthStencilBuffer);
		depthStencilBuffer = createDepthStencilBuffer();

		auto framebufferView = graphicsSystem->get(framebuffer);
		auto colorBufferView = graphicsSystem->get(colorBuffer);
		auto depthStencilBufferView = graphicsSystem->get(depthStencilBuffer);
		Framebuffer::OutputAttachment colorAttachment(
			colorBufferView->getDefaultView(), clearColorBuffer, false, true);
		Framebuffer::OutputAttachment depthStencilAttachment(
			depthStencilBufferView->getDefaultView(), true, false, true);
		framebufferView->update(graphicsSystem->getScaledFramebufferSize(), 
			&colorAttachment, 1, depthStencilAttachment);
	}

	// Forward system notifies both framebufferSize and bufferCount changes!
	if (swapchainChanges.framebufferSize || swapchainChanges.bufferCount)
		Manager::Instance::get()->runEvent("ColorBufferRecreate");
}

//**********************************************************************************************************************
ID<Image> ForwardRenderSystem::getColorBuffer()
{
	if (!colorBuffer)
		colorBuffer = createColorBuffer(hdrColorBuffer);
	return colorBuffer;
}
ID<Image> ForwardRenderSystem::getDepthStencilBuffer()
{
	if (!depthStencilBuffer)
		depthStencilBuffer = createDepthStencilBuffer();
	return depthStencilBuffer;
}

ID<Framebuffer> ForwardRenderSystem::getFramebuffer()
{
	if (!framebuffer)
		framebuffer = createFramebuffer(getColorBuffer(), getDepthStencilBuffer(), clearColorBuffer);
	return framebuffer;
}