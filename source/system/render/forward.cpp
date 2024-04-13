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
#include "garden/system/settings.hpp"

using namespace garden;

//**********************************************************************************************************************
static ID<Image> createColorBuffer(int2 framebufferSize, bool useHdrColorBuffer)
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	auto swapchainView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());
	auto swapchainImageView = graphicsSystem->get(swapchainView->getColorAttachments()[0].imageView);
	auto image = graphicsSystem->createImage(
		useHdrColorBuffer ? Image::Format::SfloatR16G16B16A16 : swapchainImageView->getFormat(), 
		Image::Bind::ColorAttachment | Image::Bind::Sampled | Image::Bind::Fullscreen |
		Image::Bind::TransferSrc, { { nullptr } }, framebufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, image, "image.forward.color");
	return image;
}
static ID<Image> createDepthStencilBuffer(int2 framebufferSize, bool useStencilBuffer)
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	auto image = graphicsSystem->createImage(
		useStencilBuffer ? Image::Format::UnormD24UintS8 : Image::Format::SfloatD32,
		Image::Bind::DepthStencilAttachment | Image::Bind::Sampled | Image::Bind::Fullscreen,
		{ { nullptr } }, framebufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, image, "image.forward.depth");
	return image;
}

static ID<Framebuffer> createFramebuffer(ID<Image> colorBuffer, ID<Image> depthBuffer, int2 framebufferSize)
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	auto colorBufferView = graphicsSystem->get(colorBuffer);
	auto depthBufferView = graphicsSystem->get(depthBuffer);

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(colorBufferView->getDefaultView(), false, false, true), };
	Framebuffer::OutputAttachment depthStencilAttachment(depthBufferView->getDefaultView(), true, false, true);

	auto framebuffer = graphicsSystem->createFramebuffer(
		framebufferSize, std::move(colorAttachments), depthStencilAttachment); 
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, framebuffer,"framebuffer.forward");
	return framebuffer;
}

//**********************************************************************************************************************
ForwardRenderSystem::ForwardRenderSystem(Manager* manager, bool useAsyncRecording,
	bool useHdrColorBuffer, bool useStencilBuffer) : System(manager)
{
	this->asyncRecording = useAsyncRecording;
	this->hdrColorBuffer = useHdrColorBuffer;
	this->stencilBuffer = useStencilBuffer;

	manager->registerEvent("ForwardRender");

	SUBSCRIBE_TO_EVENT("PreInit", ForwardRenderSystem::preInit);
	SUBSCRIBE_TO_EVENT("PostDeinit", ForwardRenderSystem::postDeinit);
	SUBSCRIBE_TO_EVENT("Render", ForwardRenderSystem::render);
	SUBSCRIBE_TO_EVENT("SwapchainRecreate", ForwardRenderSystem::swapchainRecreate);
}
ForwardRenderSystem::~ForwardRenderSystem()
{
	auto manager = getManager();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("PreInit", ForwardRenderSystem::preInit);
		UNSUBSCRIBE_FROM_EVENT("PostDeinit", ForwardRenderSystem::postDeinit);
		UNSUBSCRIBE_FROM_EVENT("Render", ForwardRenderSystem::render);
		UNSUBSCRIBE_FROM_EVENT("SwapchainRecreate", ForwardRenderSystem::swapchainRecreate);

		manager->unregisterEvent("ForwardRender");
	}
}

//**********************************************************************************************************************
void ForwardRenderSystem::preInit()
{
	auto manager = getManager();
	auto settingsSystem = manager->tryGet<SettingsSystem>();
	if (settingsSystem)
		settingsSystem->getFloat("renderScale", renderScale);

	auto graphicsSystem = GraphicsSystem::getInstance();
	GARDEN_ASSERT(asyncRecording == graphicsSystem->useAsyncRecording());

	framebufferSize = max((int2)(float2(graphicsSystem->getFramebufferSize()) * renderScale), int2(1));

	if (!colorBuffer)
		colorBuffer = createColorBuffer(framebufferSize, hdrColorBuffer);
	if (!depthStencilBuffer)
		depthStencilBuffer = createDepthStencilBuffer(framebufferSize, stencilBuffer);
	if (!framebuffer)
		framebuffer = createFramebuffer(colorBuffer, depthStencilBuffer, framebufferSize);
}
void ForwardRenderSystem::postDeinit()
{
	auto manager = getManager();
	if (manager->isRunning())
	{
		auto graphicsSystem = GraphicsSystem::getInstance();
		graphicsSystem->destroy(framebuffer);
		graphicsSystem->destroy(depthStencilBuffer);
		graphicsSystem->destroy(colorBuffer);
	}
}

//**********************************************************************************************************************
void ForwardRenderSystem::render()
{
	if (!isEnabled || !GraphicsSystem::getInstance()->canRender())
		return;

	// TODO: check if deferred render system is also enabled and throw exception.

	auto graphicsSystem = GraphicsSystem::getInstance();
	graphicsSystem->startRecording(CommandBufferType::Frame);

	auto framebufferView = graphicsSystem->get(framebuffer);
	{
		SET_GPU_DEBUG_LABEL("Forward Pass", Color::transparent);
		framebufferView->beginRenderPass(float4(0.0f), 0.0f, 0x00, int4(0), asyncRecording);
		getManager()->runEvent("ForwardRender");
		framebufferView->endRenderPass();
	}

	if (runSwapchainPass)
	{
		SET_GPU_DEBUG_LABEL("Swapchain Pass", Color::transparent);
		framebufferView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());
		auto colorImageView = graphicsSystem->get(framebufferView->getColorAttachments()[0].imageView);

		if (!hdrColorBuffer && framebufferSize == graphicsSystem->getFramebufferSize())
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
	auto graphicsSystem = GraphicsSystem::getInstance();
	auto& swapchainChanges = graphicsSystem->getSwapchainChanges();

	if (swapchainChanges.framebufferSize)
	{
		framebufferSize = max((int2)(float2(graphicsSystem->getFramebufferSize()) * renderScale), int2(1));

		graphicsSystem->destroy(depthStencilBuffer);
		graphicsSystem->destroy(colorBuffer);
		colorBuffer = createColorBuffer(framebufferSize, hdrColorBuffer);
		depthStencilBuffer = createDepthStencilBuffer(framebufferSize, stencilBuffer);

		auto framebufferView = graphicsSystem->get(framebuffer);
		auto colorBufferView = graphicsSystem->get(colorBuffer);
		auto depthStencilBufferView = graphicsSystem->get(depthStencilBuffer);
		Framebuffer::OutputAttachment colorAttachment(
			colorBufferView->getDefaultView(), false, true, true);
		Framebuffer::OutputAttachment depthStencilAttachment(
			depthStencilBufferView->getDefaultView(), false, true, true);
		framebufferView->update(framebufferSize, &colorAttachment, 1, depthStencilAttachment);
	}
}

//**********************************************************************************************************************
void ForwardRenderSystem::setRenderScale(float renderScale)
{
	if (renderScale == this->renderScale)
		return;
	this->renderScale = renderScale;

	SwapchainChanges swapchainChanges = {};
	swapchainChanges.framebufferSize = true;
	GraphicsSystem::getInstance()->recreateSwapchain(swapchainChanges);
}

static int2 getScaledFramebufferSize(Manager* manager, float& renderScale)
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	auto settingsSystem = manager->tryGet<SettingsSystem>();
	if (settingsSystem)
		settingsSystem->getFloat("renderScale", renderScale);
	return max((int2)(float2(graphicsSystem->getFramebufferSize()) * renderScale), int2(1));
}

ID<Image> ForwardRenderSystem::getColorBuffer()
{
	if (!colorBuffer)
	{
		framebufferSize = getScaledFramebufferSize(getManager(), renderScale);
		colorBuffer = createColorBuffer(framebufferSize, hdrColorBuffer);
	}
	return colorBuffer;
}
ID<Image> ForwardRenderSystem::getDepthStencilBuffer()
{
	if (!depthStencilBuffer)
	{
		framebufferSize = getScaledFramebufferSize(getManager(), renderScale);
		depthStencilBuffer = createDepthStencilBuffer(framebufferSize, stencilBuffer);
	}
	return depthStencilBuffer;
}

ID<Framebuffer> ForwardRenderSystem::getFramebuffer()
{
	if (!framebuffer)
	{
		framebufferSize = getScaledFramebufferSize(getManager(), renderScale);
		framebuffer = createFramebuffer(getColorBuffer(), getDepthStencilBuffer(), framebufferSize);
	}
	return framebuffer;
}