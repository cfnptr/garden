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

#include "garden/system/render/atmosphere.hpp"
#include "garden/system/render/pbr-lighting.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/resource.hpp"
#include "atmosphere/constants.h"
#include "math/angles.hpp"

using namespace garden;

static ID<Image> createTransLUT(Image::Format format)
{
	auto transLUT = GraphicsSystem::Instance::get()->createImage(format, 
		Image::Usage::Sampled | Image::Usage::ColorAttachment, { { nullptr } }, 
		uint2(TRANSMITTANCE_LUT_WIDTH, TRANSMITTANCE_LUT_HEIGHT), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(transLUT, "image.atmosphere.transLUT");
	return transLUT;
}
static ID<Image> createMultiScatLUT()
{
	auto multiScatLUT = GraphicsSystem::Instance::get()->createImage(
		Image::Format::SfloatR16G16B16A16, Image::Usage::Sampled | Image::Usage::Storage, 
		{ { nullptr } }, uint2(MULTI_SCAT_LUT_LENGTH), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(multiScatLUT, "image.atmosphere.multiScatLUT");
	return multiScatLUT;
}
static ID<Image> createCameraVolume()
{
	auto cameraVolume = GraphicsSystem::Instance::get()->createImage(
		Image::Format::SfloatR16G16B16A16, Image::Usage::Sampled | Image::Usage::Storage, 
		{ { nullptr } }, uint3(CAMERA_VOLUME_LENGTH), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(cameraVolume, "image.atmosphere.cameraVolume");
	return cameraVolume;
}
static ID<Image> createSkyViewLUT()
{
	auto size = max(GraphicsSystem::Instance::get()->getScaledFrameSize() / 10, uint2::one);
	auto skyViewLUT = GraphicsSystem::Instance::get()->createImage(Image::Format::UfloatB10G11R11,
		Image::Usage::Sampled | Image::Usage::ColorAttachment, { { nullptr } }, size, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(skyViewLUT, "image.atmosphere.skyViewLUT");
	return skyViewLUT;
}

static constexpr Image::Format getTransLutFormat(GraphicsQuality quality) noexcept
{
	return quality == GraphicsQuality::PotatoPC ? Image::Format::UnormB8G8R8A8 : Image::Format::SfloatR16G16B16A16;
}

//**********************************************************************************************************************
static ID<Framebuffer> createScatLutFramebuffer(ID<Image> lut, const char* debugName)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto lutView = graphicsSystem->get(lut);
	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(lutView->getDefaultView(), { false, false, true }) };
	auto framebuffer = graphicsSystem->createFramebuffer(
		(uint2)lutView->getSize(), std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.atmosphere." + string(debugName));
	return framebuffer;
}

static void createSkyboxViews(ID<Image> skybox, ID<ImageView>* imageViews)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	for (uint8 i = 0; i < Image::cubemapSideCount; i++)
	{
		auto imageView = graphicsSystem->createImageView(skybox, 
			Image::Type::Texture2D, Image::Format::Undefined, 0, 1, i, 1);
		SET_RESOURCE_DEBUG_NAME(imageView, "imageView.atmosphere.skybox" + to_string(i));
		imageViews[i] = imageView;
	}
}
static void destroySkyboxViews(ID<ImageView>* imageViews)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	for (uint8 i = 0; i < Image::cubemapSideCount; i++)
	{
		graphicsSystem->destroy(imageViews[i]);
		imageViews[i] = {};
	}
}

static void createSkyboxFramebuffers(ID<Framebuffer>* framebuffers)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	for (uint8 i = 0; i < Image::cubemapSideCount; i++)
	{
		vector<Framebuffer::OutputAttachment> colorAttachments =
		{ Framebuffer::OutputAttachment({}, { false, false, true }) };
		auto framebuffer = graphicsSystem->createFramebuffer(uint2(16), std::move(colorAttachments));
		SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.atmosphere.skybox" + to_string(i));
		framebuffers[i] = framebuffer;
	}
}
static void destroySkyboxFramebuffers(ID<Framebuffer>* framebuffers)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	for (uint8 i = 0; i < Image::cubemapSideCount; i++)
	{
		graphicsSystem->destroy(framebuffers[i]);
		framebuffers[i] = {};
	}
}

//**********************************************************************************************************************
static ID<GraphicsPipeline> createTransLutPipeline(ID<Framebuffer> transLutFramebuffer, GraphicsQuality quality)
{
	float sampleCount;
	switch (quality)
	{
		case GraphicsQuality::Ultra: sampleCount = 30.0f; break;
		default: sampleCount = 10.0f; break;
	}
	Pipeline::SpecConstValues specConstValues = { { "SAMPLE_COUNT", Pipeline::SpecConstValue(sampleCount) } };

	ResourceSystem::GraphicsOptions options;
	options.specConstValues = &specConstValues;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline(
		"atmosphere/transmittance", transLutFramebuffer, options);
}
static ID<ComputePipeline> createMultiScatLutPipeline(GraphicsQuality quality)
{
	float sampleCount;
	switch (quality)
	{
		case GraphicsQuality::Ultra: sampleCount = 20.0f; break;
		default: sampleCount = 15.0f; break;
	}
	Pipeline::SpecConstValues specConstValues = { { "SAMPLE_COUNT", Pipeline::SpecConstValue(sampleCount) } };
	
	ResourceSystem::ComputeOptions options;
	options.specConstValues = &specConstValues;
	return ResourceSystem::Instance::get()->loadComputePipeline("atmosphere/multi-scattering", options);
}
static ID<ComputePipeline> createCameraVolumePipeline(GraphicsQuality quality)
{
	float sliceCount, kmPerSlice;
	switch (quality)
	{
		case GraphicsQuality::PotatoPC: sliceCount = 8.0f; kmPerSlice = 12.0f; break;
		case GraphicsQuality::Ultra: sliceCount = 32.0f; kmPerSlice = 3.0f; break;
		default: sliceCount = 16.0f; kmPerSlice = 6.0f; break;
	}

	Pipeline::SpecConstValues specConstValues =
	{
		{ "SLICE_COUNT", Pipeline::SpecConstValue(sliceCount) },
		{ "KM_PER_SLICE", Pipeline::SpecConstValue(kmPerSlice) }
	};

	ResourceSystem::ComputeOptions options;
	options.specConstValues = &specConstValues;
	return ResourceSystem::Instance::get()->loadComputePipeline("atmosphere/camera-volume", options);
}
static ID<GraphicsPipeline> createSkyViewLutPipeline(ID<Framebuffer> skyViewFramebuffer, GraphicsQuality quality)
{
	float rayMarchSppMin, rayMarchSppMax;
	switch (quality)
	{
		case GraphicsQuality::PotatoPC: rayMarchSppMin = 2.0f; rayMarchSppMax = 16.0f; break;
		case GraphicsQuality::Low: rayMarchSppMin = 4.0f; rayMarchSppMax = 32.0f; break;
		case GraphicsQuality::Medium: rayMarchSppMin = 4.0f; rayMarchSppMax = 64.0f; break;
		case GraphicsQuality::High: rayMarchSppMin = 4.0f; rayMarchSppMax = 128.0f; break;
		case GraphicsQuality::Ultra: rayMarchSppMin = 4.0f; rayMarchSppMax = 256.0f; break;
		default: abort();
	}

	Pipeline::SpecConstValues specConstValues =
	{
		{ "RAY_MARCH_SPP_MIN", Pipeline::SpecConstValue(rayMarchSppMin) },
		{ "RAY_MARCH_SPP_MAX", Pipeline::SpecConstValue(rayMarchSppMax) },
		{ "RAY_MARCH_SPP_DIST", Pipeline::SpecConstValue(150.0f) }
	};

	ResourceSystem::GraphicsOptions options;
	options.specConstValues = &specConstValues;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline(
		"atmosphere/sky-view", skyViewFramebuffer, options);
}
static ID<GraphicsPipeline> createSkyboxPipeline()
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	ResourceSystem::GraphicsOptions options;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline(
		"atmosphere/skybox", deferredSystem->getHdrFramebuffer(), options);
}

//**********************************************************************************************************************
static DescriptorSet::Uniforms getScatLutUniforms(ID<Image> transLUT, ID<Image> multiScatLUT)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto transLutView = graphicsSystem->get(transLUT)->getDefaultView();
	auto multiScatLutView = graphicsSystem->get(multiScatLUT)->getDefaultView();

	DescriptorSet::Uniforms uniforms =
	{
		{ "transLUT", DescriptorSet::Uniform(transLutView) },
		{ "multiScatLUT", DescriptorSet::Uniform(multiScatLutView) }
	};
	return uniforms;
}
static DescriptorSet::Uniforms getCameraVolumeUniforms(
	ID<Image> transLUT, ID<Image> multiScatLUT, ID<Image> cameraVolume)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto transLutView = graphicsSystem->get(transLUT)->getDefaultView();
	auto multiScatLutView = graphicsSystem->get(multiScatLUT)->getDefaultView();
	auto cameraVolumeView = graphicsSystem->get(cameraVolume)->getDefaultView();
	auto inFlightCount = graphicsSystem->getInFlightCount();

	DescriptorSet::Uniforms uniforms =
	{
		{ "transLUT", DescriptorSet::Uniform(transLutView, 1, inFlightCount) },
		{ "multiScatLUT", DescriptorSet::Uniform(multiScatLutView, 1, inFlightCount) },
		{ "cameraVolume", DescriptorSet::Uniform(cameraVolumeView, 1, inFlightCount) },
		{ "cc", DescriptorSet::Uniform(graphicsSystem->getCommonConstantsBuffers()) }
	};
	return uniforms;
}
static DescriptorSet::Uniforms getSkyboxUniforms(ID<Image> transLUT, ID<Image> skyViewLUT)
{
	auto depthBufferView = DeferredRenderSystem::Instance::get()->getDepthImageView();
	auto transLutView = GraphicsSystem::Instance::get()->get(transLUT)->getDefaultView();
	auto skyViewLutView = GraphicsSystem::Instance::get()->get(skyViewLUT)->getDefaultView();

	DescriptorSet::Uniforms uniforms =
	{
		{ "transLUT", DescriptorSet::Uniform(transLutView) },
		{ "skyViewLUT", DescriptorSet::Uniform(skyViewLutView) },
		{ "depthBuffer", DescriptorSet::Uniform(depthBufferView) }
	};
	return uniforms;
}

//**********************************************************************************************************************
AtmosphereRenderSystem::AtmosphereRenderSystem(bool setSingleton) :Singleton(setSingleton)
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", AtmosphereRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", AtmosphereRenderSystem::deinit);
}
AtmosphereRenderSystem::~AtmosphereRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", AtmosphereRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", AtmosphereRenderSystem::deinit);
	}

	unsetSingleton();
}

void AtmosphereRenderSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("PreDeferredRender", AtmosphereRenderSystem::preDeferredRender);
	ECSM_SUBSCRIBE_TO_EVENT("HdrRender", AtmosphereRenderSystem::hdrRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", AtmosphereRenderSystem::gBufferRecreate);
	ECSM_SUBSCRIBE_TO_EVENT("QualityChange", AtmosphereRenderSystem::qualityChange);
}
void AtmosphereRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(skyboxDS);
		graphicsSystem->destroy(skyViewLutDS);
		graphicsSystem->destroy(cameraVolumeDS);
		graphicsSystem->destroy(multiScatLutDS);
		graphicsSystem->destroy(skyboxPipeline);
		graphicsSystem->destroy(skyViewLutPipeline);
		graphicsSystem->destroy(cameraVolumePipeline);
		graphicsSystem->destroy(multiScatLutPipeline);
		graphicsSystem->destroy(transLutPipeline);
		destroySkyboxFramebuffers(skyboxFramebuffers);
		destroySkyboxViews(skyboxViews);
		graphicsSystem->destroy(skyViewLutFramebuffer);
		graphicsSystem->destroy(transLutFramebuffer);
		graphicsSystem->destroy(skyViewLUT);
		graphicsSystem->destroy(cameraVolume);
		graphicsSystem->destroy(multiScatLUT);
		graphicsSystem->destroy(transLUT);

		ECSM_UNSUBSCRIBE_FROM_EVENT("PreDeferredRender", AtmosphereRenderSystem::preDeferredRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("HdrRender", AtmosphereRenderSystem::hdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", AtmosphereRenderSystem::gBufferRecreate);
		ECSM_UNSUBSCRIBE_FROM_EVENT("QualityChange", AtmosphereRenderSystem::qualityChange);
	}
}

//**********************************************************************************************************************
void AtmosphereRenderSystem::preDeferredRender()
{
	if (!isEnabled)
		return;

	if (!isInitialized)
	{
		if (!transLUT)
			transLUT = createTransLUT(getTransLutFormat(quality));
		if (!multiScatLUT)
			multiScatLUT = createMultiScatLUT();
		if (!cameraVolume)
			cameraVolume = createCameraVolume();
		if (!skyViewLUT)
			skyViewLUT = createSkyViewLUT();
		if (!transLutFramebuffer)
			transLutFramebuffer = createScatLutFramebuffer(transLUT, "transLUT");
		if (!skyViewLutFramebuffer)
			skyViewLutFramebuffer = createScatLutFramebuffer(skyViewLUT, "skyViewLUT");
		if (!skyboxFramebuffers[0])
			createSkyboxFramebuffers(skyboxFramebuffers);
		if (!transLutPipeline)
			transLutPipeline = createTransLutPipeline(transLutFramebuffer, quality);
		if (!multiScatLutPipeline)
			multiScatLutPipeline = createMultiScatLutPipeline(quality);
		if (!cameraVolumePipeline)
			cameraVolumePipeline = createCameraVolumePipeline(quality);
		if (!skyViewLutPipeline)
			skyViewLutPipeline = createSkyViewLutPipeline(skyViewLutFramebuffer, quality);
		if (!skyboxPipeline)
			skyboxPipeline = createSkyboxPipeline();
		isInitialized = true;
	}

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto transLutPipelineView = graphicsSystem->get(transLutPipeline);
	auto multiScatLutPipelineView = graphicsSystem->get(multiScatLutPipeline);
	auto cameraVolumePipelineView = graphicsSystem->get(cameraVolumePipeline);
	auto skyViewLutPipelineView = graphicsSystem->get(skyViewLutPipeline);

	if (!transLutPipelineView->isReady() || !multiScatLutPipelineView->isReady() ||
		!cameraVolumePipelineView->isReady() || !skyViewLutPipelineView->isReady())
	{
		return;
	}

	if (!multiScatLutDS)
	{
		auto uniforms = getScatLutUniforms(transLUT, multiScatLUT);
		multiScatLutDS = graphicsSystem->createDescriptorSet(multiScatLutPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(multiScatLutDS, "descriptorSet.atmosphere.multiScatLUT");
	}
	if (!cameraVolumeDS)
	{
		auto uniforms = getCameraVolumeUniforms(transLUT, multiScatLUT, cameraVolume);
		cameraVolumeDS = graphicsSystem->createDescriptorSet(cameraVolumePipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(cameraVolumeDS, "descriptorSet.atmosphere.cameraVolume");
	}
	if (!skyViewLutDS)
	{
		auto uniforms = getScatLutUniforms(transLUT, multiScatLUT);
		skyViewLutDS = graphicsSystem->createDescriptorSet(skyViewLutPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(skyViewLutDS, "descriptorSet.atmosphere.skyViewLUT");
	}

	const auto& cc = graphicsSystem->getCommonConstants();
	auto multiScatMul = graphicsSystem->get(transLUT)->getFormat() == 
		Image::Format::SfloatR16G16B16A16 ? 1.0f : 2.0f;
	auto cameraHeight = fma(max(cc.cameraPos.getY(), 0.0f), 
		0.001f, groundRadius + PLANET_RADIUS_OFFSET);
	auto cameraPos = float3(0.0f, cameraHeight, 0.0f);
	auto topRadius = groundRadius + atmosphereHeight;
	auto sunDir = (float3)-cc.lightDir;
	auto rayleighScattering = (float3)this->rayleighScattering * this->rayleighScattering.w;
	auto rayDensityExpScale = -1.0f / rayleightScaleHeight;
	auto mieScattering = (float3)this->mieScattering * this->mieScattering.w;
	auto mieExtinction = mieScattering + (float3)mieAbsorption * mieAbsorption.w;
	auto mieDensityExpScale = -1.0f / mieScaleHeight;
	auto absorptionExtinction = (float3)ozoneAbsorption * ozoneAbsorption.w;
	auto absDensity0ConstantTerm = ozoneLayerTip - ozoneLayerWidth * ozoneLayerSlope;
	auto absDensity1ConstantTerm = ozoneLayerTip - ozoneLayerWidth * -ozoneLayerSlope;

	graphicsSystem->startRecording(CommandBufferType::Frame);
	BEGIN_GPU_DEBUG_LABEL("Atmosphere", Color::transparent);
	graphicsSystem->stopRecording();

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		auto framebufferView = graphicsSystem->get(transLutFramebuffer);
		TransmittancePC pc;
		pc.rayleighScattering = rayleighScattering;
		pc.rayDensityExpScale = rayDensityExpScale;
		pc.mieExtinction = mieExtinction;
		pc.mieDensityExpScale = mieDensityExpScale;
		pc.absorptionExtinction = absorptionExtinction;
		pc.absDensity0LayerWidth = ozoneLayerWidth;
		pc.sunDir = sunDir;
		pc.absDensity0ConstantTerm = absDensity0ConstantTerm;
		pc.absDensity0LinearTerm = ozoneLayerSlope;
		pc.absDensity1ConstantTerm = absDensity1ConstantTerm;
		pc.absDensity1LinearTerm = -ozoneLayerSlope;
		pc.bottomRadius = groundRadius;
		pc.topRadius = topRadius;

		SET_GPU_DEBUG_LABEL("Trans LUT", Color::transparent);
		framebufferView->beginRenderPass(float4::zero);
		transLutPipelineView->bind();
		transLutPipelineView->setViewportScissor();
		transLutPipelineView->pushConstants(&pc);
		transLutPipelineView->drawFullscreen();
		framebufferView->endRenderPass();
	}
	{
		MultiScatPC pc;
		pc.rayleighScattering = rayleighScattering;
		pc.rayDensityExpScale = rayDensityExpScale;
		pc.mieExtinction = mieExtinction;
		pc.mieDensityExpScale = mieDensityExpScale;
		pc.absorptionExtinction = absorptionExtinction;
		pc.miePhaseG = miePhaseG;
		pc.mieScattering = mieScattering;
		pc.absDensity0LayerWidth = ozoneLayerWidth;
		pc.groundAlbedo = groundAlbedo;
		pc.absDensity0ConstantTerm = absDensity0ConstantTerm;
		pc.absDensity0LinearTerm = ozoneLayerSlope;
		pc.absDensity1ConstantTerm = absDensity1ConstantTerm;
		pc.absDensity1LinearTerm = -ozoneLayerSlope;
		pc.bottomRadius = groundRadius;
		pc.topRadius = topRadius;
		pc.multiScatFactor = multiScatFactor * multiScatMul;

		SET_GPU_DEBUG_LABEL("Multi Scat LUT", Color::transparent);
		multiScatLutPipelineView->bind();
		multiScatLutPipelineView->bindDescriptorSet(multiScatLutDS);
		multiScatLutPipelineView->pushConstants(&pc);
		multiScatLutPipelineView->dispatch(uint2(MULTI_SCAT_LUT_LENGTH), false);
	}
	{
		auto inFlightIndex = graphicsSystem->getInFlightIndex();
		CameraVolumePC pc;
		pc.rayleighScattering = rayleighScattering;
		pc.rayDensityExpScale = rayDensityExpScale;
		pc.mieExtinction = mieExtinction;
		pc.mieDensityExpScale = mieDensityExpScale;
		pc.absorptionExtinction = absorptionExtinction;
		pc.miePhaseG = miePhaseG;
		pc.mieScattering = mieScattering;
		pc.absDensity0LayerWidth = ozoneLayerWidth;
		pc.sunDir = sunDir;
		pc.absDensity0ConstantTerm = absDensity0ConstantTerm;
		pc.cameraPos = cameraPos;
		pc.absDensity0LinearTerm = ozoneLayerSlope;
		pc.absDensity1ConstantTerm = absDensity1ConstantTerm;
		pc.absDensity1LinearTerm = -ozoneLayerSlope;
		pc.bottomRadius = groundRadius;
		pc.topRadius = topRadius;

		SET_GPU_DEBUG_LABEL("Camera Volume", Color::transparent);
		cameraVolumePipelineView->bind();
		cameraVolumePipelineView->bindDescriptorSet(cameraVolumeDS, inFlightIndex);
		cameraVolumePipelineView->pushConstants(&pc);
		cameraVolumePipelineView->dispatch(uint3(CAMERA_VOLUME_LENGTH));
	}
	{
		auto framebufferView = graphicsSystem->get(skyViewLutFramebuffer);
		SkyViewPC pc;
		pc.rayleighScattering = rayleighScattering;
		pc.rayDensityExpScale = rayDensityExpScale;
		pc.mieExtinction = mieExtinction;
		pc.mieDensityExpScale = mieDensityExpScale;
		pc.absorptionExtinction = absorptionExtinction;
		pc.miePhaseG = miePhaseG;
		pc.mieScattering = mieScattering;
		pc.absDensity0LayerWidth = ozoneLayerWidth;
		pc.sunDir = sunDir;
		pc.absDensity0ConstantTerm = absDensity0ConstantTerm;
		pc.cameraPos = cameraPos;
		pc.absDensity0LinearTerm = ozoneLayerSlope;
		pc.skyViewLutSize = framebufferView->getSize();
		pc.absDensity1ConstantTerm = absDensity1ConstantTerm;
		pc.absDensity1LinearTerm = -ozoneLayerSlope;
		pc.bottomRadius = groundRadius;
		pc.topRadius = topRadius;

		SET_GPU_DEBUG_LABEL("Sky View LUT", Color::transparent);
		framebufferView->beginRenderPass(float4::zero);
		skyViewLutPipelineView->bind();
		skyViewLutPipelineView->setViewportScissor();
		skyViewLutPipelineView->bindDescriptorSet(skyViewLutDS);
		skyViewLutPipelineView->pushConstants(&pc);
		skyViewLutPipelineView->drawFullscreen();
		framebufferView->endRenderPass();
	}
	graphicsSystem->stopRecording();

	auto pbrLightingView = PbrLightingSystem::Instance::get()->tryGetComponent(graphicsSystem->camera);
	if (pbrLightingView && pbrLightingView->cubemap)
	{
		if (lastSkybox != pbrLightingView->cubemap)
		{
			graphicsSystem->destroy(lastSkybox);
			lastSkybox = pbrLightingView->cubemap;

			destroySkyboxViews(skyboxViews);
			createSkyboxViews(ID<Image>(lastSkybox), skyboxViews);
		}

		// TODO: update sky color from SH.
		//auto skyColor = brdf::diffuseIrradiance(float3::top, shBuffer.data());
		//graphicsSystem->setSkyColor((float3)skyColor);
	}

	graphicsSystem->startRecording(CommandBufferType::Frame);
	END_GPU_DEBUG_LABEL();
	graphicsSystem->stopRecording();
}

//**********************************************************************************************************************
void AtmosphereRenderSystem::hdrRender()
{
	if (!isEnabled)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto skyboxPipelineView = graphicsSystem->get(skyboxPipeline);
	if (!skyboxPipelineView->isReady())
		return;

	if (!skyboxDS)
	{
		auto uniforms = getSkyboxUniforms(transLUT, skyViewLUT);
		skyboxDS = graphicsSystem->createDescriptorSet(skyboxPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(skyboxDS, "descriptorSet.atmosphere.skybox");
	}

	auto& cc = graphicsSystem->getCommonConstants();
	auto cameraHeight = fma(max(cc.cameraPos.getY(), 0.0f), 
		0.001f, groundRadius + PLANET_RADIUS_OFFSET);
	auto cameraPos = float3(0.0f, cameraHeight, 0.0f);
	skyboxPipelineView->updateFramebuffer(graphicsSystem->getCurrentFramebuffer());

	SkyboxPC pc;
	pc.invViewProj = (float4x4)cc.invViewProj;
	pc.cameraPos = cameraPos;
	pc.bottomRadius = groundRadius;
	pc.sunDir = (float3)-cc.lightDir;
	pc.topRadius = groundRadius + atmosphereHeight;
	pc.sunColor = (float3)sunColor * sunColor.w;
	pc.sunSize = cos(radians(sunAngularSize * 0.5f));

	SET_GPU_DEBUG_LABEL("Atmosphere Skybox", Color::transparent);
	skyboxPipelineView->bind();
	skyboxPipelineView->setViewportScissor();
	skyboxPipelineView->bindDescriptorSet(skyboxDS);
	skyboxPipelineView->pushConstants(&pc);
	skyboxPipelineView->drawFullscreen();
}

//**********************************************************************************************************************
void AtmosphereRenderSystem::gBufferRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (skyboxDS)
	{
		graphicsSystem->destroy(skyboxDS);
		skyboxDS = {};
	}
	if (skyViewLUT)
	{
		graphicsSystem->destroy(skyViewLUT);
		skyViewLUT = createSkyViewLUT();
	}
	if (skyViewLutFramebuffer)
	{
		auto skyViewLutView = graphicsSystem->get(skyViewLUT);
		auto framebufferView = graphicsSystem->get(skyViewLutFramebuffer);
		Framebuffer::OutputAttachment colorAttachment(
			skyViewLutView->getDefaultView(), { false, false, true });
		framebufferView->update((uint2)skyViewLutView->getSize(), &colorAttachment, 1);
	}
}

void AtmosphereRenderSystem::qualityChange()
{
	setQuality(GraphicsSystem::Instance::get()->quality);
}

void AtmosphereRenderSystem::setQuality(GraphicsQuality quality)
{
	if (this->quality == quality)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	graphicsSystem->destroy(skyViewLutDS);
	graphicsSystem->destroy(cameraVolumeDS);
	graphicsSystem->destroy(multiScatLutDS);
	multiScatLutDS = cameraVolumeDS = skyViewLutDS = {};

	if (transLUT)
	{
		graphicsSystem->destroy(transLUT);
		transLUT = createTransLUT(getTransLutFormat(quality));
	}

	this->quality = quality;
}

ID<Image> AtmosphereRenderSystem::getTransLUT()
{
	if (!transLUT)
		transLUT = createTransLUT(getTransLutFormat(quality));
	return transLUT;
}
ID<Image> AtmosphereRenderSystem::getMultiScatLUT()
{
	if (!multiScatLUT)
		multiScatLUT = createMultiScatLUT();
	return multiScatLUT;
}
ID<Image> AtmosphereRenderSystem::getCameraVolume()
{
	if (!cameraVolume)
		cameraVolume = createCameraVolume();
	return cameraVolume;
}
ID<Image> AtmosphereRenderSystem::getSkyViewLUT()
{
	if (!skyViewLUT)
		skyViewLUT = createSkyViewLUT();
	return skyViewLUT;
}