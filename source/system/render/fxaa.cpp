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

#include "garden/system/render/fxaa.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/settings.hpp"
#include "garden/profiler.hpp"

using namespace garden;

static ID<Framebuffer> createFramebuffer()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto gBuffer = deferredSystem->getGBuffers()[0]; // Note: Reusing G-Buffer memory.
	auto gBufferView = graphicsSystem->get(gBuffer)->getDefaultView(); 
	GARDEN_ASSERT(graphicsSystem->get(gBuffer)->getFormat() == DeferredRenderSystem::ldrBufferFormat);

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(gBufferView, FxaaRenderSystem::framebufferFlags) };

	auto framebuffer = graphicsSystem->createFramebuffer(
		graphicsSystem->getScaledFramebufferSize(), std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.fxaa");
	return framebuffer;
}
static ID<GraphicsPipeline> createPipeline(ID<Framebuffer> framebuffer)
{
	ResourceSystem::GraphicsOptions options;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("fxaa", framebuffer, options);
}

static DescriptorSet::Uniforms getUniforms()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto hdrFramebufferView = graphicsSystem->get(deferredSystem->getHdrFramebuffer());
	auto ldrFramebufferView = graphicsSystem->get(deferredSystem->getLdrFramebuffer());
	auto hdrBufferView = hdrFramebufferView->getColorAttachments()[0].imageView;
	auto ldrBufferView = ldrFramebufferView->getColorAttachments()[0].imageView;

	// TODO: support forward rendering too

	DescriptorSet::Uniforms uniforms =
	{ 
		{ "hdrBuffer", DescriptorSet::Uniform(hdrBufferView) },
		{ "ldrBuffer", DescriptorSet::Uniform(ldrBufferView) },
	};
	return uniforms;
}

//**********************************************************************************************************************
FxaaRenderSystem::FxaaRenderSystem(bool setSingleton) : Singleton(setSingleton)
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", FxaaRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", FxaaRenderSystem::deinit);
}
FxaaRenderSystem::~FxaaRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", FxaaRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", FxaaRenderSystem::deinit);
	}

	unsetSingleton();
}

void FxaaRenderSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("PreUiRender", FxaaRenderSystem::preUiRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", FxaaRenderSystem::gBufferRecreate);

	auto settingsSystem = SettingsSystem::Instance::tryGet();
	if (settingsSystem)
		settingsSystem->getBool("fxaa.isEnabled", isEnabled);

	if (isEnabled)
	{
		if (!framebuffer)
			framebuffer = createFramebuffer();
		if (!pipeline)
			pipeline = createPipeline(framebuffer);
		isInitialized = true;
	}
}
void FxaaRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(descriptorSet);
		graphicsSystem->destroy(framebuffer);
		graphicsSystem->destroy(pipeline);

		ECSM_UNSUBSCRIBE_FROM_EVENT("PreUiRender", FxaaRenderSystem::preUiRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", FxaaRenderSystem::gBufferRecreate);
	}
}

//**********************************************************************************************************************
void FxaaRenderSystem::preUiRender()
{
	SET_CPU_ZONE_SCOPED("FXAA Pre UI Render");

	if (!isEnabled)
		return;
	
	if (!isInitialized)
	{
		if (!framebuffer)
			framebuffer = createFramebuffer();
		if (!pipeline)
			pipeline = createPipeline(framebuffer);
		isInitialized = true;
	}

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(pipeline);
	if (!pipelineView->isReady())
		return;

	if (!descriptorSet)
	{
		auto uniforms = getUniforms();
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.fxaa");
	}

	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto framebufferView = graphicsSystem->get(framebuffer);
	auto fxaaBufferView = graphicsSystem->get(framebufferView->getColorAttachments()[0].imageView);

	PushConstants pc;
	pc.invFrameSize = float2::one / framebufferView->getSize();

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		SET_GPU_DEBUG_LABEL("FXAA", Color::transparent);
		framebufferView->beginRenderPass(float4::zero);
		pipelineView->bind();
		pipelineView->setViewportScissor();
		pipelineView->bindDescriptorSet(descriptorSet);
		pipelineView->pushConstants(&pc);
		pipelineView->drawFullscreen();
		framebufferView->endRenderPass();
		Image::copy(fxaaBufferView->getImage(), deferredSystem->getLdrBuffer());
	}
	graphicsSystem->stopRecording();
}

void FxaaRenderSystem::gBufferRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (framebuffer)
	{
		auto deferredSystem = DeferredRenderSystem::Instance::get();
		auto gBuffer = deferredSystem->getGBuffers()[0]; // Note: Reusing G-Buffer memory.
		auto gBufferView = graphicsSystem->get(gBuffer)->getDefaultView();
		GARDEN_ASSERT(graphicsSystem->get(gBuffer)->getFormat() == DeferredRenderSystem::ldrBufferFormat);

		auto framebufferView = graphicsSystem->get(framebuffer);
		Framebuffer::OutputAttachment colorAttachment(gBufferView, FxaaRenderSystem::framebufferFlags);
		framebufferView->update(graphicsSystem->getScaledFramebufferSize(), &colorAttachment, 1);
	}
	if (descriptorSet)
	{
		graphicsSystem->destroy(descriptorSet);
		descriptorSet = {};
	}
}

ID<Framebuffer> FxaaRenderSystem::getFramebuffer()
{
	if (!framebuffer)
		framebuffer = createFramebuffer();
	return framebuffer;
}
ID<GraphicsPipeline> FxaaRenderSystem::getPipeline()
{
	if (!pipeline)
		pipeline = createPipeline(getFramebuffer());
	return pipeline;
}