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

#include "garden/system/render/smaa.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/settings.hpp"
#include "garden/profiler.hpp"

using namespace garden;

static Ref<Image> createSearchLUT()
{
	return ResourceSystem::Instance::get()->loadImage("smaa/search", Image::Format::UnormR8, // TODO: BC4_UNORM
		Image::Usage::Sampled | Image::Usage::TransferDst | Image::Usage::TransferQ, 1, 
		Image::Strategy::Size, ImageLoadFlags::LoadShared, 9.0f);
}
static Ref<Image> createAreaLUT()
{
	return ResourceSystem::Instance::get()->loadImage("smaa/area", Image::Format::UnormR8G8, // TODO: BC5_UNORM
		Image::Usage::Sampled | Image::Usage::TransferDst | Image::Usage::TransferQ, 1, 
		Image::Strategy::Size, ImageLoadFlags::LoadShared, 9.0f);
}


static ID<Image> createEdgesBuffer(GraphicsSystem* graphicsSystem)
{
	auto framebufferSize = graphicsSystem->getScaledFrameSize();
	auto edgesBuffer = graphicsSystem->createImage(SmaaRenderSystem::edgesBufferFormat, Image::Usage::ColorAttachment | 
		Image::Usage::Sampled | Image::Usage::Fullscreen, { { nullptr } }, framebufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(edgesBuffer, "image.smaa.edgesBuffer");
	return edgesBuffer;
}
static ID<ImageView> getLdrCopyView(GraphicsSystem* graphicsSystem, DeferredRenderSystem* deferredSystem)
{
	auto gBuffer = deferredSystem->getGBuffers()[DeferredRenderSystem::gBufferBaseColor]; 
	auto gBufferView = graphicsSystem->get(gBuffer)->getDefaultView(); // Note: Reusing G-Buffer memory.
	GARDEN_ASSERT(graphicsSystem->get(gBuffer)->getFormat() == DeferredRenderSystem::ldrBufferFormat);
	return gBufferView;
}
static ID<ImageView> getWeightsView(GraphicsSystem* graphicsSystem, DeferredRenderSystem* deferredSystem)
{
	auto gBuffer = deferredSystem->getGBuffers()[DeferredRenderSystem::gBufferMetallic]; 
	auto gBufferView = graphicsSystem->get(gBuffer)->getDefaultView(); // Note: Reusing G-Buffer memory.
	GARDEN_ASSERT(graphicsSystem->get(gBuffer)->getFormat() == Image::Format::UnormR8G8B8A8);
	return gBufferView;
}

static ID<Framebuffer> createEdgesFramebuffer(GraphicsSystem* graphicsSystem, ID<Image> edgesBuffer)
{
	auto edgesView = graphicsSystem->get(edgesBuffer)->getDefaultView();
	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(edgesView, SmaaRenderSystem::processFbFlags) };

	auto framebuffer = graphicsSystem->createFramebuffer(
		graphicsSystem->getScaledFrameSize(), std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.smaa.edges");
	return framebuffer;
}
static ID<Framebuffer> createWeightsFramebuffer(GraphicsSystem* graphicsSystem)
{
	auto weightsView = getWeightsView(graphicsSystem, DeferredRenderSystem::Instance::get());
	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(weightsView, SmaaRenderSystem::processFbFlags) };

	auto framebuffer = graphicsSystem->createFramebuffer(
		graphicsSystem->getScaledFrameSize(), std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.smaa.weights");
	return framebuffer;
}
static ID<Framebuffer> createBlendFramebuffer(GraphicsSystem* graphicsSystem)
{
	auto ldrBuffer = DeferredRenderSystem::Instance::get()->getLdrBuffer();
	auto ldrBufferView = graphicsSystem->get(ldrBuffer)->getDefaultView();
	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(ldrBufferView, SmaaRenderSystem::blendFbFlags) };

	auto framebuffer = graphicsSystem->createFramebuffer(
		graphicsSystem->getScaledFrameSize(), std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.smaa.blend");
	return framebuffer;
}

//**********************************************************************************************************************
static ID<GraphicsPipeline> createEdgesPipeline(ID<Framebuffer> edgesFramebuffer, GraphicsQuality quality)
{
	float threshold;
	switch (quality)
	{
		case GraphicsQuality::PotatoPC: threshold = 0.15f; break;
		case GraphicsQuality::Low: threshold = 0.1f; break;
		case GraphicsQuality::Medium: threshold = 0.1; break;
		case GraphicsQuality::High: threshold = 0.05f; break;
		case GraphicsQuality::Ultra: threshold = 0.025f; break;
		default: abort();
	}
	Pipeline::SpecConstValues specConstValues = { { "THRESHOLD", Pipeline::SpecConstValue(threshold) } };

	ResourceSystem::GraphicsOptions options;
	options.specConstValues = &specConstValues;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("smaa/edges", edgesFramebuffer, options);
}
static ID<GraphicsPipeline> createWeightsPipeline(ID<Framebuffer> weightsFramebuffer, 
	GraphicsQuality quality, int32 cornerRounding)
{
	float maxSearchSteps; int32 maxSearchStepsDiag;
	switch (quality)
	{
		case GraphicsQuality::PotatoPC: maxSearchSteps = 2.0f; maxSearchStepsDiag = 0; break;
		case GraphicsQuality::Low: maxSearchSteps = 4.0f; maxSearchStepsDiag = 0; break;
		case GraphicsQuality::Medium: maxSearchSteps = 8.0f; maxSearchStepsDiag = 4; break;
		case GraphicsQuality::High: maxSearchSteps = 16.0f; maxSearchStepsDiag = 8; break;
		case GraphicsQuality::Ultra: maxSearchSteps = 32.0f; maxSearchStepsDiag = 16; break;
		default: abort();
	}

	if (quality < GraphicsQuality::Medium)
		cornerRounding = 100; // Disable rounding.

	Pipeline::SpecConstValues specConstValues =
	{
		{ "MAX_SEARCH_STEPS", Pipeline::SpecConstValue(maxSearchSteps) },
		{ "MAX_SEARCH_STEPS_DIAG", Pipeline::SpecConstValue(maxSearchStepsDiag) },
		{ "CORNER_ROUNDING", Pipeline::SpecConstValue(cornerRounding) },
	};

	ResourceSystem::GraphicsOptions options;
	options.specConstValues = &specConstValues;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("smaa/weights", weightsFramebuffer, options);
}
static ID<GraphicsPipeline> createBlendPipeline(ID<Framebuffer> blendFramebuffer)
{
	ResourceSystem::GraphicsOptions options;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("smaa/blend", blendFramebuffer, options);
}

static DescriptorSet::Uniforms getEdgesUniforms(GraphicsSystem* graphicsSystem)
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto ldrFramebufferView = graphicsSystem->get(deferredSystem->getLdrFramebuffer()); // TODO: support forward rendering too
	auto ldrBufferView = ldrFramebufferView->getColorAttachments()[0].imageView;
	return { { "ldrBuffer", DescriptorSet::Uniform(ldrBufferView) } };
}
static DescriptorSet::Uniforms getWeightsUniforms(GraphicsSystem* graphicsSystem, 
	ID<Image> areaLUT, ID<Image> searchLUT, ID<Image> edgesBuffer)
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto areaLutView = graphicsSystem->get(areaLUT)->getDefaultView();
	auto searchLutView = graphicsSystem->get(searchLUT)->getDefaultView();
	auto edgesView = graphicsSystem->get(edgesBuffer)->getDefaultView();

	DescriptorSet::Uniforms uniforms =
	{ 
		{ "areaLUT", DescriptorSet::Uniform(areaLutView) },
		{ "searchLUT", DescriptorSet::Uniform(searchLutView) },
		{ "edgesBuffer", DescriptorSet::Uniform(edgesView) }
	};
	return uniforms;
}
static DescriptorSet::Uniforms getBlendUniforms(GraphicsSystem* graphicsSystem)
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto weightsView = getWeightsView(graphicsSystem, deferredSystem);
	auto ldrCopyView = getLdrCopyView(graphicsSystem, deferredSystem); 

	DescriptorSet::Uniforms uniforms =
	{ 
		{ "weightsBuffer", DescriptorSet::Uniform(weightsView) },
		{ "ldrBuffer", DescriptorSet::Uniform(ldrCopyView) }
	};
	return uniforms;
}

//**********************************************************************************************************************
SmaaRenderSystem::SmaaRenderSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", SmaaRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", SmaaRenderSystem::deinit);
}
SmaaRenderSystem::~SmaaRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", SmaaRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", SmaaRenderSystem::deinit);
	}

	unsetSingleton();
}

void SmaaRenderSystem::init()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("PreUiRender", SmaaRenderSystem::preUiRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", SmaaRenderSystem::gBufferRecreate);
	ECSM_SUBSCRIBE_TO_EVENT("QualityChange", SmaaRenderSystem::qualityChange);

	auto settingsSystem = SettingsSystem::Instance::tryGet();
	if (settingsSystem)
	{
		settingsSystem->getBool("smaa.enabled", isEnabled);
		settingsSystem->getType("smaa.quality", quality, graphicsQualityNames, (uint32)GraphicsQuality::Count);
	}
}
void SmaaRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(blendDS);
		graphicsSystem->destroy(weightsDS);
		graphicsSystem->destroy(edgesDS);
		graphicsSystem->destroy(edgesBuffer);
		graphicsSystem->destroy(blendFramebuffer);
		graphicsSystem->destroy(weightsFramebuffer);
		graphicsSystem->destroy(edgesFramebuffer);
		graphicsSystem->destroy(blendPipeline);
		graphicsSystem->destroy(weightsPipeline);
		graphicsSystem->destroy(edgesPipeline);
		graphicsSystem->destroy(areaLUT);
		graphicsSystem->destroy(searchLUT);

		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreUiRender", SmaaRenderSystem::preUiRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", SmaaRenderSystem::gBufferRecreate);
		ECSM_UNSUBSCRIBE_FROM_EVENT("QualityChange", SmaaRenderSystem::qualityChange);
	}
}

//**********************************************************************************************************************
void SmaaRenderSystem::preUiRender()
{
	SET_CPU_ZONE_SCOPED("SMAA Pre UI Render");

	if (!isEnabled)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!isInitialized)
	{
		if (!searchLUT)
			searchLUT = createSearchLUT();
		if (!areaLUT)
			areaLUT = createAreaLUT();
		if (!edgesBuffer)
			edgesBuffer = createEdgesBuffer(graphicsSystem);
		if (!edgesFramebuffer)
			edgesFramebuffer = createEdgesFramebuffer(graphicsSystem, edgesBuffer);
		if (!weightsFramebuffer)
			weightsFramebuffer = createWeightsFramebuffer(graphicsSystem);
		if (!blendFramebuffer)
			blendFramebuffer = createBlendFramebuffer(graphicsSystem);
		if (!edgesPipeline)
			edgesPipeline = createEdgesPipeline(edgesFramebuffer, quality);
		if (!weightsPipeline)
			weightsPipeline = createWeightsPipeline(weightsFramebuffer, quality, cornerRounding);
		if (!blendPipeline)
			blendPipeline = createBlendPipeline(blendFramebuffer);
		isInitialized = true;
	}

	auto edgesPipelineView = graphicsSystem->get(edgesPipeline);
	auto weightsPipelineView = graphicsSystem->get(weightsPipeline);
	auto blendPipelineView = graphicsSystem->get(blendPipeline);
	if (!edgesPipelineView->isReady() || !weightsPipelineView->isReady() || !blendPipelineView->isReady())
		return;

	if (!edgesDS)
	{
		auto uniforms = getEdgesUniforms(graphicsSystem);
		edgesDS = graphicsSystem->createDescriptorSet(edgesPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(edgesDS, "descriptorSet.smaa.edges");
	}
	if (!weightsDS)
	{
		auto uniforms = getWeightsUniforms(graphicsSystem, 
			ID<Image>(areaLUT), ID<Image>(searchLUT), edgesBuffer);
		weightsDS = graphicsSystem->createDescriptorSet(weightsPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(weightsDS, "descriptorSet.smaa.weights");
	}
	if (!blendDS)
	{
		auto uniforms = getBlendUniforms(graphicsSystem);
		blendDS = graphicsSystem->createDescriptorSet(blendPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(blendDS, "descriptorSet.smaa.blend");
	}

	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto framebufferView = graphicsSystem->get(edgesFramebuffer);
	auto ldrCopyView = graphicsSystem->get(getLdrCopyView(graphicsSystem, deferredSystem));

	PushConstants pc;
	pc.frameSize = framebufferView->getSize();
	pc.invFrameSize = float2::one / pc.frameSize;

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		SET_GPU_DEBUG_LABEL("SMAA");
		Image::copy(deferredSystem->getLdrBuffer(), ldrCopyView->getImage());

		{
			SET_GPU_DEBUG_LABEL("Edges Detection");
			{
				RenderPass renderPass(edgesFramebuffer, float4::zero);
				edgesPipelineView->bind();
				edgesPipelineView->setViewportScissor();
				edgesPipelineView->bindDescriptorSet(edgesDS);
				edgesPipelineView->pushConstants(&pc);
				edgesPipelineView->drawFullscreen();
			}
		}
		{
			SET_GPU_DEBUG_LABEL("Weights Calculation");
			{
				RenderPass renderPass(weightsFramebuffer, float4::zero);
				weightsPipelineView->bind();
				weightsPipelineView->setViewportScissor();
				weightsPipelineView->bindDescriptorSet(weightsDS);
				weightsPipelineView->pushConstants(&pc);
				weightsPipelineView->drawFullscreen();
			}
		}

		#if GARDEN_DEBUG || GARDEN_EDITOR
		if (visualize)
			graphicsSystem->get(deferredSystem->getLdrBuffer())->clear(float4::zero);
		#endif

		{
			SET_GPU_DEBUG_LABEL("Neighborhood Blending");
			{
				RenderPass renderPass(blendFramebuffer, float4::zero);
				blendPipelineView->bind();
				blendPipelineView->setViewportScissor();
				blendPipelineView->bindDescriptorSet(blendDS);
				blendPipelineView->pushConstants(&pc);
				blendPipelineView->drawFullscreen();
			}
		}
	}
	graphicsSystem->stopRecording();
}

//**********************************************************************************************************************
void SmaaRenderSystem::gBufferRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (edgesBuffer)
	{
		graphicsSystem->destroy(edgesBuffer);
		edgesBuffer = createEdgesBuffer(graphicsSystem);
	}

	if (edgesFramebuffer)
	{
		auto framebufferView = graphicsSystem->get(edgesFramebuffer);
		auto edgesView = graphicsSystem->get(edgesBuffer)->getDefaultView();
		Framebuffer::OutputAttachment colorAttachment(edgesView, processFbFlags);
		framebufferView->update(graphicsSystem->getScaledFrameSize(), &colorAttachment, 1);
	}
	if (weightsFramebuffer)
	{
		auto framebufferView = graphicsSystem->get(weightsFramebuffer);
		auto weightsView = getWeightsView(graphicsSystem, DeferredRenderSystem::Instance::get());
		Framebuffer::OutputAttachment colorAttachment(weightsView, processFbFlags);
		framebufferView->update(graphicsSystem->getScaledFrameSize(), &colorAttachment, 1);
	}
	if (blendFramebuffer)
	{
		auto framebufferView = graphicsSystem->get(blendFramebuffer);
		auto ldrBuffer = DeferredRenderSystem::Instance::get()->getLdrBuffer();
		auto ldrBufferView = graphicsSystem->get(ldrBuffer)->getDefaultView();
		Framebuffer::OutputAttachment colorAttachment(ldrBufferView, blendFbFlags);
		framebufferView->update(graphicsSystem->getScaledFrameSize(), &colorAttachment, 1);
	}

	if (blendDS)
	{
		graphicsSystem->destroy(blendDS); blendDS = {};
	}
	if (weightsDS)
	{
		graphicsSystem->destroy(weightsDS); weightsDS = {};
	}
	if (edgesDS)
	{
		graphicsSystem->destroy(edgesDS); edgesDS = {};
	}
}

//**********************************************************************************************************************
void SmaaRenderSystem::qualityChange()
{
	setQuality(GraphicsSystem::Instance::get()->quality);
}

void SmaaRenderSystem::setQuality(GraphicsQuality quality, int cornerRounding)
{
	GARDEN_ASSERT(cornerRounding >= 0);
	GARDEN_ASSERT(cornerRounding <= 100);

	if (this->quality == quality && this->cornerRounding == cornerRounding)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (blendDS)
	{
		graphicsSystem->destroy(blendDS); blendDS = {};
	}
	if (weightsDS)
	{
		graphicsSystem->destroy(weightsDS); weightsDS = {};
	}
	if (edgesDS)
	{
		graphicsSystem->destroy(edgesDS); edgesDS = {};
	}

	if (blendPipeline)
	{
		graphicsSystem->destroy(blendPipeline);
		blendPipeline = createBlendPipeline(blendFramebuffer);
	}
	if (weightsPipeline)
	{
		graphicsSystem->destroy(weightsPipeline);
		weightsPipeline = createWeightsPipeline(weightsFramebuffer, quality, cornerRounding);
	}
	if (edgesPipeline)
	{
		graphicsSystem->destroy(edgesPipeline);
		edgesPipeline = createEdgesPipeline(edgesFramebuffer, quality);
	}

	this->quality = quality;
	this->cornerRounding = cornerRounding;
}

ID<Image> SmaaRenderSystem::getEdgesBuffer()
{
	if (!edgesBuffer)
		edgesBuffer = createEdgesBuffer(GraphicsSystem::Instance::get());
	return edgesBuffer;
}

ID<Framebuffer> SmaaRenderSystem::getEdgesFramebuffer()
{
	if (!edgesFramebuffer)
		edgesFramebuffer = createEdgesFramebuffer(GraphicsSystem::Instance::get(), getEdgesBuffer());
	return edgesFramebuffer;
}
ID<Framebuffer> SmaaRenderSystem::getWeightsFramebuffer()
{
	if (!weightsFramebuffer)
		weightsFramebuffer = createWeightsFramebuffer(GraphicsSystem::Instance::get());
	return weightsFramebuffer;
}
ID<Framebuffer> SmaaRenderSystem::getBlendFramebuffer()
{
	if (!blendFramebuffer)
		blendFramebuffer = createBlendFramebuffer(GraphicsSystem::Instance::get());
	return blendFramebuffer;
}

ID<GraphicsPipeline> SmaaRenderSystem::getEdgesPipeline()
{
	if (!edgesPipeline)
		edgesPipeline = createEdgesPipeline(getEdgesFramebuffer(), quality);
	return edgesPipeline;
}
ID<GraphicsPipeline> SmaaRenderSystem::getWeightsPipeline()
{
	if (!weightsPipeline)
		weightsPipeline = createWeightsPipeline(getWeightsFramebuffer(), quality, cornerRounding);
	return weightsPipeline;
}
ID<GraphicsPipeline> SmaaRenderSystem::getBlendPipeline()
{
	if (!blendPipeline)
		blendPipeline = createBlendPipeline(getBlendFramebuffer());
	return blendPipeline;
}