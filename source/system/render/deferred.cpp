//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "garden/system/render/deferred.hpp"
#include "garden/system/render/editor/deferred.hpp"
#include "garden/system/settings.hpp"

using namespace garden;

//--------------------------------------------------------------------------------------------------
static void createGBuffers(GraphicsSystem* graphicsSystem,
	int2 framebufferSize, ID<Image>* gBuffers)
{
	const Image::Mips mips = { { nullptr } };
	gBuffers[0] = graphicsSystem->createImage(Image::Format::SrgbR8G8B8A8,
		Image::Bind::ColorAttachment | Image::Bind::Sampled | Image::Bind::Fullscreen,
		mips, framebufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, gBuffers[0], "image.deferred.gBuffer0");

	gBuffers[1] = graphicsSystem->createImage(Image::Format::UnormA2R10G10B10,
		Image::Bind::ColorAttachment | Image::Bind::Sampled | Image::Bind::Fullscreen,
		mips, framebufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, gBuffers[1], "image.deferred.gBuffer1");

	gBuffers[2] = graphicsSystem->createImage(Image::Format::UnormR8G8B8A8,
		Image::Bind::ColorAttachment | Image::Bind::Sampled | Image::Bind::Fullscreen,
		mips, framebufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, gBuffers[2], "image.deferred.gBuffer2");
}
static void destroyGBuffers(GraphicsSystem* graphicsSystem, const ID<Image>* gBuffers)
{
	for (uint32 i = 0; i < G_BUFFER_COUNT; i++)
		graphicsSystem->destroy(gBuffers[i]);
}

//--------------------------------------------------------------------------------------------------
static ID<Image> createDepthBuffer(GraphicsSystem* graphicsSystem, int2 framebufferSize)
{
	auto image = graphicsSystem->createImage(Image::Format::SfloatD32,
		Image::Bind::DepthStencilAttachment | Image::Bind::Sampled | Image::Bind::Fullscreen,
		{ { nullptr } }, framebufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, image, "image.deferred.depth");
	return image;
}
static ID<Image> createHdrBuffer(GraphicsSystem* graphicsSystem, int2 framebufferSize)
{
	auto image = graphicsSystem->createImage(Image::Format::SfloatR16G16B16A16,
		Image::Bind::ColorAttachment | Image::Bind::Sampled | Image::Bind::Fullscreen,
		{ { nullptr } }, framebufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, image, "image.deferred.hdr");
	return image;
}
static ID<Image> createLdrBuffer(GraphicsSystem* graphicsSystem, int2 framebufferSize)
{
	auto swapchainView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());
	auto swapchainImageView = graphicsSystem->get(
		swapchainView->getColorAttachments()[0].imageView);
	auto image = graphicsSystem->createImage(swapchainImageView->getFormat(), 
		Image::Bind::ColorAttachment | Image::Bind::Sampled | Image::Bind::Fullscreen |
		Image::Bind::TransferSrc, { { nullptr } }, framebufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, image, "image.deferred.ldr");
	return image;
}

//--------------------------------------------------------------------------------------------------
static ID<Framebuffer> createGFramebuffer(GraphicsSystem* graphicsSystem,
	const ID<Image>* gBuffers, ID<Image> depthBuffer, int2 framebufferSize)
{
	auto gBuffer0View = graphicsSystem->get(gBuffers[0]);
	auto gBuffer1View = graphicsSystem->get(gBuffers[1]);
	auto gBuffer2View = graphicsSystem->get(gBuffers[2]);
	auto depthBufferView = graphicsSystem->get(depthBuffer);

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{
		Framebuffer::OutputAttachment(
			gBuffer0View->getDefaultView(), false, false, true),
		Framebuffer::OutputAttachment(
			gBuffer1View->getDefaultView(), false, false, true),
		Framebuffer::OutputAttachment(
			gBuffer2View->getDefaultView(), false, false, true),
	};

	Framebuffer::OutputAttachment depthStencilAttachment(
		depthBufferView->getDefaultView(), true, false, true);

	auto framebuffer = graphicsSystem->createFramebuffer(
		framebufferSize, std::move(colorAttachments), depthStencilAttachment); 
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, framebuffer,
		"framebuffer.deferred.gBuffer");
	return framebuffer;
}

//--------------------------------------------------------------------------------------------------
static ID<Framebuffer> createHdrFramebuffer(GraphicsSystem* graphicsSystem,
	ID<Image> depthBuffer, ID<Image> hdrBuffer, int2 framebufferSize)
{
	auto depthBufferView = graphicsSystem->get(depthBuffer);
	auto hdrBufferView = graphicsSystem->get(hdrBuffer);
	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(hdrBufferView->getDefaultView(), false, true, true) };
	Framebuffer::OutputAttachment depthStencilAttachment(
		depthBufferView->getDefaultView(), false, true, true);
	auto framebuffer = graphicsSystem->createFramebuffer(
		framebufferSize, std::move(colorAttachments), depthStencilAttachment);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, framebuffer, "framebuffer.deferred.hdr");
	return framebuffer;
}
static ID<Framebuffer> createLdrFramebuffer(
	GraphicsSystem* graphicsSystem, ID<Image> ldrBuffer, int2 framebufferSize)
{
	auto ldrBufferView = graphicsSystem->get(ldrBuffer);
	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(ldrBufferView->getDefaultView(), false, true, true) };
	auto framebuffer = graphicsSystem->createFramebuffer(
		framebufferSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, framebuffer, "framebuffer.deferred.ldr");
	return framebuffer;
}

//--------------------------------------------------------------------------------------------------
void DeferredRenderSystem::initialize()
{
	auto manager = getManager();
	auto settingsSystem = manager->tryGet<SettingsSystem>();
	if (settingsSystem) settingsSystem->getFloat("renderScale", renderScale);

	auto graphicsSystem = getGraphicsSystem();
	GARDEN_ASSERT(isAsync == graphicsSystem->isUseThreading());

	framebufferSize = max((int2)(float2(
		graphicsSystem->getFramebufferSize()) * renderScale), int2(1));

	if (!gBuffers[0]) createGBuffers(graphicsSystem, framebufferSize, gBuffers);
	if (!depthBuffer) depthBuffer = createDepthBuffer(graphicsSystem, framebufferSize);
	if (!hdrBuffer) hdrBuffer = createHdrBuffer(graphicsSystem, framebufferSize);
	if (!ldrBuffer) ldrBuffer = createLdrBuffer(graphicsSystem, framebufferSize);
	
	if (!gFramebuffer)
	{
		gFramebuffer = createGFramebuffer(graphicsSystem,
			gBuffers, depthBuffer, framebufferSize);
	}
	if (!hdrFramebuffer)
	{
		hdrFramebuffer = createHdrFramebuffer(graphicsSystem,
			depthBuffer, hdrBuffer, framebufferSize);
	}
	if (!ldrFramebuffer)
	{
		ldrFramebuffer = createLdrFramebuffer(
			graphicsSystem, ldrBuffer, framebufferSize);
	}

	#if GARDEN_EDITOR
	if (!editor) editor = new DeferredEditor(this);
	#endif

	auto& subsystems = manager->getSubsystems<DeferredRenderSystem>();
	for (auto subsystem : subsystems)
	{
		auto deferredSystem = dynamic_cast<IDeferredRenderSystem*>(subsystem.system);
		GARDEN_ASSERT(deferredSystem);
		deferredSystem->deferredSystem = this;
	}
}
void DeferredRenderSystem::terminate()
{
	#if GARDEN_EDITOR
	delete (DeferredEditor*)editor;
	#endif
}

//--------------------------------------------------------------------------------------------------
void DeferredRenderSystem::render()
{
	#if GARDEN_EDITOR
	((DeferredEditor*)editor)->prepare();
	#endif

	auto graphicsSystem = getGraphicsSystem();
	auto& subsystems = getManager()->getSubsystems<DeferredRenderSystem>();
	graphicsSystem->startRecording(CommandBufferType::Frame);

	if (!subsystems.empty())
	{
		auto framebufferView = graphicsSystem->get(gFramebuffer);
		{
			SET_GPU_DEBUG_LABEL("Deferred Pass", Color::transparent);
			float4 clearColors[G_BUFFER_COUNT] =
				{ float4(0.0f), float4(0.0f), float4(0.0f) };
			framebufferView->beginRenderPass(clearColors,
				G_BUFFER_COUNT, 0.0f, 0x00, int4(0), isAsync);
			
			for (auto subsystem : subsystems)
			{
				auto deferredSystem = dynamic_cast<
					IDeferredRenderSystem*>(subsystem.system);
				deferredSystem->deferredRender();
			}

			#if GARDEN_EDITOR
			((DeferredEditor*)editor)->deferredRender();
			#endif

			framebufferView->endRenderPass();
		}

		{
			SET_GPU_DEBUG_LABEL("Pre HDR", Color::transparent);
			for (auto subsystem : subsystems)
			{
				auto deferredSystem = dynamic_cast<
					IDeferredRenderSystem*>(subsystem.system);
				deferredSystem->preHdrRender();
			}
		}

		{
			SET_GPU_DEBUG_LABEL("HDR Pass", Color::transparent);
			framebufferView = graphicsSystem->get(hdrFramebuffer);
			framebufferView->beginRenderPass(float4(0.0f), 0.0f, 0, int4(0), isAsync);

			for (auto subsystem : subsystems)
			{
				auto deferredSystem = dynamic_cast<
					IDeferredRenderSystem*>(subsystem.system);
				deferredSystem->hdrRender();
			}

			framebufferView->endRenderPass();
		}

		{
			SET_GPU_DEBUG_LABEL("Pre LDR", Color::transparent);
			for (auto subsystem : subsystems)
			{
				auto deferredSystem = dynamic_cast<
					IDeferredRenderSystem*>(subsystem.system);
				deferredSystem->preLdrRender();
			}
		}

		{
			SET_GPU_DEBUG_LABEL("LDR Pass", Color::transparent);
			framebufferView = graphicsSystem->get(ldrFramebuffer);
			framebufferView->beginRenderPass(float4(0.0f));

			for (auto subsystem : subsystems)
			{
				auto deferredSystem = dynamic_cast<
					IDeferredRenderSystem*>(subsystem.system);
				deferredSystem->ldrRender();
			}

			framebufferView->endRenderPass();
		}

		{
			SET_GPU_DEBUG_LABEL("Pre Swapchain", Color::transparent);
			for (auto subsystem : subsystems)
			{
				auto deferredSystem = dynamic_cast<
					IDeferredRenderSystem*>(subsystem.system);
				deferredSystem->preSwapchainRender();
			}
		}

		if (runSwapchainPass)
		{
			SET_GPU_DEBUG_LABEL("Swapchain Pass", Color::transparent);
			framebufferView = graphicsSystem->get(
				graphicsSystem->getSwapchainFramebuffer());
			auto& colorAttachments = framebufferView->getColorAttachments();
			auto colorImageView = graphicsSystem->get(colorAttachments[0].imageView);
			
			if (framebufferSize == graphicsSystem->getFramebufferSize())
				Image::copy(ldrBuffer, colorImageView->getImage());
			else
				Image::blit(ldrBuffer, colorImageView->getImage(), SamplerFilter::Linear);
		}
	}

	#if GARDEN_EDITOR
	((DeferredEditor*)editor)->render();
	#endif

	graphicsSystem->stopRecording();
}

//--------------------------------------------------------------------------------------------------
void DeferredRenderSystem::recreateSwapchain(const SwapchainChanges& changes)
{
	if (changes.framebufferSize)
	{
		auto graphicsSystem = getGraphicsSystem();
		framebufferSize = max((int2)(float2(
			graphicsSystem->getFramebufferSize()) * renderScale), int2(1));

		auto framebufferView = graphicsSystem->get(gFramebuffer);
		destroyGBuffers(graphicsSystem, gBuffers);
		createGBuffers(graphicsSystem, framebufferSize, gBuffers);
		graphicsSystem->destroy(depthBuffer);
		depthBuffer = createDepthBuffer(graphicsSystem, framebufferSize);
		graphicsSystem->destroy(hdrBuffer);
		hdrBuffer = createHdrBuffer(graphicsSystem, framebufferSize);

		graphicsSystem->destroy(ldrBuffer);
		ldrBuffer = createLdrBuffer(graphicsSystem, framebufferSize);
		auto ldrBufferView = graphicsSystem->get(ldrBuffer);
		Framebuffer::OutputAttachment colorAttachment(
			ldrBufferView->getDefaultView(), false, true, true);

		framebufferView = graphicsSystem->get(ldrFramebuffer);
		framebufferView->update(framebufferSize, &colorAttachment, 1);

		framebufferView = graphicsSystem->get(hdrFramebuffer);
		auto depthBufferView = graphicsSystem->get(depthBuffer);
		auto hdrBufferView = graphicsSystem->get(hdrBuffer);
		colorAttachment.imageView = hdrBufferView->getDefaultView();
		Framebuffer::OutputAttachment depthStencilAttachment(
			depthBufferView->getDefaultView(), false, true, true);
		framebufferView->update(framebufferSize,
			&colorAttachment, 1, depthStencilAttachment);

		framebufferView = graphicsSystem->get(gFramebuffer);
		auto gBuffer0View = graphicsSystem->get(gBuffers[0]);
		auto gBuffer1View = graphicsSystem->get(gBuffers[1]);
		auto gBuffer2View = graphicsSystem->get(gBuffers[2]);

		Framebuffer::OutputAttachment colorAttachments[G_BUFFER_COUNT];
		colorAttachments[0] = Framebuffer::OutputAttachment(
			gBuffer0View->getDefaultView(), false, false, true);
		colorAttachments[1] = Framebuffer::OutputAttachment(
			gBuffer1View->getDefaultView(), false, false, true);
		colorAttachments[2] = Framebuffer::OutputAttachment(
			gBuffer2View->getDefaultView(), false, false, true);

		depthStencilAttachment.clear = true;
		depthStencilAttachment.load = false;

		framebufferView->update(framebufferSize,
			colorAttachments, G_BUFFER_COUNT, depthStencilAttachment);
	}

	#if GARDEN_EDITOR
	((DeferredEditor*)editor)->recreateSwapchain(changes);
	#endif
}

//--------------------------------------------------------------------------------------------------
void DeferredRenderSystem::setRenderScale(float renderScale)
{
	if (renderScale == this->renderScale) return;
	this->renderScale = renderScale;
	IRenderSystem::SwapchainChanges swapchainChanges;
	swapchainChanges.framebufferSize = true;
	getGraphicsSystem()->recreateSwapchain(swapchainChanges);
}

//--------------------------------------------------------------------------------------------------
static int2 getScaledFrameSize(GraphicsSystem* graphicsSystem,
	Manager* manager, float& renderScale)
{
	auto settingsSystem = manager->tryGet<SettingsSystem>();
	if (settingsSystem) settingsSystem->getFloat("renderScale", renderScale);
	return max((int2)(float2(graphicsSystem->getFramebufferSize()) * renderScale), int2(1));
}

ID<Image>* DeferredRenderSystem::getGBuffers()
{
	if (!gBuffers[0])
	{
		auto graphicsSystem = getGraphicsSystem();
		framebufferSize = getScaledFrameSize(
			graphicsSystem, getManager(), renderScale);
		createGBuffers(graphicsSystem, framebufferSize, gBuffers);
	}
	return gBuffers;
}
ID<Image> DeferredRenderSystem::getDepthBuffer()
{
	if (!depthBuffer)
	{
		auto graphicsSystem = getGraphicsSystem();
		framebufferSize = getScaledFrameSize(
			graphicsSystem, getManager(), renderScale);
		depthBuffer = createDepthBuffer(graphicsSystem, framebufferSize);
	}
	return depthBuffer;
}
ID<Image> DeferredRenderSystem::getHdrBuffer()
{
	if (!hdrBuffer)
	{
		auto graphicsSystem = getGraphicsSystem();
		framebufferSize = getScaledFrameSize(
			graphicsSystem, getManager(), renderScale);
		hdrBuffer = createHdrBuffer(graphicsSystem, framebufferSize);
	}
	return hdrBuffer;
}
ID<Image> DeferredRenderSystem::getLdrBuffer()
{
	if (!ldrBuffer)
	{
		auto graphicsSystem = getGraphicsSystem();
		framebufferSize = getScaledFrameSize(
			graphicsSystem, getManager(), renderScale);
		ldrBuffer = createLdrBuffer(graphicsSystem, framebufferSize);
	}
	return ldrBuffer;
}

//--------------------------------------------------------------------------------------------------
ID<Framebuffer> DeferredRenderSystem::getGFramebuffer()
{
	if (!gFramebuffer)
	{
		auto graphicsSystem = getGraphicsSystem();
		framebufferSize = getScaledFrameSize(
			graphicsSystem, getManager(), renderScale);
		gFramebuffer = createGFramebuffer(graphicsSystem,
			getGBuffers(), getDepthBuffer(), framebufferSize);
	}
	return gFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getHdrFramebuffer()
{
	if (!hdrFramebuffer)
	{
		auto graphicsSystem = getGraphicsSystem();
		framebufferSize = getScaledFrameSize(
			graphicsSystem, getManager(), renderScale);
		hdrFramebuffer = createHdrFramebuffer(graphicsSystem,
			getDepthBuffer(), getHdrBuffer(), framebufferSize);
	}
	return hdrFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getLdrFramebuffer()
{
	if (!ldrFramebuffer)
	{
		auto graphicsSystem = getGraphicsSystem();
		framebufferSize = getScaledFrameSize(
			graphicsSystem, getManager(), renderScale);
		ldrFramebuffer = createLdrFramebuffer(
			graphicsSystem, getLdrBuffer(), framebufferSize);
	}
	return ldrFramebuffer;
}

#if GARDEN_EDITOR
ID<Framebuffer> DeferredRenderSystem::getEditorFramebuffer()
{
	if (!editor) editor = new DeferredEditor(this);
	return ((DeferredEditor*)editor)->getFramebuffer();
}
#endif