// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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
#include "garden/system/transform.hpp"
#include "garden/system/camera.hpp"
#include "garden/profiler.hpp"

// TODO: Add stencil support like in the deferred system.

using namespace garden;

//**********************************************************************************************************************
static ID<Image> createColorBuffer(GraphicsSystem* graphicsSystem, bool useHdrColorBuffer)
{
	auto image = graphicsSystem->createImage(useHdrColorBuffer ? 
		ForwardRenderSystem::hdrBufferFormat : ForwardRenderSystem::colorBufferFormat, Image::Usage::ColorAttachment | 
		Image::Usage::Sampled | Image::Usage::TransferSrc | Image::Usage::Fullscreen, 
		{ { nullptr } }, graphicsSystem->getScaledFrameSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.forward.color");
	return image;
}
static ID<Image> createUiBuffer(GraphicsSystem* graphicsSystem)
{
	auto image = graphicsSystem->createImage(ForwardRenderSystem::uiBufferFormat, Image::Usage::ColorAttachment | 
		Image::Usage::Sampled | Image::Usage::TransferSrc | Image::Usage::TransferDst | Image::Usage::Fullscreen,
		{ { nullptr } }, graphicsSystem->getFramebufferSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.forward.ui");
	return image;
}
static ID<Image> createDepthStencilBuffer(GraphicsSystem* graphicsSystem)
{
	auto usage = Image::Usage::DepthStencilAttachment | Image::Usage::Fullscreen;
	#if GARDEN_EDITOR
	usage |= Image::Usage::TransferSrc;
	#endif

	auto image = graphicsSystem->createImage(ForwardRenderSystem::depthStencilFormat, usage, 
		{ { nullptr } }, graphicsSystem->getScaledFrameSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.forward.depthStencil");
	return image;
}

static ID<Framebuffer> createColorFramebuffer(GraphicsSystem* graphicsSystem, ID<Image> colorBuffer)
{
	auto colorBufferView = graphicsSystem->get(colorBuffer)->getView();
	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(colorBufferView, ForwardRenderSystem::colorBufferFlags), };

	auto framebuffer = graphicsSystem->createFramebuffer(
		graphicsSystem->getScaledFrameSize(), std::move(colorAttachments)); 
	SET_RESOURCE_DEBUG_NAME(framebuffer,"framebuffer.forward.color");
	return framebuffer;
}
static ID<Framebuffer> createFullFramebuffer(GraphicsSystem* graphicsSystem, 
	ID<Image> colorBuffer, ID<Image> depthStencilBuffer)
{
	auto colorBufferView = graphicsSystem->get(colorBuffer)->getView();
	auto depthStencilBufferView = graphicsSystem->get(depthStencilBuffer)->getView();

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(colorBufferView, ForwardRenderSystem::colorBufferFlags), };
	Framebuffer::OutputAttachment depthStencilAttachment(
		depthStencilBufferView, ForwardRenderSystem::depthBufferFlags);

	auto framebuffer = graphicsSystem->createFramebuffer(graphicsSystem->getScaledFrameSize(), 
		std::move(colorAttachments), depthStencilAttachment); 
	SET_RESOURCE_DEBUG_NAME(framebuffer,"framebuffer.forward.full");
	return framebuffer;
}
static ID<Framebuffer> createUiFramebuffer(GraphicsSystem* graphicsSystem, ID<Image> uiBuffer)
{
	auto uiBufferView = graphicsSystem->get(uiBuffer)->getView();
	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(uiBufferView, ForwardRenderSystem::uiBufferFlags) };

	auto framebuffer = graphicsSystem->createFramebuffer(
		graphicsSystem->getFramebufferSize(), std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.forward.ui");
	return framebuffer;
}

//**********************************************************************************************************************
ForwardRenderSystem::ForwardRenderSystem(bool useAsyncRecording, bool useHdrColorBuffer, bool setSingleton) : 
	Singleton(setSingleton), asyncRecording(useAsyncRecording), hdrColorBuffer(useHdrColorBuffer)
{
	auto manager = Manager::Instance::get();
	manager->registerEvent("PreForwardRender");
	manager->registerEvent("ForwardRender");
	manager->registerEvent("PreDepthForwardRender");
	manager->registerEvent("DepthForwardRender");
	manager->tryRegisterEvent("PreUiRender");
	manager->tryRegisterEvent("UiRender");
	manager->registerEvent("ColorBufferRecreate");

	ECSM_SUBSCRIBE_TO_EVENT("Init", ForwardRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", ForwardRenderSystem::deinit);
}
ForwardRenderSystem::~ForwardRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", ForwardRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", ForwardRenderSystem::deinit);
		
		manager->unregisterEvent("PreForwardRender");
		manager->unregisterEvent("ForwardRender");
		manager->unregisterEvent("PreDepthForwardRender");
		manager->unregisterEvent("DepthForwardRender");
		manager->tryUnregisterEvent("PreUiRender");
		manager->tryUnregisterEvent("UiRender");
		manager->unregisterEvent("ColorBufferRecreate");
	}

	unsetSingleton();
}

//**********************************************************************************************************************
void ForwardRenderSystem::init()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	GARDEN_ASSERT(asyncRecording == graphicsSystem->useAsyncRecording());

	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Render", ForwardRenderSystem::render);
	ECSM_SUBSCRIBE_TO_EVENT("SwapchainRecreate", ForwardRenderSystem::swapchainRecreate);
}
void ForwardRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		if (uiFramebuffer != colorFramebuffer)
			graphicsSystem->destroy(uiFramebuffer);
		graphicsSystem->destroy(fullFramebuffer);
		graphicsSystem->destroy(colorFramebuffer);
		graphicsSystem->destroy(depthStencilBuffer);
		if (uiBuffer != colorBuffer)
			graphicsSystem->destroy(uiBuffer);
		graphicsSystem->destroy(colorBuffer);

		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Render", ForwardRenderSystem::render);
		ECSM_UNSUBSCRIBE_FROM_EVENT("SwapchainRecreate", ForwardRenderSystem::swapchainRecreate);
	}
}

//**********************************************************************************************************************
void ForwardRenderSystem::render()
{
	SET_CPU_ZONE_SCOPED("Forward Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!isEnabled || !graphicsSystem->canRender() || !graphicsSystem->camera)
		return;

	auto manager = Manager::Instance::get();
	auto cameraView = manager->tryGet<CameraComponent>(graphicsSystem->camera);
	auto transformView = manager->tryGet<TransformComponent>(graphicsSystem->camera);
	if (!cameraView || !transformView || !transformView->isActive())
		return;

	#if GARDEN_DEBUG
	if (DeferredRenderSystem::Instance::tryGet())
	{
		GARDEN_ASSERT_MSG(!DeferredRenderSystem::Instance::get()->isEnabled, 
			"Can not use forward and deferred render system at the same time"); 
	}
	#endif

	auto event = &manager->getEvent("PreForwardRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre Forward Render");
		event->run();
	}

	event = &manager->getEvent("ForwardRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Forward Render Pass");
		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("Forward Pass");
			{
				RenderPass renderPass(getColorFramebuffer(), float4::zero,
					0.0f, 0x00, int4::zero, asyncRecording);
				event->run();
			}
		}
		graphicsSystem->stopRecording();
	}

	event = &manager->getEvent("PreDepthForwardRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre Depth Forward Render");
		event->run();
	}

	event = &manager->getEvent("DepthForwardRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Depth Forward Render Pass");
		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("Depth Forward Pass");
			{
				RenderPass renderPass(getFullFramebuffer(), float4::zero,
					0.0f, 0x00, int4::zero, asyncRecording);
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
	event = &manager->getEvent("UiRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("UI Render Pass");
		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("UI Pass");
			{
				RenderPass renderPass(getUiFramebuffer(), float4::zero);
				event->run();
			}
		}
		graphicsSystem->stopRecording();
	}

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		SET_GPU_DEBUG_LABEL("Copy UI to Pass");
		if (hdrColorBuffer)
		{
			abort(); // TODO: tonemapping.
		}
		else
		{
			auto _uiBuffer = getUiBuffer();
			auto framebufferView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());
			const auto& colorAttachments = framebufferView->getColorAttachments();
			auto swapchainImageView = graphicsSystem->get(colorAttachments[0].imageView);

			if (uiBufferFormat == swapchainImageView->getFormat())
				Image::copy(_uiBuffer, swapchainImageView->getImage());
			else
				Image::blit(_uiBuffer, swapchainImageView->getImage(), Sampler::Filter::Nearest);
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
		if (uiBuffer != colorBuffer)
			graphicsSystem->destroy(uiBuffer);
		graphicsSystem->destroy(uiFramebuffer);
		graphicsSystem->destroy(depthStencilBuffer);
		graphicsSystem->destroy(colorBuffer);

		auto framebufferSize = graphicsSystem->getScaledFrameSize();
		Framebuffer::OutputAttachment colorAttachment;

		if (fullFramebuffer)
		{
			auto framebufferView = graphicsSystem->get(fullFramebuffer);
			colorAttachment = Framebuffer::OutputAttachment(
				graphicsSystem->get(getColorBuffer())->getView(), colorBufferFlags);
			Framebuffer::OutputAttachment depthStencilAttachment;
			depthStencilAttachment.imageView = graphicsSystem->get(getDepthStencilBuffer())->getView();
			depthStencilAttachment.setFlags(depthBufferFlags);
			framebufferView->update(framebufferSize, &colorAttachment, 1, depthStencilAttachment);
		}
		if (colorFramebuffer)
		{
			colorAttachment = Framebuffer::OutputAttachment(
				graphicsSystem->get(getColorBuffer())->getView(), colorBufferFlags);
			auto framebufferView = graphicsSystem->get(colorFramebuffer);
			framebufferView->update(framebufferSize, &colorAttachment, 1);
		}
	}

	if (swapchainChanges.framebufferSize)
		Manager::Instance::get()->runEvent("ColorBufferRecreate");
}

//**********************************************************************************************************************
ID<Image> ForwardRenderSystem::getColorBuffer()
{
	if (!colorBuffer)
		colorBuffer = createColorBuffer(GraphicsSystem::Instance::get(), hdrColorBuffer);
	return colorBuffer;
}
ID<Image> ForwardRenderSystem::getUiBuffer()
{
	if (!uiBuffer)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		uiBuffer = graphicsSystem->getScaledFrameSize() != graphicsSystem->getFramebufferSize() ? 
			createUiBuffer(graphicsSystem) : colorBuffer;
	}
	return uiBuffer;
}
ID<Image> ForwardRenderSystem::getDepthStencilBuffer()
{
	if (!depthStencilBuffer)
		depthStencilBuffer = createDepthStencilBuffer(GraphicsSystem::Instance::get());
	return depthStencilBuffer;
}

ID<Framebuffer> ForwardRenderSystem::getColorFramebuffer()
{
	if (!colorFramebuffer)
		colorFramebuffer = createColorFramebuffer(GraphicsSystem::Instance::get(), getColorBuffer());
	return colorFramebuffer;
}
ID<Framebuffer> ForwardRenderSystem::getFullFramebuffer()
{
	if (!fullFramebuffer)
	{
		fullFramebuffer = createFullFramebuffer(GraphicsSystem::Instance::get(), 
			getColorBuffer(), getDepthStencilBuffer());
	}
	return fullFramebuffer;
}
ID<Framebuffer> ForwardRenderSystem::getUiFramebuffer()
{
	if (!uiFramebuffer)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		uiFramebuffer = graphicsSystem->getScaledFrameSize() != graphicsSystem->getFramebufferSize() ? 
			createUiFramebuffer(graphicsSystem, getUiBuffer()) : getColorFramebuffer();
	}
	return uiFramebuffer;
}