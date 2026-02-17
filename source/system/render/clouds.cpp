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

static const uint32 bayerIndices4x4[16] = 
{
	 0,  8,  2, 10,
	12,  4, 14,  6,
	 3, 11,  1,  9,
	15,  7, 13,  5
};

static ID<Image> createCloudsCamView(GraphicsSystem* graphicsSystem)
{
	auto size = max(graphicsSystem->getScaledFrameSize() / 2, uint2::one);
	auto cloudsCamView = graphicsSystem->createImage(CloudsRenderSystem::cloudsColorFormat, Image::Usage::Sampled | 
		Image::Usage::ColorAttachment, { { nullptr, nullptr } }, size, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(cloudsCamView, "image.clouds.camView");
	return cloudsCamView;
}
static ID<Image> createCloudsSkybox(GraphicsSystem* graphicsSystem, ID<Image> skybox)
{
	auto cubemapSize = max(graphicsSystem->get(skybox)->getSize().getX() / 2, 1u);
	auto cloudsSkybox = graphicsSystem->createImage(CloudsRenderSystem::cloudsColorFormat, Image::Usage::Sampled | 
		Image::Usage::ColorAttachment, { { nullptr } }, uint2(cubemapSize), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(cloudsSkybox, "image.clouds.skybox");
	return cloudsSkybox;
}
static ID<Image> createCloudsCamViewDepth(GraphicsSystem* graphicsSystem)
{
	auto size = max(graphicsSystem->getScaledFrameSize() / 2, uint2::one);
	auto cloudsCamViewDepth = graphicsSystem->createImage(CloudsRenderSystem::cloudsDepthFormat, 
		Image::Usage::Sampled | Image::Usage::ColorAttachment, { { nullptr } }, size, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(cloudsCamViewDepth, "image.clouds.camViewDepth");
	return cloudsCamViewDepth;
}

static ID<Framebuffer> createCamViewFramebuffer(GraphicsSystem* graphicsSystem, 
	ID<Image> cloudsCamView, ID<Image> cloudsCamViewDepth)
{
	auto camViewView = graphicsSystem->get(cloudsCamView);
	auto viewDepthView = graphicsSystem->get(cloudsCamViewDepth);

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{
		Framebuffer::OutputAttachment(camViewView->getView(), CloudsRenderSystem::framebufferFlags),
		Framebuffer::OutputAttachment(viewDepthView->getView(), CloudsRenderSystem::framebufferFlags),
	};

	auto framebuffer = graphicsSystem->createFramebuffer(
		(uint2)camViewView->getSize(), std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.clouds.camView");
	return framebuffer;
}
static void updateCamViewFramebuffer(GraphicsSystem* graphicsSystem, ID<Image> cloudsCamView, 
	ID<Image> cloudsCamViewDepth, View<Framebuffer> framebufferView)
{
	auto camView = graphicsSystem->get(cloudsCamView);
	auto camViewView = camView->getView(graphicsSystem->getInFlightIndex(), 0);
	auto viewDepthView = graphicsSystem->get(cloudsCamViewDepth)->getView();

	Framebuffer::OutputAttachment colorAttachments[2] = 
	{
		Framebuffer::OutputAttachment(camViewView, CloudsRenderSystem::framebufferFlags),
		Framebuffer::OutputAttachment(viewDepthView, CloudsRenderSystem::framebufferFlags),
	};
	framebufferView->update((uint2)camView->getSize(), colorAttachments, 2);
}

static ID<Framebuffer> createSkyboxFramebuffer(GraphicsSystem* graphicsSystem, ID<Image> cloudsSkybox)
{
	auto cloudsSkyboxView = graphicsSystem->get(cloudsSkybox);
	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(cloudsSkyboxView->getView(), CloudsRenderSystem::framebufferFlags) };
	auto framebuffer = graphicsSystem->createFramebuffer((uint2)cloudsSkyboxView->getSize(), std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.clouds.skybox");
	return framebuffer;
}

//**********************************************************************************************************************
static Ref<Image> createDataFields()
{
	return ResourceSystem::Instance::get()->loadImage("clouds/data-fields", Image::Format::UnormR8G8B8A8,
		Image::Usage::Sampled | Image::Usage::TransferSrc | Image::Usage::TransferDst | 
		Image::Usage::TransferQ, 0, Image::Strategy::Size, ImageLoadFlags::LoadShared, 9.0f);
}
static Ref<Image> createVertProfile()
{
	return ResourceSystem::Instance::get()->loadImage("clouds/vert-profile", 
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

static void getCloudsQuality(GraphicsQuality cloudsQuality, float& stepAdjDist, float& sliceCount, float& kmPerSlice)
{
	auto atmosphereQuality = AtmosphereRenderSystem::Instance::get()->getQuality();
	AtmosphereRenderSystem::getSliceQuality(atmosphereQuality, sliceCount, kmPerSlice);

	switch (cloudsQuality)
	{
		case GraphicsQuality::PotatoPC: stepAdjDist = 3.072f; break;
		case GraphicsQuality::Low: stepAdjDist = 4.096f; break;
		case GraphicsQuality::Medium: stepAdjDist = 8.192f; break;
		case GraphicsQuality::High: stepAdjDist = 16.384f; break;
		case GraphicsQuality::Ultra: stepAdjDist = 32.768f; break;
		default: abort();
	}
	stepAdjDist = 1.0f / stepAdjDist;
}
static ID<GraphicsPipeline> createCamViewPipeline(ID<Framebuffer> framebuffer, GraphicsQuality quality)
{
	float stepAdjDist, sliceCount, kmPerSlice;
	getCloudsQuality(quality, stepAdjDist, sliceCount, kmPerSlice);

	Pipeline::SpecConstValues specConstValues =
	{
		{ "STEP_ADJ_DIST", Pipeline::SpecConstValue(stepAdjDist) },
		{ "SLICE_COUNT", Pipeline::SpecConstValue(sliceCount) },
		{ "KM_PER_SLICE", Pipeline::SpecConstValue(kmPerSlice) }
	};

	ResourceSystem::GraphicsOptions options;
	options.specConstValues = &specConstValues;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("clouds/cam-view", framebuffer, options);
}
static ID<GraphicsPipeline> createSkyboxPipeline(ID<Framebuffer> framebuffer, GraphicsQuality quality)
{
	float stepAdjDist, sliceCount, kmPerSlice;
	getCloudsQuality(quality, stepAdjDist, sliceCount, kmPerSlice);

	Pipeline::SpecConstValues specConstValues =
	{
		{ "STEP_ADJ_DIST", Pipeline::SpecConstValue(stepAdjDist) },
		{ "SLICE_COUNT", Pipeline::SpecConstValue(sliceCount) },
		{ "KM_PER_SLICE", Pipeline::SpecConstValue(kmPerSlice) }
	};

	ResourceSystem::GraphicsOptions options;
	options.specConstValues = &specConstValues;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("clouds/skybox", framebuffer, options);
}

static ID<GraphicsPipeline> createViewBlendPipeline()
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	ResourceSystem::GraphicsOptions options;
	options.useAsyncRecording = deferredSystem->getOptions().useAsyncRecording;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline(
		"clouds/view-blend", deferredSystem->getHdrFramebuffer(), options);
}
static ID<GraphicsPipeline> createSkyBlendPipeline(ID<Framebuffer> framebuffer)
{
	ResourceSystem::GraphicsOptions options;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("process/alpha-blend", framebuffer, options);
}
static ID<GraphicsPipeline> createShadowPipeline()
{
	auto pbrLightingSystem = PbrLightingSystem::Instance::get();
	ResourceSystem::GraphicsOptions options;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline(
		"clouds/shadow", pbrLightingSystem->getShadBaseFB(), options);
}

//**********************************************************************************************************************
static DescriptorSet::Uniforms getCamViewUniforms(GraphicsSystem* graphicsSystem, ID<Image> dataFields, 
	ID<Image> vertProfile, ID<Image> noiseShape, ID<Image> cirrusShape, ID<Image> cloudsCamView)
{
	auto atmosphereSystem = AtmosphereRenderSystem::Instance::get();
	auto hizBufferView = HizRenderSystem::Instance::get()->getView(2);
	auto gFramebuffer = graphicsSystem->get(DeferredRenderSystem::Instance::get()->getGFramebuffer());
	auto gVelocityView = gFramebuffer->getColorAttachments()[DeferredRenderSystem::gBufferVelocity].imageView;
	auto transLutView = graphicsSystem->get(atmosphereSystem->getTransLUT())->getView();
	auto cameraVolumeView = graphicsSystem->get(atmosphereSystem->getCameraVolume())->getView();
	auto dataFieldsView = graphicsSystem->get(dataFields)->getView();
	auto vertProfileView = graphicsSystem->get(vertProfile)->getView();
	auto noiseShapeView = graphicsSystem->get(noiseShape)->getView();
	auto cirrusShapeView = graphicsSystem->get(cirrusShape)->getView();
	auto camViewView = graphicsSystem->get(cloudsCamView);
	auto inFlightCount = graphicsSystem->getInFlightCount();
	GARDEN_ASSERT(inFlightCount == 2); // Tuned for a 2 views.

	DescriptorSet::ImageViews camViews = { { camViewView->getView(1, 0) }, { camViewView->getView(0, 0) } };

	DescriptorSet::Uniforms uniforms =
	{ 
		{ "camView", DescriptorSet::Uniform(camViews) },
		{ "hizBuffer", DescriptorSet::Uniform(hizBufferView, 1, inFlightCount) },
		{ "gVelocity", DescriptorSet::Uniform(gVelocityView, 1, inFlightCount) },
		{ "transLUT", DescriptorSet::Uniform(transLutView, 1, inFlightCount) },
		{ "cameraVolume", DescriptorSet::Uniform(cameraVolumeView, 1, inFlightCount) },
		{ "dataFields", DescriptorSet::Uniform(dataFieldsView, 1, inFlightCount) },
		{ "vertProfile", DescriptorSet::Uniform(vertProfileView, 1, inFlightCount) },
		{ "noiseShape", DescriptorSet::Uniform(noiseShapeView, 1, inFlightCount) },
		{ "cirrusShape", DescriptorSet::Uniform(cirrusShapeView, 1, inFlightCount) },
		{ "cc", DescriptorSet::Uniform(graphicsSystem->getCommonConstantsBuffers()) }
	};
	return uniforms;
}
static DescriptorSet::Uniforms getSkyboxUniforms(GraphicsSystem* graphicsSystem, 
	ID<Image> dataFields, ID<Image> vertProfile, ID<Image> noiseShape, ID<Image> cirrusShape)
{
	auto atmosphereSystem = AtmosphereRenderSystem::Instance::get();
	auto transLutView = graphicsSystem->get(atmosphereSystem->getTransLUT())->getView();
	auto cameraVolumeView = graphicsSystem->get(atmosphereSystem->getCameraVolume())->getView();
	auto dataFieldsView = graphicsSystem->get(dataFields)->getView();
	auto vertProfileView = graphicsSystem->get(vertProfile)->getView();
	auto noiseShapeView = graphicsSystem->get(noiseShape)->getView();
	auto cirrusShapeView = graphicsSystem->get(cirrusShape)->getView();
	auto inFlightCount = graphicsSystem->getInFlightCount();

	DescriptorSet::Uniforms uniforms =
	{ 
		{ "transLUT", DescriptorSet::Uniform(transLutView, 1, inFlightCount) },
		{ "cameraVolume", DescriptorSet::Uniform(cameraVolumeView, 1, inFlightCount) },
		{ "dataFields", DescriptorSet::Uniform(dataFieldsView, 1, inFlightCount) },
		{ "vertProfile", DescriptorSet::Uniform(vertProfileView, 1, inFlightCount) },
		{ "noiseShape", DescriptorSet::Uniform(noiseShapeView, 1, inFlightCount) },
		{ "cirrusShape", DescriptorSet::Uniform(cirrusShapeView, 1, inFlightCount) },
		{ "cc", DescriptorSet::Uniform(graphicsSystem->getCommonConstantsBuffers()) }
	};
	return uniforms;
}
static DescriptorSet::Uniforms getViewBlendUniforms(GraphicsSystem* graphicsSystem, 
	ID<Image> cloudsCamView, ID<Image> cloudsCamViewDepth)
{
	auto depthBufferView = DeferredRenderSystem::Instance::get()->getDepthImageView();
	auto camViewView = graphicsSystem->get(cloudsCamView);
	auto camViewDepthView = graphicsSystem->get(cloudsCamViewDepth)->getView();
	DescriptorSet::ImageViews camViews = { { camViewView->getView(0, 0) }, { camViewView->getView(1, 0) } };

	DescriptorSet::Uniforms uniforms =
	{
		{ "depthBuffer", DescriptorSet::Uniform(depthBufferView, 1, 2) },
		{ "cloudsBuffer", DescriptorSet::Uniform(camViews) },
		{ "cloudsDepth", DescriptorSet::Uniform(camViewDepthView, 1, 2) }
	};
	return uniforms;
}
static DescriptorSet::Uniforms getSkyBlendUniforms(GraphicsSystem* graphicsSystem, ID<Image> cloudsSkybox)
{
	auto cloudsSkyboxView = graphicsSystem->get(cloudsSkybox)->getView();
	return { { "srcBuffer", DescriptorSet::Uniform(cloudsSkyboxView) } };
}
static DescriptorSet::Uniforms getShadowUniforms(GraphicsSystem* graphicsSystem, ID<Image> dataFields)
{
	auto hizBufferView = HizRenderSystem::Instance::get()->getView(1);
	auto dataFieldsView = graphicsSystem->get(dataFields)->getView();

	DescriptorSet::Uniforms uniforms =
	{ 
		{ "hizBuffer", DescriptorSet::Uniform(hizBufferView) },
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
	ECSM_SUBSCRIBE_TO_EVENT("PreSkyFaceRender", CloudsRenderSystem::preSkyFaceRender);
	ECSM_SUBSCRIBE_TO_EVENT("SkyFaceRender", CloudsRenderSystem::skyFaceRender);
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
		graphicsSystem->destroy(skyBlendDS);
		graphicsSystem->destroy(viewBlendDS);
		graphicsSystem->destroy(skyboxDS);
		graphicsSystem->destroy(camViewDS);
		graphicsSystem->destroy(skyboxFramebuffer);
		graphicsSystem->destroy(camViewFramebuffer);
		graphicsSystem->destroy(cloudsSkybox);
		graphicsSystem->destroy(cloudsCamViewDepth);
		graphicsSystem->destroy(cloudsCamView);
		graphicsSystem->destroy(cirrusShape);
		graphicsSystem->destroy(noiseShape);
		graphicsSystem->destroy(vertProfile);
		graphicsSystem->destroy(dataFields);
		graphicsSystem->destroy(shadowPipeline);
		graphicsSystem->destroy(skyBlendPipeline);
		graphicsSystem->destroy(viewBlendPipeline);
		graphicsSystem->destroy(skyboxPipeline);
		graphicsSystem->destroy(camViewPipeline);

		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreDeferredRender", CloudsRenderSystem::preDeferredRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreSkyFaceRender", CloudsRenderSystem::preSkyFaceRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("SkyFaceRender", CloudsRenderSystem::skyFaceRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreHdrRender", CloudsRenderSystem::preHdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("HdrRender", CloudsRenderSystem::hdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreShadowRender", CloudsRenderSystem::preShadowRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("ShadowRender", CloudsRenderSystem::shadowRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", CloudsRenderSystem::gBufferRecreate);
		ECSM_UNSUBSCRIBE_FROM_EVENT("QualityChange", CloudsRenderSystem::qualityChange);
	}
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
static float calcTemperatureDiff(float temperatureDiff) noexcept { return pow(saturate(temperatureDiff), 2.0f); }

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
		if (!vertProfile)
			vertProfile = createVertProfile();
		if (!noiseShape)
			noiseShape = createNoiseShape();
		if (!cirrusShape)
			cirrusShape = createCirrusShape();
		if (!cloudsCamView)
			cloudsCamView = createCloudsCamView(graphicsSystem);
		if (!cloudsCamViewDepth)
			cloudsCamViewDepth = createCloudsCamViewDepth(graphicsSystem);
		if (!camViewFramebuffer)
			camViewFramebuffer = createCamViewFramebuffer(graphicsSystem, cloudsCamView, cloudsCamViewDepth);
		if (!camViewPipeline)
			camViewPipeline = createCamViewPipeline(camViewFramebuffer, quality);
		if (!viewBlendPipeline)
			viewBlendPipeline = createViewBlendPipeline();
		isInitialized = true;
	}

	auto pipelineView = graphicsSystem->get(camViewPipeline);
	auto dataFieldsView = graphicsSystem->get(dataFields);
	auto vertProfileView = graphicsSystem->get(vertProfile);
	auto noiseShapeView = graphicsSystem->get(noiseShape);

	if (!pipelineView->isReady() || !dataFieldsView->isReady() || 
		!vertProfileView->isReady() || !noiseShapeView->isReady())
	{
		return;
	}

	if (!camViewDS)
	{
		auto uniforms = getCamViewUniforms(graphicsSystem, ID<Image>(dataFields), 
			ID<Image>(vertProfile), ID<Image>(noiseShape), ID<Image>(cirrusShape), cloudsCamView);
		camViewDS = graphicsSystem->createDescriptorSet(camViewPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(camViewDS, "descriptorSet.clouds.camView");
	}

	auto framebufferView = graphicsSystem->get(camViewFramebuffer);
	updateCamViewFramebuffer(graphicsSystem, cloudsCamView, cloudsCamViewDepth, framebufferView);
	pipelineView->updateFramebuffer(camViewFramebuffer);
}

//**********************************************************************************************************************
void CloudsRenderSystem::preSkyFaceRender()
{
	SET_CPU_ZONE_SCOPED("Clouds Pre Sky Face Render");

	if (!isEnabled || !camViewDS)
		return;

	auto manager = Manager::Instance::get();
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pbrLightingView = manager->tryGet<PbrLightingComponent>(graphicsSystem->camera);
	if (!pbrLightingView || !pbrLightingView->skybox)
		return;

	auto updateSkyboxFB = false;
	if (cloudsSkybox)
	{
		auto cloudsSkyboxView = graphicsSystem->get(cloudsSkybox);
		auto skyboxView = graphicsSystem->get(pbrLightingView->skybox);
		if (cloudsSkyboxView->getSize().getX() * 2 != skyboxView->getSize().getX())
		{
			graphicsSystem->destroy(cloudsSkybox);
			graphicsSystem->destroy(skyBlendDS);
			updateSkyboxFB = true;
		}
	}

	if (!cloudsSkybox)
		cloudsSkybox = createCloudsSkybox(graphicsSystem, ID<Image>(pbrLightingView->skybox));
	if (!skyboxFramebuffer)
		skyboxFramebuffer = createSkyboxFramebuffer(graphicsSystem, cloudsSkybox);

	auto framebufferView = graphicsSystem->get(skyboxFramebuffer);
	if (updateSkyboxFB)
	{
		auto cloudsSkyboxView = graphicsSystem->get(cloudsSkybox);
		Framebuffer::OutputAttachment colorAttachment(cloudsSkyboxView->getView(), framebufferFlags);
		framebufferView->update((uint2)cloudsSkyboxView->getSize(), &colorAttachment, 1);
	}

	if (!skyboxPipeline)
		skyboxPipeline = createSkyboxPipeline(skyboxFramebuffer, quality);

	auto pipelineView = graphicsSystem->get(skyboxPipeline);
	if (!pipelineView->isReady())
		return;
	
	if (!skyboxDS)
	{
		auto uniforms = getSkyboxUniforms(graphicsSystem, ID<Image>(dataFields),
			ID<Image>(vertProfile), ID<Image>(noiseShape), ID<Image>(cirrusShape));
		skyboxDS = graphicsSystem->createDescriptorSet(skyboxPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(skyboxDS, "descriptorSet.clouds.skybox");
	}

	auto atmosphereSystem = AtmosphereRenderSystem::Instance::get();
	auto inFlightIndex = graphicsSystem->getInFlightIndex();
	auto groundRadius = atmosphereSystem->groundRadius;
	auto& cc = graphicsSystem->getCommonConstants();
	pipelineView->updateFramebuffer(skyboxFramebuffer);

	SkyboxPC pc;
	pc.invViewProj = atmosphereSystem->getCurrentInvViewProj();
	pc.cameraPos = calcCameraPos(cc, groundRadius);
	pc.groundRadius = groundRadius;
	pc.atmTopRadius = groundRadius + atmosphereSystem->atmosphereHeight;
	pc.bottomRadius = groundRadius + bottomRadius;
	pc.topRadius = groundRadius + topRadius;
	pc.minDistance = minDistance;
	pc.maxDistance = maxDistance;
	pc.currentTime = calcCurrentTime(cc, currentTime);
	pc.cumulusCoverage = calcCoverage(cumulusCoverage);
	pc.cirrusCoverage = 1.0f - saturate(cirrusCoverage);
	pc.temperatureDiff = calcTemperatureDiff(temperatureDiff);

	SET_GPU_DEBUG_LABEL("Skybox Clouds");
	{
		RenderPass renderPass(skyboxFramebuffer, float4::zero);
		pipelineView->bind();
		pipelineView->setViewportScissor();
		pipelineView->bindDescriptorSet(skyboxDS, inFlightIndex);
		pipelineView->pushConstants(&pc);
		pipelineView->drawFullscreen();
	}
}
void CloudsRenderSystem::skyFaceRender()
{
	if (!isEnabled || !cloudsSkybox)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto skyboxFramebuffer = graphicsSystem->getCurrentFramebuffer();
	if (!skyBlendPipeline)
		skyBlendPipeline = createSkyBlendPipeline(skyboxFramebuffer);

	auto pipelineView = graphicsSystem->get(skyBlendPipeline);
	if (!pipelineView->isReady())
		return;

	if (!skyBlendDS)
	{
		auto uniforms = getSkyBlendUniforms(graphicsSystem, cloudsSkybox);
		skyBlendDS = graphicsSystem->createDescriptorSet(skyBlendPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(skyBlendDS, "descriptorSet.clouds.skyBlend");
	}

	SET_GPU_DEBUG_LABEL("Sky Face Clouds");
	pipelineView->updateFramebuffer(skyboxFramebuffer);
	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->bindDescriptorSet(skyBlendDS);
	pipelineView->drawFullscreen();
}

//**********************************************************************************************************************
void CloudsRenderSystem::preHdrRender()
{
	SET_CPU_ZONE_SCOPED("Clouds HDR Render");

	if (!isEnabled || !camViewDS)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto atmosphereSystem = AtmosphereRenderSystem::Instance::get();
	auto inFlightIndex = graphicsSystem->getInFlightIndex();
	auto currentFrameIndex = graphicsSystem->getCurrentFrameIndex();
	auto groundRadius = atmosphereSystem->groundRadius;
	auto& cc = graphicsSystem->getCommonConstants();
	auto pipelineView = graphicsSystem->get(camViewPipeline);

	auto bayerIndex = bayerIndices4x4[currentFrameIndex % 16];
	uint2 bayerPos; bayerPos.y = bayerIndex / 4;
	bayerPos.x = bayerIndex - bayerPos.y * 4;

	CamViewPC pc;
	pc.cameraPos = calcCameraPos(cc, groundRadius);
	pc.groundRadius = groundRadius;
	pc.bayerPos = bayerPos;
	pc.atmTopRadius = groundRadius + atmosphereSystem->atmosphereHeight;
	pc.bottomRadius = groundRadius + bottomRadius;
	pc.topRadius = groundRadius + topRadius;
	pc.minDistance = minDistance;
	pc.maxDistance = maxDistance;
	pc.currentTime = calcCurrentTime(cc, currentTime);
	pc.cumulusCoverage = calcCoverage(cumulusCoverage);
	pc.cirrusCoverage = 1.0f - saturate(cirrusCoverage);
	pc.temperatureDiff = calcTemperatureDiff(temperatureDiff);

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		SET_GPU_DEBUG_LABEL("Camera View Clouds");
		{
			constexpr const float4 clearColors[2] = { float4(0.0f), float4(0.0f) };
			RenderPass renderPass(camViewFramebuffer, clearColors, 2);
			pipelineView->bind();
			pipelineView->setViewportScissor();
			pipelineView->bindDescriptorSet(camViewDS, inFlightIndex);
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
		auto uniforms = getViewBlendUniforms(graphicsSystem, cloudsCamView, cloudsCamViewDepth);
		viewBlendDS = graphicsSystem->createDescriptorSet(viewBlendPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(viewBlendDS, "descriptorSet.clouds.viewBlend");
	}

	auto inFlightIndex = graphicsSystem->getInFlightIndex();

	SET_GPU_DEBUG_LABEL("View Clouds Blend");
	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->bindDescriptorSet(viewBlendDS, inFlightIndex);
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
	auto vertProfileView = graphicsSystem->get(vertProfile);
	if (!pipelineView->isReady() || !dataFieldsView->isReady() || !vertProfileView->isReady())
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
	pc.temperatureDiff = calcTemperatureDiff(temperatureDiff);

	SET_GPU_DEBUG_LABEL("Cloud Shadows");
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
	if (cloudsCamView)
	{
		graphicsSystem->destroy(cloudsCamView);
		cloudsCamView = createCloudsCamView(graphicsSystem);
	}
	if (cloudsCamViewDepth)
	{
		graphicsSystem->destroy(cloudsCamViewDepth);
		cloudsCamViewDepth = createCloudsCamViewDepth(graphicsSystem);
	}

	if (camViewFramebuffer)
	{
		auto framebufferView = graphicsSystem->get(camViewFramebuffer);
		updateCamViewFramebuffer(graphicsSystem, cloudsCamView, cloudsCamViewDepth, framebufferView);
	}

	graphicsSystem->destroy(camViewDS);
	graphicsSystem->destroy(viewBlendDS);
	graphicsSystem->destroy(shadowDS);
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
	graphicsSystem->destroy(camViewDS);
	graphicsSystem->destroy(skyboxDS);

	if (camViewPipeline)
	{
		graphicsSystem->destroy(camViewPipeline);
		camViewPipeline = createCamViewPipeline(camViewFramebuffer, quality);
	}
	if (skyboxPipeline)
	{
		graphicsSystem->destroy(skyboxPipeline);
		skyboxPipeline = createSkyboxPipeline(camViewFramebuffer, quality);
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
Ref<Image> CloudsRenderSystem::getVertProfile()
{
	if (!vertProfile)
		vertProfile = createVertProfile();
	return vertProfile;
}
Ref<Image> CloudsRenderSystem::getNoiseShape()
{
	if (!noiseShape)
		noiseShape = createNoiseShape();
	return noiseShape;
}
ID<Image> CloudsRenderSystem::getCloudsCamView()
{
	if (!cloudsCamView)
		cloudsCamView = createCloudsCamView(GraphicsSystem::Instance::get());
	return cloudsCamView;
}
ID<Image> CloudsRenderSystem::getCloudsCamViewDepth()
{
	if (!cloudsCamViewDepth)
		cloudsCamViewDepth = createCloudsCamViewDepth(GraphicsSystem::Instance::get());
	return cloudsCamViewDepth;
}

ID<Framebuffer> CloudsRenderSystem::getCamViewFramebuffer()
{
	if (!camViewFramebuffer)
	{
		camViewFramebuffer = createCamViewFramebuffer(GraphicsSystem::Instance::get(), 
			getCloudsCamView(), getCloudsCamViewDepth());
	}
	return camViewFramebuffer;
}

ID<GraphicsPipeline> CloudsRenderSystem::getCamViewPipeline()
{
	if (!camViewPipeline)
		camViewPipeline = createCamViewPipeline(getCamViewFramebuffer(), quality);
	return camViewPipeline;
}
ID<GraphicsPipeline> CloudsRenderSystem::getViewBlendPipeline()
{
	if (!viewBlendPipeline)
		viewBlendPipeline = createViewBlendPipeline();
	return viewBlendPipeline;
}
ID<GraphicsPipeline> CloudsRenderSystem::getShadowPipeline()
{
	if (!shadowPipeline)
		shadowPipeline = createShadowPipeline();
	return shadowPipeline;
}