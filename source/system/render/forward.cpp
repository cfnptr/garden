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

#include "garden/system/render/forward.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/settings.hpp"

using namespace garden;

//**********************************************************************************************************************
static ID<Image> createColorBuffer(bool useHdrColorBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	auto swapchainView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());
	auto swapchainImageView = graphicsSystem->get(swapchainView->getColorAttachments()[0].imageView);
	auto image = graphicsSystem->createImage(
		useHdrColorBuffer ? Image::Format::SfloatR16G16B16A16 : swapchainImageView->getFormat(), 
		Image::Bind::ColorAttachment | Image::Bind::Sampled | Image::Bind::Fullscreen |
		Image::Bind::TransferSrc, { { nullptr } }, framebufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.forward.color");
	return image;
}
static ID<Image> createDepthStencilBuffer()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto depthBufferView = graphicsSystem->get(graphicsSystem->getDepthStencilBuffer());
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	auto image = graphicsSystem->createImage(depthBufferView->getFormat(), 
		Image::Bind::DepthStencilAttachment | Image::Bind::Fullscreen,
		{ { nullptr } }, framebufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.forward.depth");
	return image;
}

static ID<Framebuffer> createFramebuffer(ID<Image> colorBuffer, ID<Image> depthStencilBuffer, bool clearColorBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	auto colorBufferView = graphicsSystem->get(colorBuffer);
	auto mainDepthStencilBuffer = graphicsSystem->getDepthStencilBuffer();
	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(colorBufferView->getDefaultView(), clearColorBuffer, false, true), };
	Framebuffer::OutputAttachment depthStencilAttachment(mainDepthStencilBuffer, true, false, true);
	if (depthStencilBuffer)
		depthStencilAttachment.imageView = graphicsSystem->get(depthStencilBuffer)->getDefaultView();
	auto framebuffer = graphicsSystem->createFramebuffer(
		framebufferSize, std::move(colorAttachments), depthStencilAttachment); 
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
	if (manager->getEventSubscribers("PreSwapchainRender").empty())
		manager->tryRegisterEvent("PreSwapchainRender"); // Note: can be shared with deferred system.
	manager->registerEvent("ColorBufferRecreate");

	ECSM_SUBSCRIBE_TO_EVENT("Init", ForwardRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", ForwardRenderSystem::deinit);
}
ForwardRenderSystem::~ForwardRenderSystem()
{
	if (Manager::Instance::get()->isRunning())
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", ForwardRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", ForwardRenderSystem::deinit);
		
		auto manager = Manager::Instance::get();
		manager->unregisterEvent("PreForwardRender");
		manager->unregisterEvent("ForwardRender");
		manager->tryUnregisterEvent("PreSwapchainRender");
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
	if (!depthStencilBuffer && graphicsSystem->getRenderScale() != 1.0f)
		depthStencilBuffer = createDepthStencilBuffer();
	if (!framebuffer)
		framebuffer = createFramebuffer(colorBuffer, depthStencilBuffer, clearColorBuffer);
}
void ForwardRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning())
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(framebuffer);
		graphicsSystem->destroy(colorBuffer);

		ECSM_UNSUBSCRIBE_FROM_EVENT("Render", ForwardRenderSystem::render);
		ECSM_UNSUBSCRIBE_FROM_EVENT("SwapchainRecreate", ForwardRenderSystem::swapchainRecreate);
	}
}

//**********************************************************************************************************************
void ForwardRenderSystem::render()
{
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

	{
		SET_GPU_DEBUG_LABEL("Pre Forward", Color::transparent);
		manager->runEvent("PreForwardRender");
	}

	auto framebufferView = graphicsSystem->get(framebuffer);
	{
		SET_GPU_DEBUG_LABEL("Forward Pass", Color::transparent);
		framebufferView->beginRenderPass(float4(0.0f), 0.0f, 0x00, int4(0), asyncRecording);
		manager->runEvent("ForwardRender");
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
		auto colorImageView = graphicsSystem->get(framebufferView->getColorAttachments()[0].imageView);

		if (!hdrColorBuffer && graphicsSystem->getRenderScale() == 1.0f)
		{
			Image::copy(colorBuffer, colorImageView->getImage());
		}
		else
		{
			if (hdrColorBuffer)
			{
				// TODO: tonemap color buffer
				abort();
			}
			else
			{
				Image::blit(colorBuffer, colorImageView->getImage(), SamplerFilter::Linear);
			}
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
		if (graphicsSystem->getRenderScale() != 1.0f)
			depthStencilBuffer = createDepthStencilBuffer();
		else
			depthStencilBuffer = {};

		auto framebufferView = graphicsSystem->get(framebuffer);
		auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
		auto colorBufferView = graphicsSystem->get(colorBuffer);
		Framebuffer::OutputAttachment colorAttachment(
			colorBufferView->getDefaultView(), clearColorBuffer, false, true);
		auto mainDepthStencilBuffer = graphicsSystem->getDepthStencilBuffer();
		Framebuffer::OutputAttachment depthStencilAttachment(mainDepthStencilBuffer, true, false, true);
		if (depthStencilBuffer)
			depthStencilAttachment.imageView = graphicsSystem->get(depthStencilBuffer)->getDefaultView();
		framebufferView->update(framebufferSize, &colorAttachment, 1, depthStencilAttachment);

		Manager::Instance::get()->runEvent("ColorBufferRecreate");
	}
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
	{
		if (GraphicsSystem::Instance::get()->getRenderScale() != 1.0f)
			depthStencilBuffer = createDepthStencilBuffer();
	}
	return depthStencilBuffer;
}

ID<Framebuffer> ForwardRenderSystem::getFramebuffer()
{
	if (!framebuffer)
		framebuffer = createFramebuffer(getColorBuffer(), getDepthStencilBuffer(), clearColorBuffer);
	return framebuffer;
}