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
#include "garden/system/render/pbr-lighting.hpp"
#include "garden/system/render/atmosphere.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/render/hiz.hpp"
#include "garden/system/resource.hpp"
#include "garden/profiler.hpp"
#include "math/metric.hpp"

using namespace garden;

static ID<Image> createCloudsView(GraphicsSystem* graphicsSystem)
{
	auto size = max(graphicsSystem->getScaledFrameSize() / 2, uint2::one);
	auto cloudsView = graphicsSystem->createImage(CloudsRenderSystem::cloudsColorFormat, 
		Image::Usage::Sampled | Image::Usage::ColorAttachment, { { nullptr } }, size, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(cloudsView, "image.clouds.view");
	return cloudsView;
}
static ID<Image> createCloudsViewDepth(GraphicsSystem* graphicsSystem)
{
	auto size = max(graphicsSystem->getScaledFrameSize() / 2, uint2::one);
	auto cloudsViewDepth = graphicsSystem->createImage(CloudsRenderSystem::cloudsDepthFormat, 
		Image::Usage::Sampled | Image::Usage::ColorAttachment, { { nullptr } }, size, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(cloudsViewDepth, "image.clouds.viewDepth");
	return cloudsViewDepth;
}
static ID<Image> createCloudsCube(GraphicsSystem* graphicsSystem, uint32 skyboxSize)
{
	auto size = max(skyboxSize / 2, 1u);
	auto cloudsView = graphicsSystem->createImage(CloudsRenderSystem::cloudsColorFormat, Image::Usage::Sampled | 
		Image::Usage::ColorAttachment, { { nullptr } }, uint2(size), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(cloudsView, "image.clouds.cube");
	return cloudsView;
}

static ID<Framebuffer> createViewFramebuffer(GraphicsSystem* graphicsSystem, 
	ID<Image> cloudsView, ID<Image> cloudsViewDepth)
{
	auto cloudsViewView = graphicsSystem->get(cloudsView);
	auto cloudsViewDepthView = graphicsSystem->get(cloudsViewDepth);

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{
		Framebuffer::OutputAttachment(cloudsViewView->getDefaultView(), CloudsRenderSystem::framebufferFlags),
		Framebuffer::OutputAttachment(cloudsViewDepthView->getDefaultView(), CloudsRenderSystem::framebufferFlags),
	};

	auto framebuffer = graphicsSystem->createFramebuffer(
		(uint2)cloudsViewView->getSize(), std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.cloudsView");
	return framebuffer;
}

//**********************************************************************************************************************
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
static Ref<Image> createCirrusShape()
{
	return ResourceSystem::Instance::get()->loadImage("clouds/cirrus-shape", Image::Format::UnormR8G8B8A8,
		Image::Usage::Sampled | Image::Usage::TransferSrc | Image::Usage::TransferDst | 
		Image::Usage::TransferQ, 0, Image::Strategy::Size, ImageLoadFlags::LoadShared, 9.0f);
}

static ID<GraphicsPipeline> createComputePipeline(ID<Framebuffer> framebuffer, GraphicsQuality quality)
{
	float sliceCount, kmPerSlice; float stepAdjDist;
	auto atmosphereQuality = AtmosphereRenderSystem::Instance::get()->getQuality();
	AtmosphereRenderSystem::getSliceQuality(atmosphereQuality, sliceCount, kmPerSlice);

	switch (quality)
	{
		case GraphicsQuality::PotatoPC: stepAdjDist = 3.072f; break;
		case GraphicsQuality::Low: stepAdjDist = 4.096f; break;
		case GraphicsQuality::Medium: stepAdjDist = 8.192f; break;
		case GraphicsQuality::High: stepAdjDist = 16.384f; break;
		case GraphicsQuality::Ultra: stepAdjDist = 32.768f; break;
		default: abort();
	}

	Pipeline::SpecConstValues specConstValues =
	{
		{ "STEP_ADJ_DIST", Pipeline::SpecConstValue(1.0f / stepAdjDist) },
		{ "SLICE_COUNT", Pipeline::SpecConstValue(sliceCount) },
		{ "KM_PER_SLICE", Pipeline::SpecConstValue(kmPerSlice) }
	};

	ResourceSystem::GraphicsOptions options;
	options.specConstValues = &specConstValues;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("clouds/compute", framebuffer, options);
}
static ID<GraphicsPipeline> createViewBlendPipeline()
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	ResourceSystem::GraphicsOptions options;
	options.useAsyncRecording = deferredSystem->getOptions().useAsyncRecording;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline(
		"clouds/view-blend", deferredSystem->getHdrFramebuffer(), options);
}
static ID<GraphicsPipeline> createShadowPipeline()
{
	auto pbrLightingSystem = PbrLightingSystem::Instance::get();
	ResourceSystem::GraphicsOptions options;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline(
		"clouds/shadow", pbrLightingSystem->getShadowBaseFB(), options);
}

//**********************************************************************************************************************
static DescriptorSet::Uniforms getCloudsUniforms(GraphicsSystem* graphicsSystem, 
	ID<Image> dataFields, ID<Image> verticalProfile, ID<Image> noiseShape, ID<Image> cirrusShape)
{
	auto hizBufferView = HizRenderSystem::Instance::get()->getImageViews().at(1);
	auto cameraVolume = AtmosphereRenderSystem::Instance::get()->getCameraVolume();
	auto cameraVolumeView = graphicsSystem->get(cameraVolume)->getDefaultView();
	auto dataFieldsView = graphicsSystem->get(dataFields)->getDefaultView();
	auto verticalProfileView = graphicsSystem->get(verticalProfile)->getDefaultView();
	auto noiseShapeView = graphicsSystem->get(noiseShape)->getDefaultView();
	auto cirrusShapeView = graphicsSystem->get(cirrusShape)->getDefaultView();
	auto inFlightCount = graphicsSystem->getInFlightCount();

	DescriptorSet::Uniforms uniforms =
	{ 
		{ "hizBuffer", DescriptorSet::Uniform(hizBufferView, 1, inFlightCount) },
		{ "cameraVolume", DescriptorSet::Uniform(cameraVolumeView, 1, inFlightCount) },
		{ "dataFields", DescriptorSet::Uniform(dataFieldsView, 1, inFlightCount) },
		{ "verticalProfile", DescriptorSet::Uniform(verticalProfileView, 1, inFlightCount) },
		{ "noiseShape", DescriptorSet::Uniform(noiseShapeView, 1, inFlightCount) },
		{ "cirrusShape", DescriptorSet::Uniform(cirrusShapeView, 1, inFlightCount) },
		{ "cc", DescriptorSet::Uniform(graphicsSystem->getCommonConstantsBuffers()) }
	};
	return uniforms;
}
static DescriptorSet::Uniforms getViewBlendUniforms(GraphicsSystem* graphicsSystem, 
	ID<Image> cloudsView, ID<Image> cloudsViewDepth)
{
	auto depthBufferView = DeferredRenderSystem::Instance::get()->getDepthImageView();
	auto cloudsViewView = graphicsSystem->get(cloudsView)->getDefaultView();
	auto cloudsViewDepthView = graphicsSystem->get(cloudsViewDepth)->getDefaultView();

	DescriptorSet::Uniforms uniforms =
	{
		{ "depthBuffer", DescriptorSet::Uniform(depthBufferView) },
		{ "cloudsBuffer", DescriptorSet::Uniform(cloudsViewView) },
		{ "cloudsDepth", DescriptorSet::Uniform(cloudsViewDepthView) }
	};
	return uniforms;
}
static DescriptorSet::Uniforms getShadowUniforms(GraphicsSystem* graphicsSystem, ID<Image> dataFields)
{
	auto depthBufferView = DeferredRenderSystem::Instance::get()->getDepthImageView();
	auto dataFieldsView = graphicsSystem->get(dataFields)->getDefaultView();

	DescriptorSet::Uniforms uniforms =
	{ 
		{ "depthBuffer", DescriptorSet::Uniform(depthBufferView) },
		{ "dataFields", DescriptorSet::Uniform(dataFieldsView) }
	};
	return uniforms;
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
	ECSM_SUBSCRIBE_TO_EVENT("PreShadowRender", CloudsRenderSystem::preShadowRender);
	ECSM_SUBSCRIBE_TO_EVENT("ShadowRender", CloudsRenderSystem::shadowRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", CloudsRenderSystem::gBufferRecreate);
	ECSM_SUBSCRIBE_TO_EVENT("QualityChange", CloudsRenderSystem::qualityChange);
}
void CloudsRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(shadowDS);
		graphicsSystem->destroy(cubeBlendDS);
		graphicsSystem->destroy(viewBlendDS);
		graphicsSystem->destroy(computeDS);
		graphicsSystem->destroy(cubeFramebuffer);
		graphicsSystem->destroy(viewFramebuffer);
		graphicsSystem->destroy(cloudsCube);
		graphicsSystem->destroy(cloudsViewDepth);
		graphicsSystem->destroy(cloudsView);
		graphicsSystem->destroy(cirrusShape);
		graphicsSystem->destroy(noiseShape);
		graphicsSystem->destroy(verticalProfile);
		graphicsSystem->destroy(dataFields);
		graphicsSystem->destroy(shadowPipeline);
		graphicsSystem->destroy(viewBlendPipeline);
		graphicsSystem->destroy(computePipeline);

		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreDeferredRender", CloudsRenderSystem::preDeferredRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreHdrRender", CloudsRenderSystem::preHdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("HdrRender", CloudsRenderSystem::hdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreShadowRender", CloudsRenderSystem::preShadowRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("ShadowRender", CloudsRenderSystem::shadowRender);
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
		if (!cirrusShape)
			cirrusShape = createCirrusShape();
		if (!cloudsView)
			cloudsView = createCloudsView(graphicsSystem);
		if (!cloudsViewDepth)
			cloudsViewDepth = createCloudsViewDepth(graphicsSystem);
		// if (!cloudsCube)
		//	cloudsCube = createCloudsView(graphicsSystem);
		if (!viewFramebuffer)
			viewFramebuffer = createViewFramebuffer(graphicsSystem, cloudsView, cloudsViewDepth);
		// if (!cubeFramebuffer)
		// 	cubeFramebuffer = createFramebuffer(graphicsSystem, cloudsCube);
		if (!computePipeline)
			computePipeline = createComputePipeline(viewFramebuffer, quality);
		if (!viewBlendPipeline)
			viewBlendPipeline = createViewBlendPipeline();
		isInitialized = true;
	}

	auto pipelineView = graphicsSystem->get(computePipeline);
	auto dataFieldsView = graphicsSystem->get(dataFields);
	auto verticalProfileView = graphicsSystem->get(verticalProfile);
	auto noiseShapeView = graphicsSystem->get(noiseShape);

	if (!pipelineView->isReady() || !dataFieldsView->isReady() || 
		!verticalProfileView->isReady() || !noiseShapeView->isReady())
	{
		return;
	}

	if (!computeDS)
	{
		auto uniforms = getCloudsUniforms(graphicsSystem, ID<Image>(dataFields),
			ID<Image>(verticalProfile), ID<Image>(noiseShape), ID<Image>(cirrusShape));
		computeDS = graphicsSystem->createDescriptorSet(computePipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(computeDS, "descriptorSet.clouds.compute");
	}

	/* TODO:
	auto framebufferView = graphicsSystem->get(cubeFramebuffer);
	pipelineView->updateFramebuffer(cubeFramebuffer);

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

static float3 calcCameraPos(const CommonConstants& cc, float groundRadius)
{
	auto cameraPos = mToKm(cc.cameraPos); cameraPos.y += groundRadius;
	return cameraPos;
}
static float calcCurrentTime(const CommonConstants& cc, float currentTime) noexcept
{
	return currentTime != 0.0f ? currentTime : cc.currentTime;
}

static float calcCoverage(float coverage) noexcept { return 1.0f - pow(saturate(coverage), 2.0f); }
static float calcTemperature(float temperature) noexcept { return pow(saturate(temperature), 2.0f); }

//**********************************************************************************************************************
void CloudsRenderSystem::preHdrRender()
{
	SET_CPU_ZONE_SCOPED("Clouds HDR Render");

	if (!isEnabled || !computeDS)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto atmosphereSystem = AtmosphereRenderSystem::Instance::get();
	auto inFlightIndex = graphicsSystem->getInFlightIndex();
	auto groundRadius = atmosphereSystem->groundRadius;
	auto& cc = graphicsSystem->getCommonConstants();
	auto pipelineView = graphicsSystem->get(computePipeline);
	auto framebufferView = graphicsSystem->get(viewFramebuffer);
	pipelineView->updateFramebuffer(viewFramebuffer);

	ComputePC pc;
	pc.cameraPos = calcCameraPos(cc, groundRadius);
	pc.bottomRadius = groundRadius + bottomRadius;
	pc.topRadius = groundRadius + topRadius;
	pc.minDistance = minDistance;
	pc.maxDistance = maxDistance;
	pc.currentTime = calcCurrentTime(cc, currentTime);
	pc.cumulusCoverage = calcCoverage(cumulusCoverage);
	pc.cirrusCoverage = 1.0f - saturate(cirrusCoverage);
	pc.temperature = calcTemperature(temperature);

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		SET_GPU_DEBUG_LABEL("View Clouds");
		{
			constexpr const float4 clearColors[2] = { float4(0.0f), float4(0.0f) };
			RenderPass renderPass(viewFramebuffer, clearColors, 2);
			pipelineView->bind();
			pipelineView->setViewportScissor();
			pipelineView->bindDescriptorSet(computeDS, inFlightIndex);
			pipelineView->pushConstants(&pc);
			pipelineView->drawFullscreen();
		}
	}
	graphicsSystem->stopRecording();
}

//**********************************************************************************************************************
void CloudsRenderSystem::hdrRender()
{
	SET_CPU_ZONE_SCOPED("Clouds Depth HDR Render");

	if (!isEnabled)
		return;
	
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(viewBlendPipeline);
	if (!pipelineView->isReady())
		return;

	if (!viewBlendDS)
	{
		auto uniforms = getViewBlendUniforms(graphicsSystem, cloudsView, cloudsViewDepth);
		viewBlendDS = graphicsSystem->createDescriptorSet(viewBlendPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(viewBlendDS, "descriptorSet.clouds.viewBlend");
	}

	SET_GPU_DEBUG_LABEL("Blend Clouds");
	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->bindDescriptorSet(viewBlendDS);
	pipelineView->drawFullscreen();
}

void CloudsRenderSystem::preShadowRender()
{
	hasShadows = false;
	if (!isEnabled || !renderShadows)
		return;

	if (!shadowPipeline)
		shadowPipeline = createShadowPipeline();

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(shadowPipeline);
	auto dataFieldsView = graphicsSystem->get(dataFields);
	auto verticalProfileView = graphicsSystem->get(verticalProfile);
	if (!pipelineView->isReady() || !dataFieldsView->isReady() || !verticalProfileView->isReady())
		return;

	if (!shadowDS)
	{
		auto uniforms = getShadowUniforms(graphicsSystem, ID<Image>(dataFields));
		shadowDS = graphicsSystem->createDescriptorSet(shadowPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(shadowDS, "descriptorSet.clouds.shadow");
	}

	PbrLightingSystem::Instance::get()->markFbShadow();
	hasShadows = true;
}
void CloudsRenderSystem::shadowRender()
{
	SET_CPU_ZONE_SCOPED("Cloud Shadows");

	if (!hasShadows)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto atmosphereSystem = AtmosphereRenderSystem::Instance::get();
	auto groundRadius = atmosphereSystem->groundRadius;
	auto& cc = graphicsSystem->getCommonConstants();
	auto pipelineView = graphicsSystem->get(shadowPipeline);

	ShadowsPC pc;
	pc.invViewProj = (float4x4)cc.invViewProj;
	pc.cameraPos = calcCameraPos(cc, groundRadius);
	pc.bottomRadius = groundRadius + bottomRadius;
	pc.starDir = -cc.lightDir;
	pc.currentTime = calcCurrentTime(cc, currentTime);
	pc.windDir = cc.windDir;
	pc.cumulusCoverage = calcCoverage(cumulusCoverage);
	pc.temperature = calcTemperature(temperature);

	SET_GPU_DEBUG_LABEL("Clouds Shadow");
	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->bindDescriptorSet(shadowDS);
	pipelineView->pushConstants(&pc);
	pipelineView->drawFullscreen();
}

//**********************************************************************************************************************
void CloudsRenderSystem::gBufferRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (cloudsView)
	{
		graphicsSystem->destroy(cloudsView);
		cloudsView = createCloudsView(graphicsSystem);
	}
	if (cloudsViewDepth)
	{
		graphicsSystem->destroy(cloudsViewDepth);
		cloudsViewDepth = createCloudsViewDepth(graphicsSystem);
	}

	if (viewFramebuffer)
	{
		auto framebufferView = graphicsSystem->get(viewFramebuffer);
		auto cloudsViewView = graphicsSystem->get(cloudsView);
		auto cloudsViewDepthView = graphicsSystem->get(cloudsViewDepth);
		Framebuffer::OutputAttachment colorAttachment[2] =
		{
			{ cloudsViewView->getDefaultView(), CloudsRenderSystem::framebufferFlags },
			{ cloudsViewDepthView->getDefaultView(), CloudsRenderSystem::framebufferFlags },
		};
		framebufferView->update((uint2)cloudsViewView->getSize(), colorAttachment, 2);
	}

	if (computeDS)
	{
		graphicsSystem->destroy(computeDS); computeDS = {};
	}
	if (viewBlendDS)
	{
		graphicsSystem->destroy(viewBlendDS); viewBlendDS = {};
	}
	if (cubeBlendDS)
	{
		graphicsSystem->destroy(cubeBlendDS); cubeBlendDS = {};
	}
	if (shadowDS)
	{
		graphicsSystem->destroy(shadowDS); shadowDS = {};
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
	graphicsSystem->destroy(computeDS); computeDS = {};

	if (computePipeline)
	{
		graphicsSystem->destroy(computePipeline);
		computePipeline = createComputePipeline(viewFramebuffer, quality);
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
ID<Image> CloudsRenderSystem::getCloudsViewDepth()
{
	if (!cloudsViewDepth)
		cloudsViewDepth = createCloudsViewDepth(GraphicsSystem::Instance::get());
	return cloudsViewDepth;
}
ID<Framebuffer> CloudsRenderSystem::getViewFramebuffer()
{
	if (!viewFramebuffer)
	{
		viewFramebuffer = createViewFramebuffer(GraphicsSystem::Instance::get(), 
			getCloudsView(), getCloudsViewDepth());
	}
	return viewFramebuffer;
}
ID<GraphicsPipeline> CloudsRenderSystem::getComputePipeline()
{
	if (!computePipeline)
		computePipeline = createComputePipeline(getViewFramebuffer(), quality);
	return computePipeline;
}
ID<GraphicsPipeline> CloudsRenderSystem::getViewBlendPipeline()
{
	if (!viewBlendPipeline)
		viewBlendPipeline = createViewBlendPipeline();
	return viewBlendPipeline;
}