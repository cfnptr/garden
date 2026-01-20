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

#include "garden/system/render/fxaa.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/settings.hpp"
#include "garden/profiler.hpp"

using namespace garden;

static ID<ImageView> getLdrCopyView(GraphicsSystem* graphicsSystem, DeferredRenderSystem* deferredSystem)
{
	auto gBuffer = deferredSystem->getGBuffers()[DeferredRenderSystem::gBufferBaseColor];
	auto gBufferView = graphicsSystem->get(gBuffer)->getDefaultView(); // Note: Reusing G-Buffer memory.
	GARDEN_ASSERT(graphicsSystem->get(gBuffer)->getFormat() == DeferredRenderSystem::ldrBufferFormat);
	return gBufferView;
}
static ID<Framebuffer> createFramebuffer(GraphicsSystem* graphicsSystem)
{
	auto ldrBuffer = DeferredRenderSystem::Instance::get()->getLdrBuffer();
	auto ldrBufferView = graphicsSystem->get(ldrBuffer)->getDefaultView();
	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(ldrBufferView, FxaaRenderSystem::framebufferFlags) };

	auto framebuffer = graphicsSystem->createFramebuffer(
		graphicsSystem->getScaledFrameSize(), std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.fxaa");
	return framebuffer;
}
static ID<GraphicsPipeline> createPipeline(ID<Framebuffer> framebuffer, GraphicsQuality quality, float subpixelQuality)
{
	float edgeThrMax, edgeThrMin;
	switch (quality)
	{
		case GraphicsQuality::PotatoPC: edgeThrMax = 0.333f; edgeThrMin = 0.0833f; break;
		case GraphicsQuality::Low: edgeThrMax = 0.25f; edgeThrMin = 0.0625f; break;
		case GraphicsQuality::Medium: edgeThrMax = 0.166; edgeThrMin = 0.0625f; break;
		case GraphicsQuality::High: edgeThrMax = 0.125f; edgeThrMin = 0.0312f; break;
		case GraphicsQuality::Ultra: edgeThrMax = 0.063f; edgeThrMin = 0.0312f; break;
		default: abort();
	}

	Pipeline::SpecConstValues specConstValues =
	{
		{ "EDGE_THRESHOLD_MIN", Pipeline::SpecConstValue(edgeThrMin) },
		{ "EDGE_THRESHOLD_MAX", Pipeline::SpecConstValue(edgeThrMax) },
		{ "SUBPIXEL_QUALITY", Pipeline::SpecConstValue(subpixelQuality) }
	};

	ResourceSystem::GraphicsOptions options;
	options.specConstValues = &specConstValues;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("fxaa", framebuffer, options);
}

static DescriptorSet::Uniforms getUniforms(GraphicsSystem* graphicsSystem)
{
	// TODO: Support forward rendering too.
	auto ldrBufferView = getLdrCopyView(graphicsSystem, DeferredRenderSystem::Instance::get()); 
	return { { "ldrBuffer", DescriptorSet::Uniform(ldrBufferView) } };
}

//**********************************************************************************************************************
FxaaRenderSystem::FxaaRenderSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", FxaaRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", FxaaRenderSystem::deinit);
}
FxaaRenderSystem::~FxaaRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", FxaaRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", FxaaRenderSystem::deinit);
	}

	unsetSingleton();
}

void FxaaRenderSystem::init()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("PreUiRender", FxaaRenderSystem::preUiRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", FxaaRenderSystem::gBufferRecreate);
	ECSM_SUBSCRIBE_TO_EVENT("QualityChange", FxaaRenderSystem::qualityChange);

	auto settingsSystem = SettingsSystem::Instance::tryGet();
	if (settingsSystem)
	{
		settingsSystem->getBool("fxaa.enabled", isEnabled);
		settingsSystem->getType("fxaa.quality", quality, graphicsQualityNames, (uint32)GraphicsQuality::Count);
		settingsSystem->getFloat("fxaa.subpixelQuality", subpixelQuality);
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

		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreUiRender", FxaaRenderSystem::preUiRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", FxaaRenderSystem::gBufferRecreate);
		ECSM_UNSUBSCRIBE_FROM_EVENT("QualityChange", FxaaRenderSystem::qualityChange);
	}
}

//**********************************************************************************************************************
void FxaaRenderSystem::preUiRender()
{
	SET_CPU_ZONE_SCOPED("FXAA Pre UI Render");

	if (!isEnabled || subpixelQuality <= 0.0f)
		return;
	
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!isInitialized)
	{
		if (!framebuffer)
			framebuffer = createFramebuffer(graphicsSystem);
		if (!pipeline)
			pipeline = createPipeline(framebuffer, quality, subpixelQuality);
		isInitialized = true;
	}

	auto pipelineView = graphicsSystem->get(pipeline);
	if (!pipelineView->isReady())
		return;

	if (!descriptorSet)
	{
		auto uniforms = getUniforms(graphicsSystem);
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.fxaa");
	}

	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto framebufferView = graphicsSystem->get(framebuffer);
	auto ldrCopyView = graphicsSystem->get(getLdrCopyView(graphicsSystem, deferredSystem));

	PushConstants pc;
	pc.invFrameSize = float2::one / framebufferView->getSize();

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		SET_GPU_DEBUG_LABEL("FXAA");
		Image::copy(deferredSystem->getLdrBuffer(), ldrCopyView->getImage());

		#if GARDEN_DEBUG || GARDEN_EDITOR
		if (visualize)
			graphicsSystem->get(deferredSystem->getLdrBuffer())->clear(float4::zero);
		#endif

		{
			RenderPass renderPass(framebuffer, float4::zero);
			pipelineView->bind();
			pipelineView->setViewportScissor();
			pipelineView->bindDescriptorSet(descriptorSet);
			pipelineView->pushConstants(&pc);
			pipelineView->drawFullscreen();
		}
	}
	graphicsSystem->stopRecording();
}

void FxaaRenderSystem::gBufferRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (framebuffer)
	{
		auto framebufferView = graphicsSystem->get(framebuffer);
		auto ldrBuffer = DeferredRenderSystem::Instance::get()->getLdrBuffer();
		auto ldrBufferView = graphicsSystem->get(ldrBuffer)->getDefaultView();
		Framebuffer::OutputAttachment colorAttachment(ldrBufferView, FxaaRenderSystem::framebufferFlags);
		framebufferView->update(graphicsSystem->getScaledFrameSize(), &colorAttachment, 1);
	}
	if (descriptorSet)
	{
		graphicsSystem->destroy(descriptorSet);
		descriptorSet = {};
	}
}
void FxaaRenderSystem::qualityChange()
{
	setQuality(GraphicsSystem::Instance::get()->quality, subpixelQuality);
}

void FxaaRenderSystem::setQuality(GraphicsQuality quality, float subpixelQuality)
{
	GARDEN_ASSERT(subpixelQuality >= 0.0f);
	GARDEN_ASSERT(subpixelQuality <= 1.0f);

	if (this->quality == quality && this->subpixelQuality == subpixelQuality)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (descriptorSet)
	{
		graphicsSystem->destroy(descriptorSet);
		descriptorSet = {};
	}
	if (pipeline)
	{
		graphicsSystem->destroy(pipeline);
		pipeline = createPipeline(framebuffer, quality, subpixelQuality);
	}

	this->quality = quality;
	this->subpixelQuality = subpixelQuality;
}

ID<Framebuffer> FxaaRenderSystem::getFramebuffer()
{
	if (!framebuffer)
		framebuffer = createFramebuffer(GraphicsSystem::Instance::get());
	return framebuffer;
}
ID<GraphicsPipeline> FxaaRenderSystem::getPipeline()
{
	if (!pipeline)
		pipeline = createPipeline(getFramebuffer(), quality, subpixelQuality);
	return pipeline;
}