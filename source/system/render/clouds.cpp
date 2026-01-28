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

#include "garden/system/render/clouds.hpp"
#include "garden/system/render/atmosphere.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/resource.hpp"
#include "garden/profiler.hpp"
#include "math/metric.hpp"

using namespace garden;

static ID<Image> createCloudsView(GraphicsSystem* graphicsSystem)
{
	auto size = max(graphicsSystem->getScaledFrameSize() / 2, uint2::one);
	auto cloudsView = graphicsSystem->createImage(Image::Format::SfloatR16G16B16A16, Image::Usage::Sampled | 
		Image::Usage::ColorAttachment, { { nullptr } }, size, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(cloudsView, "image.clouds.view");
	return cloudsView;
}
static ID<Image> createCloudsCube(GraphicsSystem* graphicsSystem, uint32 skyboxSize)
{
	auto size = max(skyboxSize / 2, 1u);
	auto cloudsView = graphicsSystem->createImage(Image::Format::SfloatR16G16B16A16, Image::Usage::Sampled | 
		Image::Usage::ColorAttachment, { { nullptr } }, uint2(size), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(cloudsView, "image.clouds.cube");
	return cloudsView;
}

static ID<Framebuffer> createFramebuffer(GraphicsSystem* graphicsSystem, ID<Image> cloudsBuffer)
{
	auto cloudsBufferView = graphicsSystem->get(cloudsBuffer);
	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(cloudsBufferView->getDefaultView(), CloudsRenderSystem::framebufferFlags) };

	auto framebuffer = graphicsSystem->createFramebuffer(
		(uint2)cloudsBufferView->getSize(), std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.clouds");
	return framebuffer;
}

static Ref<Image> createDataFields()
{
	return ResourceSystem::Instance::get()->loadImage("clouds/data-fields", Image::Format::UnormR8G8B8A8,
		Image::Usage::Sampled | Image::Usage::TransferSrc | Image::Usage::TransferDst | 
		Image::Usage::TransferQ, 0, Image::Strategy::Size, ImageLoadFlags::LoadShared, 9.0f);
}
static Ref<Image> createVerticalProfile()
{
	return ResourceSystem::Instance::get()->loadImage("clouds/vertical-profile", 
		Image::Format::UnormR8G8, Image::Usage::Sampled | Image::Usage::TransferDst | 
		Image::Usage::TransferQ, 1, Image::Strategy::Size, ImageLoadFlags::LoadShared, 9.0f);
}
static Ref<Image> createNoiseShape()
{
	return ResourceSystem::Instance::get()->loadImage("clouds/noise-shape", Image::Format::UnormR8G8B8A8,
		Image::Usage::Sampled | Image::Usage::TransferSrc | Image::Usage::TransferDst | Image::Usage::TransferQ, 
		0, Image::Strategy::Size, ImageLoadFlags::LoadShared | ImageLoadFlags::Load3D, 9.0f);
}

static ID<GraphicsPipeline> createCloudsPipeline(ID<Framebuffer> framebuffer, GraphicsQuality quality)
{
	float stepAdjDist;
	switch (quality)
	{
		case GraphicsQuality::PotatoPC: stepAdjDist = 2.048f; break;
		case GraphicsQuality::Low: stepAdjDist = 4.096f; break;
		case GraphicsQuality::Medium: stepAdjDist = 8.192f; break;
		case GraphicsQuality::High: stepAdjDist = 16.384f; break;
		case GraphicsQuality::Ultra: stepAdjDist = 32.768f; break;
		default: abort();
	}

	Pipeline::SpecConstValues specConstValues =
	{
		{ "STEP_ADJ_DIST", Pipeline::SpecConstValue(1.0f / stepAdjDist) }
	};

	ResourceSystem::GraphicsOptions options;
	options.specConstValues = &specConstValues;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("clouds/compute", framebuffer, options);
}
static ID<GraphicsPipeline> createBlendPipeline()
{
	auto hdrFramebuffer = DeferredRenderSystem::Instance::get()->getHdrFramebuffer();
	ResourceSystem::GraphicsOptions options;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("process/alpha-blend", hdrFramebuffer, options);
}

static DescriptorSet::Uniforms getCloudsUniforms(GraphicsSystem* graphicsSystem, 
	ID<Image> dataFields, ID<Image> verticalProfile, ID<Image> noiseShape)
{
	auto dataFieldsView = graphicsSystem->get(dataFields)->getDefaultView();
	auto verticalProfileView = graphicsSystem->get(verticalProfile)->getDefaultView();
	auto noiseShapeView = graphicsSystem->get(noiseShape)->getDefaultView();
	auto inFlightCount = graphicsSystem->getInFlightCount();

	DescriptorSet::Uniforms uniforms =
	{ 
		{ "dataFields", DescriptorSet::Uniform(dataFieldsView, 1, inFlightCount) },
		{ "verticalProfile", DescriptorSet::Uniform(verticalProfileView, 1, inFlightCount) },
		{ "noiseShape", DescriptorSet::Uniform(noiseShapeView, 1, inFlightCount) },
		{ "cc", DescriptorSet::Uniform(graphicsSystem->getCommonConstantsBuffers()) }
	};
	return uniforms;
}
static DescriptorSet::Uniforms getBlendUniforms(GraphicsSystem* graphicsSystem, 
	ID<Image> cloudsBuffer)
{
	auto cloudsBufferView = graphicsSystem->get(cloudsBuffer)->getDefaultView();
	return { { "srcBuffer", DescriptorSet::Uniform(cloudsBufferView) } };
}

//**********************************************************************************************************************
CloudsRenderSystem::CloudsRenderSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", CloudsRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", CloudsRenderSystem::deinit);
}
CloudsRenderSystem::~CloudsRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", CloudsRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", CloudsRenderSystem::deinit);
	}

	unsetSingleton();
}

void CloudsRenderSystem::init()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("PreDeferredRender", CloudsRenderSystem::preDeferredRender);
	ECSM_SUBSCRIBE_TO_EVENT("PreHdrRender", CloudsRenderSystem::preHdrRender);
	ECSM_SUBSCRIBE_TO_EVENT("HdrRender", CloudsRenderSystem::hdrRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", CloudsRenderSystem::gBufferRecreate);
	ECSM_SUBSCRIBE_TO_EVENT("QualityChange", CloudsRenderSystem::qualityChange);
}
void CloudsRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(cubeBlendDS);
		graphicsSystem->destroy(viewBlendDS);
		graphicsSystem->destroy(cloudsDS);
		graphicsSystem->destroy(cubeFramebuffer);
		graphicsSystem->destroy(viewFramebuffer);
		graphicsSystem->destroy(cloudsCube);
		graphicsSystem->destroy(cloudsView);
		graphicsSystem->destroy(noiseShape);
		graphicsSystem->destroy(verticalProfile);
		graphicsSystem->destroy(dataFields);
		graphicsSystem->destroy(blendPipeline);
		graphicsSystem->destroy(cloudsPipeline);

		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreDeferredRender", CloudsRenderSystem::preDeferredRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreHdrRender", CloudsRenderSystem::preHdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("HdrRender", CloudsRenderSystem::hdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", CloudsRenderSystem::gBufferRecreate);
		ECSM_UNSUBSCRIBE_FROM_EVENT("QualityChange", CloudsRenderSystem::qualityChange);
	}
}

//**********************************************************************************************************************
void CloudsRenderSystem::preDeferredRender()
{
	SET_CPU_ZONE_SCOPED("Clouds Pre Deferred Render");

	if (!isEnabled)
		return;
	
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!isInitialized)
	{
		if (!dataFields)
			dataFields = createDataFields();
		if (!verticalProfile)
			verticalProfile = createVerticalProfile();
		if (!noiseShape)
			noiseShape = createNoiseShape();
		if (!cloudsView)
			cloudsView = createCloudsView(graphicsSystem);
		if (!cloudsCube)
			cloudsCube = createCloudsView(graphicsSystem);
		if (!viewFramebuffer)
			viewFramebuffer = createFramebuffer(graphicsSystem, cloudsView);
		if (!cubeFramebuffer)
			cubeFramebuffer = createFramebuffer(graphicsSystem, cloudsCube);
		if (!cloudsPipeline)
			cloudsPipeline = createCloudsPipeline(viewFramebuffer, quality);
		if (!blendPipeline)
			blendPipeline = createBlendPipeline();
		isInitialized = true;
	}

	auto pipelineView = graphicsSystem->get(cloudsPipeline);
	auto dataFieldsView = graphicsSystem->get(dataFields);
	auto verticalProfileView = graphicsSystem->get(verticalProfile);
	auto noiseShapeView = graphicsSystem->get(noiseShape);

	if (!pipelineView->isReady() || !dataFieldsView->isReady() || 
		!verticalProfileView->isReady() || !noiseShapeView->isReady())
	{
		return;
	}

	if (!cloudsDS)
	{
		auto uniforms = getCloudsUniforms(graphicsSystem, ID<Image>(dataFields),
			ID<Image>(verticalProfile), ID<Image>(noiseShape));
		cloudsDS = graphicsSystem->createDescriptorSet(cloudsPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(cloudsDS, "descriptorSet.clouds");
	}

	auto framebufferView = graphicsSystem->get(cubeFramebuffer);
	pipelineView->updateFramebuffer(cubeFramebuffer);

	/* TODO:
	PushConstants pc;
	pc.invFrameSize = float2::one / framebufferView->getSize();

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		SET_GPU_DEBUG_LABEL("Cube Clouds");
		{
			RenderPass renderPass(cubeFramebuffer, float4::zero);
			pipelineView->bind();
			pipelineView->setViewportScissor();
			pipelineView->bindDescriptorSet(descriptorSet);
			pipelineView->pushConstants(&pc);
			pipelineView->drawFullscreen();
		}
	}
	graphicsSystem->stopRecording();
	*/
}

//**********************************************************************************************************************
void CloudsRenderSystem::preHdrRender()
{
	SET_CPU_ZONE_SCOPED("Clouds HDR Render");

	if (!isEnabled || !cloudsDS)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto atmosphereSystem = AtmosphereRenderSystem::Instance::get();
	auto groundRadius = atmosphereSystem->groundRadius;
	auto& cc = graphicsSystem->getCommonConstants();
	auto pipelineView = graphicsSystem->get(cloudsPipeline);
	auto framebufferView = graphicsSystem->get(viewFramebuffer);
	pipelineView->updateFramebuffer(viewFramebuffer);

	PushConstants pc;
	pc.cameraPos = mToKm(cc.cameraPos);
	pc.bottomRadius = groundRadius + bottomRadius;
	pc.topRadius = groundRadius + topRadius;
	pc.minDistance = minDistance;
	pc.maxDistance = maxDistance;
	pc.coverage = 1.0f - pow(saturate(coverage), 2.0f);
	pc.temperature = pow(saturate(temperature), 2.0f);
	pc.cameraPos.y += groundRadius;

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		SET_GPU_DEBUG_LABEL("View Clouds");
		{
			RenderPass renderPass(viewFramebuffer, float4::zero);
			pipelineView->bind();
			pipelineView->setViewportScissor();
			pipelineView->bindDescriptorSet(cloudsDS);
			pipelineView->pushConstants(&pc);
			pipelineView->drawFullscreen();
		}
	}
	graphicsSystem->stopRecording();
}

//**********************************************************************************************************************
void CloudsRenderSystem::hdrRender()
{
	SET_CPU_ZONE_SCOPED("Clouds HDR Render");

	if (!isEnabled)
		return;
	
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(blendPipeline);
	if (!pipelineView->isReady())
		return;

	if (!viewBlendDS)
	{
		auto uniforms = getBlendUniforms(graphicsSystem, cloudsView);
		viewBlendDS = graphicsSystem->createDescriptorSet(blendPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(viewBlendDS, "descriptorSet.clouds.viewBlend");
	}

	SET_GPU_DEBUG_LABEL("Blend Clouds");
	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->bindDescriptorSet(viewBlendDS);
	pipelineView->drawFullscreen();
}

void CloudsRenderSystem::gBufferRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (cloudsView)
	{
		graphicsSystem->destroy(cloudsView);
		cloudsView = createCloudsView(graphicsSystem);
	}
	if (viewFramebuffer)
	{
		auto cloudsBufferView = graphicsSystem->get(cloudsView);
		auto framebufferView = graphicsSystem->get(viewFramebuffer);
		Framebuffer::OutputAttachment colorAttachment(
			cloudsBufferView->getDefaultView(), CloudsRenderSystem::framebufferFlags);
		framebufferView->update((uint2)cloudsBufferView->getSize(), &colorAttachment, 1);
	}
	if (viewBlendDS)
	{
		graphicsSystem->destroy(viewBlendDS);
		viewBlendDS = {};
	}
}

void CloudsRenderSystem::qualityChange()
{
	setQuality(GraphicsSystem::Instance::get()->quality);
}
void CloudsRenderSystem::setQuality(GraphicsQuality quality)
{
	if (this->quality == quality)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	graphicsSystem->destroy(cloudsDS); cloudsDS = {};

	if (cloudsPipeline)
	{
		graphicsSystem->destroy(cloudsPipeline);
		cloudsPipeline = createCloudsPipeline(viewFramebuffer, quality);
	}

	this->quality = quality;
}

//**********************************************************************************************************************
Ref<Image> CloudsRenderSystem::getDataFields()
{
	if (!dataFields)
		dataFields = createDataFields();
	return dataFields;
}
Ref<Image> CloudsRenderSystem::getVerticalProfile()
{
	if (!verticalProfile)
		verticalProfile = createVerticalProfile();
	return verticalProfile;
}
Ref<Image> CloudsRenderSystem::getNoiseShape()
{
	if (!noiseShape)
		noiseShape = createNoiseShape();
	return noiseShape;
}
ID<Image> CloudsRenderSystem::getCloudsView()
{
	if (!cloudsView)
		cloudsView = createCloudsView(GraphicsSystem::Instance::get());
	return cloudsView;
}
ID<Framebuffer> CloudsRenderSystem::getViewFramebuffer()
{
	if (!viewFramebuffer)
		viewFramebuffer = createFramebuffer(GraphicsSystem::Instance::get(), getCloudsView());
	return viewFramebuffer;
}
ID<GraphicsPipeline> CloudsRenderSystem::getCloudsPipeline()
{
	if (!cloudsPipeline)
		cloudsPipeline = createCloudsPipeline(getViewFramebuffer(), quality);
	return cloudsPipeline;
}
ID<GraphicsPipeline> CloudsRenderSystem::getBlendPipeline()
{
	if (!blendPipeline)
		blendPipeline = createBlendPipeline();
	return blendPipeline;
}