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

#include "garden/system/render/blur.hpp"
#include "garden/system/render/gpu-process.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/profiler.hpp"

using namespace garden;

static ID<ImageView> getLdrGgxView(GraphicsSystem* graphicsSystem, DeferredRenderSystem* deferredSystem)
{
	auto gBuffer = deferredSystem->getGBuffers()[DeferredRenderSystem::gBufferBaseColor]; 
	auto gBufferView = graphicsSystem->get(gBuffer)->getView(); // Note: Reusing G-Buffer memory.
	GARDEN_ASSERT(graphicsSystem->get(gBuffer)->getFormat() == DeferredRenderSystem::ldrBufferFormat);
	return gBufferView;
}

//**********************************************************************************************************************
BlurRenderSystem::BlurRenderSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", BlurRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", BlurRenderSystem::deinit);
}
BlurRenderSystem::~BlurRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", BlurRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", BlurRenderSystem::deinit);
	}

	unsetSingleton();
}

void BlurRenderSystem::init()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("PreDepthLdrRender", BlurRenderSystem::preDepthLdrRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", BlurRenderSystem::gBufferRecreate);
}
void BlurRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(ldrGgxFramebuffers[0]);
		graphicsSystem->destroy(ldrGgxFramebuffers[1]);
		graphicsSystem->destroy(ldrGgxDS);
		graphicsSystem->destroy(ldrGgxPipeline);

		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreLdrRender", BlurRenderSystem::preDepthLdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", BlurRenderSystem::gBufferRecreate);
	}
}

//**********************************************************************************************************************
void BlurRenderSystem::preDepthLdrRender()
{
	SET_CPU_ZONE_SCOPED("Blur Pre Depth LDR Render");

	if (!ldrGgxBlur || intensity <= 0.0f)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto gpuProcessSystem = GpuProcessSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getScaledFrameSize();
	auto kernelBuffer = gpuProcessSystem->getGgxBlurKernel();
	auto ldrBuffer = deferredSystem->getLdrBuffer();
	auto ldrBufferView = graphicsSystem->get(ldrBuffer)->getView();

	if (!ldrGgxFramebuffers[0])
	{
		vector<Framebuffer::OutputAttachment> colorAttachments =
		{ Framebuffer::OutputAttachment(ldrBufferView, framebufferFlags) };
		ldrGgxFramebuffers[0] = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
		SET_RESOURCE_DEBUG_NAME(ldrGgxFramebuffers[0], "framebuffer.ldrGgxBlur0");

		colorAttachments = { Framebuffer::OutputAttachment(
			getLdrGgxView(graphicsSystem, deferredSystem), framebufferFlags) };
		ldrGgxFramebuffers[1] = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
		SET_RESOURCE_DEBUG_NAME(ldrGgxFramebuffers[1], "framebuffer.ldrGgxBlur1");
	}

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		gpuProcessSystem->gaussianBlur(ldrBufferView, ldrGgxFramebuffers[0], 
			ldrGgxFramebuffers[1], kernelBuffer, intensity, false, ldrGgxPipeline, ldrGgxDS);
	}
	graphicsSystem->stopRecording();
}
void BlurRenderSystem::gBufferRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	graphicsSystem->destroy(ldrGgxDS);

	if (ldrGgxFramebuffers[0])
	{
		auto deferredSystem = DeferredRenderSystem::Instance::get();
		auto ldrBuffer = deferredSystem->getLdrBuffer();
		auto ldrBufferView = graphicsSystem->get(ldrBuffer)->getView();
		auto framebufferSize = graphicsSystem->getScaledFrameSize();
		auto framebufferView = graphicsSystem->get(ldrGgxFramebuffers[0]);
		Framebuffer::OutputAttachment colorAttachment(ldrBufferView, framebufferFlags);
		framebufferView->update(framebufferSize, &colorAttachment, 1);

		framebufferView = graphicsSystem->get(ldrGgxFramebuffers[1]);
		colorAttachment = Framebuffer::OutputAttachment(
			getLdrGgxView(graphicsSystem, deferredSystem), framebufferFlags);
		framebufferView->update(framebufferSize, &colorAttachment, 1);
	}
}