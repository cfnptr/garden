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

#include "garden/system/render/atmosphere.hpp"
#include "garden/system/render/pbr-lighting.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/settings.hpp"
#include "garden/system/camera.hpp"
#include "garden/profiler.hpp"

#include "atmosphere/constants.h"
#include "math/matrix/transform.hpp"
#include "math/normal-mapping.hpp"
#include "math/cone-tracing.hpp"
#include "math/angles.hpp"
#include "math/brdf.hpp"

// TOOD: !!! Take into account planet position for an atmosphere!!!

using namespace garden;

static constexpr int32 shCacheBinarySize = ibl::shCoeffCount * sizeof(f32x4);
static constexpr int32 shBinarySize = ibl::shCoeffCount * sizeof(f16x4);

static ID<Image> createTransLUT(GraphicsSystem* graphicsSystem, Image::Format format)
{
	auto transLUT = graphicsSystem->createImage(format, Image::Usage::Sampled | Image::Usage::ColorAttachment, 
		{ { nullptr } }, uint2(TRANSMITTANCE_LUT_WIDTH, TRANSMITTANCE_LUT_HEIGHT), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(transLUT, "image.atmosphere.transLUT");
	return transLUT;
}
static ID<Image> createMultiScatLUT(GraphicsSystem* graphicsSystem)
{
	auto multiScatLUT = graphicsSystem->createImage(Image::Format::SfloatR16G16B16A16, Image::Usage::Sampled | 
		Image::Usage::Storage, { { nullptr } }, uint2(MULTI_SCAT_LUT_LENGTH), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(multiScatLUT, "image.atmosphere.multiScatLUT");
	return multiScatLUT;
}
static ID<Image> createCameraVolume(GraphicsSystem* graphicsSystem)
{
	auto cameraVolume = graphicsSystem->createImage(Image::Format::SfloatR16G16B16A16, Image::Usage::Sampled | 
		Image::Usage::Storage, { { nullptr } }, uint3(CAMERA_VOLUME_LENGTH), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(cameraVolume, "image.atmosphere.cameraVolume");
	return cameraVolume;
}
static ID<Image> createSkyViewLUT(GraphicsSystem* graphicsSystem)
{
	auto size = max(graphicsSystem->getScaledFrameSize() / 10, uint2::one);
	auto skyViewLUT = graphicsSystem->createImage(Image::Format::UfloatB10G11R11, Image::Usage::Sampled | 
		Image::Usage::ColorAttachment, { { nullptr } }, size, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(skyViewLUT, "image.atmosphere.skyViewLUT");
	return skyViewLUT;
}

static ID<ImageView> createSkyboxShView(GraphicsSystem* graphicsSystem, ID<Image> skybox)
{
	auto skyboxView = graphicsSystem->get(skybox);
	auto skyboxShView = graphicsSystem->createImageView(skybox, 
		Image::Type::Texture2DArray, Image::Format::Undefined, 0, 1, 0, 0);
	SET_RESOURCE_DEBUG_NAME(skyboxShView, "imageView.atmosphere.skyboxSH");
	return skyboxShView;
}
static void createShCaches(GraphicsSystem* graphicsSystem, uint32 skyboxSize,
	View<ComputePipeline> shGenPipelineView, DescriptorSet::Buffers& shCaches)
{
	GARDEN_ASSERT(shGenPipelineView->getLocalSize().x == shGenPipelineView->getLocalSize().y);
	auto localSize = shGenPipelineView->getLocalSize().x;
	auto inFlightCount = graphicsSystem->getInFlightCount();
	shCaches.resize(inFlightCount);

	uint64 reducedSize = skyboxSize / localSize;
	reducedSize = reducedSize * reducedSize * Image::cubemapFaceCount;
	uint64 cacheSize = sizeof(uint4) + reducedSize * shCacheBinarySize + shCacheBinarySize;

	localSize = localSize * localSize;
	for (float i = ceil((float)reducedSize / localSize); i > 1.001f; i = ceil(i / localSize))
		cacheSize += (uint64)i * shCacheBinarySize;

	for (uint32 i = 0; i < inFlightCount; i++)
	{
		auto shCache = graphicsSystem->createBuffer(Buffer::Usage::Storage | Buffer::Usage::TransferDst, 
			Buffer::CpuAccess::RandomReadWrite, cacheSize, Buffer::Location::Auto, Buffer::Strategy::Size);
		SET_RESOURCE_DEBUG_NAME(shCache, "buffer.storage.atmosphere.shCache" + to_string(i));
		shCaches[i].push_back(shCache);
	}
}
static void createShStagings(GraphicsSystem* graphicsSystem, DescriptorSet::Buffers& shStagings)
{
	auto inFlightCount = graphicsSystem->getInFlightCount();
	shStagings.resize(inFlightCount);

	for (uint32 i = 0; i < inFlightCount; i++)
	{
		auto shStaging = graphicsSystem->createStagingBuffer(Buffer::CpuAccess::SequentialWrite, shBinarySize);
		SET_RESOURCE_DEBUG_NAME(shStaging, "buffer.staging.atmosphere.sh" + to_string(i));
		shStagings[i].push_back(shStaging);
	}
}

static constexpr Image::Format getTransLutFormat(GraphicsQuality quality) noexcept
{
	return quality == GraphicsQuality::PotatoPC ? Image::Format::UnormR8G8B8A8 : Image::Format::SfloatR16G16B16A16;
}

//**********************************************************************************************************************
static ID<Framebuffer> createScatLutFramebuffer(GraphicsSystem* graphicsSystem, ID<Image> lut, const char* debugName)
{
	auto lutView = graphicsSystem->get(lut);
	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(lutView->getDefaultView(), AtmosphereRenderSystem::framebufferFlags) };
	auto framebuffer = graphicsSystem->createFramebuffer((uint2)lutView->getSize(), std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.atmosphere." + string(debugName));
	return framebuffer;
}

static void createSkyboxViews(GraphicsSystem* graphicsSystem, ID<Image> skybox, ID<ImageView>* imageViews)
{
	for (uint8 i = 0; i < Image::cubemapFaceCount; i++)
	{
		auto imageView = graphicsSystem->createImageView(skybox, 
			Image::Type::Texture2D, Image::Format::Undefined, 0, 1, i, 1);
		SET_RESOURCE_DEBUG_NAME(imageView, "imageView.atmosphere.skybox" + to_string(i));
		imageViews[i] = imageView;
	}
}
static void destroySkyboxViews(GraphicsSystem* graphicsSystem, ID<ImageView>* imageViews)
{
	for (uint8 i = 0; i < Image::cubemapFaceCount; i++)
	{
		graphicsSystem->destroy(imageViews[i]);
		imageViews[i] = {};
	}
}

static void createSkyboxFramebuffers(GraphicsSystem* graphicsSystem, ID<Framebuffer>* framebuffers)
{
	for (uint8 i = 0; i < Image::cubemapFaceCount; i++)
	{
		vector<Framebuffer::OutputAttachment> colorAttachments =
		{ Framebuffer::OutputAttachment({}, AtmosphereRenderSystem::framebufferFlags) };
		auto framebuffer = graphicsSystem->createFramebuffer(uint2(16), std::move(colorAttachments));
		SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.atmosphere.skybox" + to_string(i));
		framebuffers[i] = framebuffer;
	}
}
static void destroySkyboxFramebuffers(GraphicsSystem* graphicsSystem, ID<Framebuffer>* framebuffers)
{
	for (uint8 i = 0; i < Image::cubemapFaceCount; i++)
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
		case GraphicsQuality::Medium: sampleCount = 20.0f; break;
		case GraphicsQuality::High: sampleCount = 30.0f; break;
		case GraphicsQuality::Ultra: sampleCount = 40.0f; break;
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
		case GraphicsQuality::Ultra: sampleCount = 30.0f; break;
		default: sampleCount = 20.0f; break;
	}
	Pipeline::SpecConstValues specConstValues = { { "SAMPLE_COUNT", Pipeline::SpecConstValue(sampleCount) } };
	
	ResourceSystem::ComputeOptions options;
	options.specConstValues = &specConstValues;
	return ResourceSystem::Instance::get()->loadComputePipeline("atmosphere/multi-scattering", options);
}
static ID<ComputePipeline> createCameraVolumePipeline(GraphicsQuality quality)
{
	float sliceCount, kmPerSlice; float samplesPerSlice;
	AtmosphereRenderSystem::getSliceQuality(quality, sliceCount, kmPerSlice);

	switch (quality)
	{
		case GraphicsQuality::PotatoPC: samplesPerSlice = 1.0f; break;
		case GraphicsQuality::Ultra: samplesPerSlice = 4.0f; break;
		default: samplesPerSlice = 2.0f; break;
	}

	Pipeline::SpecConstValues specConstValues =
	{
		{ "SLICE_COUNT", Pipeline::SpecConstValue(sliceCount) },
		{ "KM_PER_SLICE", Pipeline::SpecConstValue(kmPerSlice) },
		{ "SAMPLES_PER_SLICE", Pipeline::SpecConstValue(kmPerSlice) }
	};

	ResourceSystem::ComputeOptions options;
	options.specConstValues = &specConstValues;
	return ResourceSystem::Instance::get()->loadComputePipeline("atmosphere/camera-volume", options);
}

//**********************************************************************************************************************
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
static ID<GraphicsPipeline> createHdrSkyPipeline(GraphicsQuality quality)
{
	float sliceCount, kmPerSlice;
	AtmosphereRenderSystem::getSliceQuality(quality, sliceCount, kmPerSlice);

	Pipeline::SpecConstValues specConstValues =
	{
		{ "USE_CUBEMAP_ONLY", Pipeline::SpecConstValue(false) },
		{ "SLICE_COUNT", Pipeline::SpecConstValue(sliceCount) },
		{ "KM_PER_SLICE", Pipeline::SpecConstValue(kmPerSlice) }
	};
	GraphicsPipeline::BlendStates blendStates { { 0, { GraphicsPipeline::BlendState(true) } } };

	ResourceSystem::GraphicsOptions options;
	options.specConstValues = &specConstValues;
	options.blendStateOverrides = &blendStates;

	auto deferredSystem = DeferredRenderSystem::Instance::get();
	return ResourceSystem::Instance::get()->loadGraphicsPipeline(
		"atmosphere/skybox", deferredSystem->getHdrFramebuffer(), options);
}
static ID<GraphicsPipeline> createSkyboxPipeline(ID<Framebuffer> framebuffer, GraphicsQuality quality)
{
	float sliceCount, kmPerSlice;
	AtmosphereRenderSystem::getSliceQuality(quality, sliceCount, kmPerSlice);

	Pipeline::SpecConstValues specConstValues =
	{
		{ "USE_CUBEMAP_ONLY", Pipeline::SpecConstValue(true) },
		{ "SLICE_COUNT", Pipeline::SpecConstValue(sliceCount) },
		{ "KM_PER_SLICE", Pipeline::SpecConstValue(kmPerSlice) }
	};

	ResourceSystem::GraphicsOptions options;
	options.specConstValues = &specConstValues;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("atmosphere/skybox", framebuffer, options);
}

static ID<ComputePipeline> createShGeneratePipeline()
{
	ResourceSystem::ComputeOptions options;
	return ResourceSystem::Instance::get()->loadComputePipeline("atmosphere/sh-generate", options);
}
static ID<ComputePipeline> createShReducePipeline()
{
	ResourceSystem::ComputeOptions options;
	return ResourceSystem::Instance::get()->loadComputePipeline("atmosphere/sh-reduce", options);
}

//**********************************************************************************************************************
static DescriptorSet::Uniforms getScatLutUniforms(
	GraphicsSystem* graphicsSystem, ID<Image> transLUT, ID<Image> multiScatLUT)
{
	auto transLutView = graphicsSystem->get(transLUT)->getDefaultView();
	auto multiScatLutView = graphicsSystem->get(multiScatLUT)->getDefaultView();

	DescriptorSet::Uniforms uniforms =
	{
		{ "transLUT", DescriptorSet::Uniform(transLutView) },
		{ "multiScatLUT", DescriptorSet::Uniform(multiScatLutView) }
	};
	return uniforms;
}
static DescriptorSet::Uniforms getCameraVolumeUniforms(GraphicsSystem* graphicsSystem, 
	ID<Image> transLUT, ID<Image> multiScatLUT, ID<Image> cameraVolume)
{
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
static DescriptorSet::Uniforms getSkyUniforms(GraphicsSystem* graphicsSystem, 
	ID<Image> transLUT, ID<Image> skyViewLUT, ID<Image> cameraVolume)
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto depthBufferView = deferredSystem->getDepthImageView();
	auto gFramebufferView = graphicsSystem->get(deferredSystem->getGFramebuffer());
	auto transLutView = graphicsSystem->get(transLUT)->getDefaultView();
	auto skyViewLutView = graphicsSystem->get(skyViewLUT)->getDefaultView();
	auto cameraVolumeView = graphicsSystem->get(cameraVolume)->getDefaultView();

	DescriptorSet::Uniforms uniforms =
	{
		{ "depthBuffer", DescriptorSet::Uniform(depthBufferView) },
		{ "transLUT", DescriptorSet::Uniform(transLutView) },
		{ "skyViewLUT", DescriptorSet::Uniform(skyViewLutView) },
		{ "cameraVolume", DescriptorSet::Uniform(cameraVolumeView) }
	};
	return uniforms;
}

static DescriptorSet::Uniforms getShGenerateUniforms(GraphicsSystem* graphicsSystem, 
	ID<ImageView> skyboxShView, const DescriptorSet::Buffers& shCaches)
{
	auto inFlightCount = graphicsSystem->getInFlightCount();
	DescriptorSet::Uniforms uniforms =
	{
		{ "skybox", DescriptorSet::Uniform(skyboxShView, 1, inFlightCount) },
		{ "sh", DescriptorSet::Uniform(shCaches) }
	};
	return uniforms;
}
static DescriptorSet::Uniforms getShReduceUniforms(
	GraphicsSystem* graphicsSystem, const DescriptorSet::Buffers& shCaches)
{
	DescriptorSet::Uniforms uniforms = { { "sh", DescriptorSet::Uniform(shCaches) } };
	return uniforms;
}

//**********************************************************************************************************************
AtmosphereRenderSystem::AtmosphereRenderSystem(bool setSingleton) :Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", AtmosphereRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", AtmosphereRenderSystem::deinit);

	auto settingsSystem = SettingsSystem::Instance::tryGet();
	if (settingsSystem)
		settingsSystem->getType("atmosphere.quality", quality, graphicsQualityNames, (uint32)GraphicsQuality::Count);
}
AtmosphereRenderSystem::~AtmosphereRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", AtmosphereRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", AtmosphereRenderSystem::deinit);
	}

	unsetSingleton();
}

void AtmosphereRenderSystem::init()
{
	auto manager = Manager::Instance::get();
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
		graphicsSystem->destroy(iblDescriptorSets);
		graphicsSystem->destroy(shReduceDS);
		graphicsSystem->destroy(shGenerateDS);
		graphicsSystem->destroy(skyboxDS);
		graphicsSystem->destroy(hdrSkyDS);
		graphicsSystem->destroy(skyViewLutDS);
		graphicsSystem->destroy(cameraVolumeDS);
		graphicsSystem->destroy(multiScatLutDS);
		graphicsSystem->destroy(shReducePipeline);
		graphicsSystem->destroy(shGeneratePipeline);
		graphicsSystem->destroy(skyboxPipeline);
		graphicsSystem->destroy(hdrSkyPipeline);
		graphicsSystem->destroy(skyViewLutPipeline);
		graphicsSystem->destroy(cameraVolumePipeline);
		graphicsSystem->destroy(multiScatLutPipeline);
		graphicsSystem->destroy(transLutPipeline);
		destroySkyboxFramebuffers(graphicsSystem, skyboxFramebuffers);
		destroySkyboxViews(graphicsSystem, skyboxViews);
		graphicsSystem->destroy(skyViewLutFramebuffer);
		graphicsSystem->destroy(transLutFramebuffer);
		graphicsSystem->destroy(shStagings);
		graphicsSystem->destroy(shCaches);
		graphicsSystem->destroy(specularCache);
		graphicsSystem->destroy(specularViews);
		graphicsSystem->destroy(lastSkyboxShView);
		graphicsSystem->destroy(lastSpecular);
		graphicsSystem->destroy(lastSkybox);
		graphicsSystem->destroy(skyViewLUT);
		graphicsSystem->destroy(cameraVolume);
		graphicsSystem->destroy(multiScatLUT);
		graphicsSystem->destroy(transLUT);

		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreDeferredRender", AtmosphereRenderSystem::preDeferredRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("HdrRender", AtmosphereRenderSystem::hdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", AtmosphereRenderSystem::gBufferRecreate);
		ECSM_UNSUBSCRIBE_FROM_EVENT("QualityChange", AtmosphereRenderSystem::qualityChange);
	}
}

static float calcCameraHeight(float cameraPosY, float groundRadius) noexcept
{
	return fma(max(cameraPosY, 0.0f), 0.001f, groundRadius + PLANET_RADIUS_OFFSET);
}
static float calcStarSize(float starAngularSize, float starDirY) noexcept
{
	auto horizonMul = fma(pow(saturate(1.0f - starDirY), 8.0f), 2.0, 1.0f);
	return cos(radians(starAngularSize * horizonMul * 0.5f));
}

static f32x4 calcAmbientLight(float3 upDir, f32x4 lightDir, f32x4* shBuffer)
{
	auto tbn = (f32x4x4)approximateTBN(normalize(upDir));
	auto amientLight = f32x4::zero; auto totalWeight = 0.0f;

	for (uint8 i = 0; i < diffuseConeCount; i++)
	{
		auto sampleDir = normalize3(tbn * f32x4(diffuseConeDirs[i]));
		auto weight = diffuseConeWeights[i] * saturate(dot3(lightDir, sampleDir) + 0.5f);
		amientLight += brdf::diffuseIrradiance(sampleDir, shBuffer) * weight;
		totalWeight += weight;
	}

	return totalWeight > 0.0f ? amientLight / totalWeight : f32x4::zero;
}

//**********************************************************************************************************************
void AtmosphereRenderSystem::preDeferredRender()
{
	SET_CPU_ZONE_SCOPED("Atmosphere Pre Deferred Render");

	if (!isEnabled)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!isInitialized)
	{
		if (!transLUT)
			transLUT = createTransLUT(graphicsSystem, getTransLutFormat(quality));
		if (!multiScatLUT)
			multiScatLUT = createMultiScatLUT(graphicsSystem);
		if (!cameraVolume)
			cameraVolume = createCameraVolume(graphicsSystem);
		if (!skyViewLUT)
			skyViewLUT = createSkyViewLUT(graphicsSystem);
		if (!transLutFramebuffer)
			transLutFramebuffer = createScatLutFramebuffer(graphicsSystem, transLUT, "transLUT");
		if (!skyViewLutFramebuffer)
			skyViewLutFramebuffer = createScatLutFramebuffer(graphicsSystem, skyViewLUT, "skyViewLUT");
		if (!skyboxFramebuffers[0])
			createSkyboxFramebuffers(graphicsSystem, skyboxFramebuffers);
		if (!transLutPipeline)
			transLutPipeline = createTransLutPipeline(transLutFramebuffer, quality);
		if (!multiScatLutPipeline)
			multiScatLutPipeline = createMultiScatLutPipeline(quality);
		if (!cameraVolumePipeline)
			cameraVolumePipeline = createCameraVolumePipeline(quality);
		if (!skyViewLutPipeline)
			skyViewLutPipeline = createSkyViewLutPipeline(skyViewLutFramebuffer, quality);
		if (!hdrSkyPipeline)
			hdrSkyPipeline = createHdrSkyPipeline(quality);
		isInitialized = true;
	}

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
		auto uniforms = getScatLutUniforms(graphicsSystem, transLUT, multiScatLUT);
		multiScatLutDS = graphicsSystem->createDescriptorSet(multiScatLutPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(multiScatLutDS, "descriptorSet.atmosphere.multiScatLUT");
	}
	if (!cameraVolumeDS)
	{
		auto uniforms = getCameraVolumeUniforms(graphicsSystem, transLUT, multiScatLUT, cameraVolume);
		cameraVolumeDS = graphicsSystem->createDescriptorSet(cameraVolumePipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(cameraVolumeDS, "descriptorSet.atmosphere.cameraVolume");
	}
	if (!skyViewLutDS)
	{
		auto uniforms = getScatLutUniforms(graphicsSystem, transLUT, multiScatLUT);
		skyViewLutDS = graphicsSystem->createDescriptorSet(skyViewLutPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(skyViewLutDS, "descriptorSet.atmosphere.skyViewLUT");
	}

	const auto& cc = graphicsSystem->getCommonConstants();
	auto cameraHeight = calcCameraHeight(cc.cameraPos.y, groundRadius);
	auto cameraPos = float3(0.0f, cameraHeight, 0.0f);
	auto topRadius = groundRadius + atmosphereHeight;
	auto rayDensityExpScale = -1.0f / rayleightScaleHeight;
	auto mieExtinction = mieScattering + mieAbsorption;
	auto mieDensityExpScale = -1.0f / mieScaleHeight;
	auto absDensity0ConstantTerm = ozoneLayerTip - ozoneLayerWidth * ozoneLayerSlope;
	auto absDensity1ConstantTerm = ozoneLayerTip - ozoneLayerWidth * -ozoneLayerSlope;

	graphicsSystem->startRecording(CommandBufferType::Frame);
	BEGIN_GPU_DEBUG_LABEL("Atmosphere");
	graphicsSystem->stopRecording();

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		TransmittancePC pc;
		pc.rayleighScattering = rayleighScattering;
		pc.rayDensityExpScale = rayDensityExpScale;
		pc.mieExtinction = mieExtinction;
		pc.mieDensityExpScale = mieDensityExpScale;
		pc.absorptionExtinction = ozoneAbsorption;
		pc.absDensity0LayerWidth = ozoneLayerWidth;
		pc.starDir = -cc.lightDir;
		pc.absDensity0ConstantTerm = absDensity0ConstantTerm;
		pc.absDensity0LinearTerm = ozoneLayerSlope;
		pc.absDensity1ConstantTerm = absDensity1ConstantTerm;
		pc.absDensity1LinearTerm = -ozoneLayerSlope;
		pc.bottomRadius = groundRadius;
		pc.topRadius = topRadius;

		SET_GPU_DEBUG_LABEL("Trans LUT");
		{
			RenderPass renderPass(transLutFramebuffer, float4::zero);
			transLutPipelineView->bind();
			transLutPipelineView->setViewportScissor();
			transLutPipelineView->pushConstants(&pc);
			transLutPipelineView->drawFullscreen();
		}
	}
	{
		MultiScatPC pc;
		pc.rayleighScattering = rayleighScattering;
		pc.rayDensityExpScale = rayDensityExpScale;
		pc.mieExtinction = mieExtinction;
		pc.mieDensityExpScale = mieDensityExpScale;
		pc.absorptionExtinction = ozoneAbsorption;
		pc.miePhaseG = saturate(miePhaseG);
		pc.mieScattering = mieScattering;
		pc.absDensity0LayerWidth = ozoneLayerWidth;
		pc.groundAlbedo = groundAlbedo;
		pc.absDensity0ConstantTerm = absDensity0ConstantTerm;
		pc.absDensity0LinearTerm = ozoneLayerSlope;
		pc.absDensity1ConstantTerm = absDensity1ConstantTerm;
		pc.absDensity1LinearTerm = -ozoneLayerSlope;
		pc.bottomRadius = groundRadius;
		pc.topRadius = topRadius;
		pc.multiScatFactor = multiScatFactor;

		SET_GPU_DEBUG_LABEL("Multi Scat LUT");
		{
			multiScatLutPipelineView->bind();
			multiScatLutPipelineView->bindDescriptorSet(multiScatLutDS);
			multiScatLutPipelineView->pushConstants(&pc);
			multiScatLutPipelineView->dispatch(uint2(MULTI_SCAT_LUT_LENGTH), false);
		}
	}
	{
		CameraVolumePC pc;
		pc.rayleighScattering = rayleighScattering;
		pc.rayDensityExpScale = rayDensityExpScale;
		pc.mieExtinction = mieExtinction;
		pc.mieDensityExpScale = mieDensityExpScale;
		pc.absorptionExtinction = ozoneAbsorption;
		pc.miePhaseG = saturate(miePhaseG);
		pc.mieScattering = mieScattering;
		pc.absDensity0LayerWidth = ozoneLayerWidth;
		pc.starDir = -cc.lightDir;
		pc.absDensity0ConstantTerm = absDensity0ConstantTerm;
		pc.cameraPos = cameraPos;
		pc.absDensity0LinearTerm = ozoneLayerSlope;
		pc.absDensity1ConstantTerm = absDensity1ConstantTerm;
		pc.absDensity1LinearTerm = -ozoneLayerSlope;
		pc.bottomRadius = groundRadius;
		pc.topRadius = topRadius;
		auto inFlightIndex = graphicsSystem->getInFlightIndex();

		SET_GPU_DEBUG_LABEL("Camera Volume");
		{
			cameraVolumePipelineView->bind();
			cameraVolumePipelineView->bindDescriptorSet(cameraVolumeDS, inFlightIndex);
			cameraVolumePipelineView->pushConstants(&pc);
			cameraVolumePipelineView->dispatch(uint3(CAMERA_VOLUME_LENGTH));
		}
	}
	{
		auto framebufferView = graphicsSystem->get(skyViewLutFramebuffer);
		SkyViewPC pc;
		pc.rayleighScattering = rayleighScattering;
		pc.rayDensityExpScale = rayDensityExpScale;
		pc.mieExtinction = mieExtinction;
		pc.mieDensityExpScale = mieDensityExpScale;
		pc.absorptionExtinction = ozoneAbsorption;
		pc.miePhaseG = saturate(miePhaseG);
		pc.mieScattering = mieScattering;
		pc.absDensity0LayerWidth = ozoneLayerWidth;
		pc.starDir = -cc.lightDir;
		pc.absDensity0ConstantTerm = absDensity0ConstantTerm;
		pc.cameraPos = cameraPos;
		pc.absDensity0LinearTerm = ozoneLayerSlope;
		pc.skyViewLutSize = framebufferView->getSize();
		pc.absDensity1ConstantTerm = absDensity1ConstantTerm;
		pc.absDensity1LinearTerm = -ozoneLayerSlope;
		pc.bottomRadius = groundRadius;
		pc.topRadius = topRadius;

		SET_GPU_DEBUG_LABEL("Sky View LUT");
		{
			RenderPass renderPass(skyViewLutFramebuffer, float4::zero);
			skyViewLutPipelineView->bind();
			skyViewLutPipelineView->setViewportScissor();
			skyViewLutPipelineView->bindDescriptorSet(skyViewLutDS);
			skyViewLutPipelineView->pushConstants(&pc);
			skyViewLutPipelineView->drawFullscreen();
		}
	}
	graphicsSystem->stopRecording();

	updateSkybox();

	graphicsSystem->startRecording(CommandBufferType::Frame);
	END_GPU_DEBUG_LABEL();
	graphicsSystem->stopRecording();
}

//**********************************************************************************************************************
void AtmosphereRenderSystem::updateSkybox()
{
	auto manager = Manager::Instance::get();
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pbrLightingView = manager->tryGet<PbrLightingComponent>(graphicsSystem->camera);

	if (!pbrLightingView)
		return;

	if (lastSkybox != pbrLightingView->skybox || lastSpecular != pbrLightingView->specular)
	{
		graphicsSystem->destroy(shReduceDS); graphicsSystem->destroy(shGenerateDS); 
		graphicsSystem->destroy(lastSkyboxShView); graphicsSystem->destroy(shCaches);
		graphicsSystem->destroy(lastSpecular); graphicsSystem->destroy(lastSkybox);
		shGenerateDS = {}; shReduceDS = {}; lastSkyboxShView = {}; lastSpecular = {}; lastSkybox = {};
		destroySkyboxViews(graphicsSystem, skyboxViews); shCaches.clear();

		graphicsSystem->destroy(iblDescriptorSets); graphicsSystem->destroy(specularViews);
		graphicsSystem->destroy(specularCache); specularCache = {};
		iblDescriptorSets.clear(); specularViews.clear();

		if (pbrLightingView->skybox && pbrLightingView->specular)
		{
			lastSkybox = pbrLightingView->skybox; lastSpecular = pbrLightingView->specular;
			createSkyboxViews(graphicsSystem, ID<Image>(lastSkybox), skyboxViews);

			auto skyboxView = graphicsSystem->get(lastSkybox);
			auto framebufferSize = (uint2)skyboxView->getSize();
			Framebuffer::OutputAttachment colorAttachment = Framebuffer::OutputAttachment(
				{}, AtmosphereRenderSystem::framebufferFlags);

			for (uint8 i = 0; i < Image::cubemapFaceCount; i++)
			{
				auto framebufferView = graphicsSystem->get(skyboxFramebuffers[i]);
				colorAttachment.imageView = skyboxViews[i];
				framebufferView->update(framebufferSize, &colorAttachment, 1);
			}

			auto pbrLightingSystem = PbrLightingSystem::Instance::get();
			graphicsSystem->startRecording(CommandBufferType::Frame);
			specularCache = pbrLightingSystem->createSpecularCache(
				skyboxView->getSize().getX(), iblWeightBuffer, iblCountBuffer);
			SET_RESOURCE_DEBUG_NAME(specularCache, "buffer.storage.atmosphere.specularCache");
			graphicsSystem->stopRecording();

			pbrLightingSystem->createIblSpecularViews(ID<Image>(lastSpecular), specularViews);
			pbrLightingSystem->createIblDescriptorSets(ID<Image>(lastSkybox), 
				specularCache, specularViews, iblDescriptorSets);
		}
		else if (skyboxFramebuffers[0])
		{
			auto framebufferSize = graphicsSystem->get(skyboxFramebuffers[0])->getSize();
			Framebuffer::OutputAttachment colorAttachment = Framebuffer::OutputAttachment(
				{}, AtmosphereRenderSystem::framebufferFlags);
			for (uint8 i = 0; i < Image::cubemapFaceCount; i++)
			{
				auto framebufferView = graphicsSystem->get(skyboxFramebuffers[i]);
				colorAttachment.imageView = skyboxViews[i];
				framebufferView->update(framebufferSize, &colorAttachment, 1);
			}
		}
	}

	if (!lastSkybox)
		return;

	graphicsSystem->startRecording(CommandBufferType::Frame);
	renderSkyboxFaces();
	generateSkySH(ID<Buffer>(pbrLightingView->shBuffer), pbrLightingView->shCoeffs);
	graphicsSystem->stopRecording();

	updatePhase = (updatePhase + 1) % (Image::cubemapFaceCount + 1);
}
void AtmosphereRenderSystem::renderSkyboxFaces()
{
	uint8 faceIndex, faceCount;
	if (noDelay)
	{
		faceIndex = 0; faceCount = Image::cubemapFaceCount;
	}
	else
	{
		if (updatePhase >= Image::cubemapFaceCount)
			return;
		faceIndex = updatePhase; faceCount = updatePhase + 1;
	}

	auto framebuffer = skyboxFramebuffers[0];
	if (!skyboxPipeline)
		skyboxPipeline = createSkyboxPipeline(framebuffer, quality);

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(skyboxPipeline);		
	if (!pipelineView->isReady())
		return;

	if (!skyboxDS)
	{
		auto uniforms = getSkyUniforms(graphicsSystem, transLUT, skyViewLUT, cameraVolume);
		skyboxDS = graphicsSystem->createDescriptorSet(skyboxPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(skyboxDS, "descriptorSet.atmosphere.skybox");
	}

	auto pbrLightingSystem = PbrLightingSystem::Instance::get();
	auto& cc = graphicsSystem->getCommonConstants();
	auto cameraHeight = calcCameraHeight(cc.cameraPos.y, groundRadius);

	SkyPushConstants pc;
	pc.cameraPos = float3(0.0f, cameraHeight, 0.0f);
	pc.bottomRadius = groundRadius;
	pc.starDir = -cc.lightDir;
	pc.topRadius = groundRadius + atmosphereHeight;
	pc.starColor = (float3)starColor * starColor.w;
	pc.starSize = calcStarSize(starAngularSize * 2.0f, pc.starDir.y);

	while (faceIndex < faceCount)
	{
		framebuffer = skyboxFramebuffers[faceIndex];
		pipelineView->updateFramebuffer(framebuffer);

		pc.invViewProj = (float4x4)inverse4x4((f32x4x4)calcPerspProjInfRevZ(
			radians(90.0f), 1.0f, defaultHmdDepth) * sideLookAts[faceIndex]);

		SET_GPU_DEBUG_LABEL("Skybox");
		{
			RenderPass renderPass(framebuffer, float4::zero);
			pipelineView->bind();
			pipelineView->setViewportScissor();
			pipelineView->bindDescriptorSet(skyboxDS);
			pipelineView->pushConstants(&pc);
			pipelineView->drawFullscreen();
		}

		pbrLightingSystem->dispatchIblSpecular(ID<Image>(lastSkybox), ID<Image>(lastSpecular), 
			iblWeightBuffer, iblCountBuffer, iblDescriptorSets, faceIndex);
		faceIndex++;
	}
}
void AtmosphereRenderSystem::generateSkySH(ID<Buffer> shBuffer, f32x4* shCoeffs)
{
	if (!noDelay && updatePhase < Image::cubemapFaceCount)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto& cc = graphicsSystem->getCommonConstants();
	auto skyboxView = graphicsSystem->get(lastSkybox);
	skyboxView->generateMips(Sampler::Filter::Linear);

	if (!shGeneratePipeline)
		shGeneratePipeline = createShGeneratePipeline();
	if (!shReducePipeline)
		shReducePipeline = createShReducePipeline();

	auto pbrLightingSystem = PbrLightingSystem::Instance::get();
	auto shGenPipelineView = graphicsSystem->get(shGeneratePipeline);
	auto shRedPipelineView = graphicsSystem->get(shReducePipeline);

	if (shBuffer && shGenPipelineView->isReady() && shRedPipelineView->isReady())
	{
		auto skyboxSize = skyboxView->getSize().getX();

		auto isFirstSH = false;
		if (shCaches.empty())
		{
			createShCaches(graphicsSystem, skyboxSize, shGenPipelineView, shCaches);
			isFirstSH = true;
		}

		if (!lastSkyboxShView)
			lastSkyboxShView = createSkyboxShView(graphicsSystem, ID<Image>(lastSkybox));
		if (shStagings.empty())
			createShStagings(graphicsSystem, shStagings);

		if (!shGenerateDS)
		{
			auto uniforms = getShGenerateUniforms(graphicsSystem, lastSkyboxShView, shCaches);
			shGenerateDS = graphicsSystem->createDescriptorSet(shGeneratePipeline, std::move(uniforms));
			SET_RESOURCE_DEBUG_NAME(shGenerateDS, "descriptorSet.atmosphere.shGenerate");
		}
		if (!shReduceDS)
		{
			auto uniforms = getShReduceUniforms(graphicsSystem, shCaches);
			shReduceDS = graphicsSystem->createDescriptorSet(shReducePipeline, std::move(uniforms));
			SET_RESOURCE_DEBUG_NAME(shReduceDS, "descriptorSet.atmosphere.shReduce");
		}

		SET_GPU_DEBUG_LABEL("Generate SH");
		auto shCacheView = graphicsSystem->get(shCaches[shInFlightIndex][0]);
		shCacheView->fill(0, sizeof(uint32));

		shGenPipelineView->bind();
		shGenPipelineView->bindDescriptorSet(shGenerateDS, shInFlightIndex);
		shGenPipelineView->dispatch(uint3(skyboxSize, skyboxSize, Image::cubemapFaceCount));

		auto localSize = shGenPipelineView->getLocalSize().x;
		GARDEN_ASSERT(shRedPipelineView->getLocalSize().x == localSize * localSize);
		auto reducedSize = skyboxSize / localSize;
		reducedSize = reducedSize * reducedSize * Image::cubemapFaceCount;
		localSize = localSize * localSize;

		ShReducePC pc;
		pc.offset = 0;

		shRedPipelineView->bind();
		// Note: do not move bindDescriptorSet, due to memory barriers!

		for (float i = reducedSize; i > 1.001f; i = ceil(i / localSize))
		{ 
			shRedPipelineView->bindDescriptorSet(shReduceDS, shInFlightIndex);
			shRedPipelineView->pushConstants(&pc);
			shRedPipelineView->dispatch((uint32)i);
			pc.offset += (uint32)i;
		}

		auto inFlightCount = graphicsSystem->getInFlightCount();
		shInFlightIndex = (shInFlightIndex + 1) % inFlightCount;

		if (!isFirstSH)
		{
			shCacheView = graphicsSystem->get(shCaches[shInFlightIndex][0]);
			auto mapOffset = shCacheView->getBinarySize() - shCacheBinarySize;
			shCacheView->invalidate(shCacheBinarySize, mapOffset);

			memcpy(shCoeffs, shCacheView->getMap() + mapOffset, shCacheBinarySize);
			for (uint8 i = 0; i < ibl::shCoeffCount; i++) shCoeffs[i] *= giFactor;
			PbrLightingSystem::processIblSH(shCoeffs);

			f16x4 shCoeffs16[ibl::shCoeffCount];
			for (uint8 i = 0; i < ibl::shCoeffCount; i++)
				shCoeffs16[i] = (f16x4)min(shCoeffs[i], f32x4(65504.0f));

			auto shStaging = shStagings[shInFlightIndex][0];
			auto shStagingView = graphicsSystem->get(shStaging);
			memcpy(shStagingView->getMap(), shCoeffs16, shBinarySize);
			shStagingView->flush();
			Buffer::copy(shStaging, shBuffer);

			auto light = brdf::diffuseIrradiance(-f32x4(cc.lightDir), shCoeffs);
			graphicsSystem->setStarLight((float3)light);
			light = calcAmbientLight(graphicsSystem->upDirection, f32x4(cc.lightDir), shCoeffs);
			graphicsSystem->setAmbientLight((float3)light);
		}
	}
}

//**********************************************************************************************************************
void AtmosphereRenderSystem::hdrRender()
{
	SET_CPU_ZONE_SCOPED("Atmosphere HDR Render");

	if (!isEnabled)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(hdrSkyPipeline);
	if (!pipelineView->isReady())
		return;

	if (!hdrSkyDS)
	{
		auto uniforms = getSkyUniforms(graphicsSystem, transLUT, skyViewLUT, cameraVolume);
		hdrSkyDS = graphicsSystem->createDescriptorSet(hdrSkyPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(hdrSkyDS, "descriptorSet.atmosphere.hdrSky");
	}

	auto& cc = graphicsSystem->getCommonConstants();
	auto cameraHeight = calcCameraHeight(cc.cameraPos.y, groundRadius);
	pipelineView->updateFramebuffer(graphicsSystem->getCurrentFramebuffer());

	SkyPushConstants pc;
	pc.invViewProj = (float4x4)cc.invViewProj;
	pc.cameraPos = float3(0.0f, cameraHeight, 0.0f);
	pc.bottomRadius = groundRadius;
	pc.starDir = -cc.lightDir;
	pc.topRadius = groundRadius + atmosphereHeight;
	pc.starColor = (float3)starColor * starColor.w;
	pc.starSize = calcStarSize(starAngularSize, pc.starDir.y);

	SET_GPU_DEBUG_LABEL("Atmosphere Skybox");
	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->bindDescriptorSet(hdrSkyDS);
	pipelineView->pushConstants(&pc);
	pipelineView->drawFullscreen();
}

void AtmosphereRenderSystem::gBufferRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (skyboxDS)
	{
		graphicsSystem->destroy(skyboxDS);
		skyboxDS = {};
	}
	if (hdrSkyDS)
	{
		graphicsSystem->destroy(hdrSkyDS);
		hdrSkyDS = {};
	}
	if (skyViewLUT)
	{
		graphicsSystem->destroy(skyViewLUT);
		skyViewLUT = createSkyViewLUT(graphicsSystem);
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

//**********************************************************************************************************************
void AtmosphereRenderSystem::qualityChange()
{
	setQuality(GraphicsSystem::Instance::get()->quality);
}

void AtmosphereRenderSystem::setQuality(GraphicsQuality quality)
{
	if (this->quality == quality)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	graphicsSystem->destroy(skyboxDS);
	graphicsSystem->destroy(hdrSkyDS);
	graphicsSystem->destroy(skyViewLutDS);
	graphicsSystem->destroy(cameraVolumeDS);
	graphicsSystem->destroy(multiScatLutDS);
	graphicsSystem->destroy(skyboxPipeline);

	multiScatLutDS = cameraVolumeDS = skyViewLutDS = 
		hdrSkyDS = skyboxDS = {}; skyboxPipeline = {};

	if (transLUT)
	{
		auto transLutView = graphicsSystem->get(transLUT);
		if (transLutView->getFormat() != getTransLutFormat(quality))
		{
			graphicsSystem->destroy(transLUT);
			transLUT = createTransLUT(graphicsSystem, getTransLutFormat(quality));

			if (transLutFramebuffer)
			{
				// Note: destroying because it may be already recorder.
				graphicsSystem->destroy(transLutFramebuffer);
				transLutFramebuffer = createScatLutFramebuffer(graphicsSystem, transLUT, "transLUT");
			}
		}
	}

	if (transLutPipeline)
	{
		graphicsSystem->destroy(transLutPipeline);
		transLutPipeline = createTransLutPipeline(transLutFramebuffer, quality);
	}
	if (multiScatLutPipeline)
	{
		graphicsSystem->destroy(multiScatLutPipeline);
		multiScatLutPipeline = createMultiScatLutPipeline(quality);
	}
	if (cameraVolumePipeline)
	{
		graphicsSystem->destroy(cameraVolumePipeline);
		cameraVolumePipeline = createCameraVolumePipeline(quality);
	}
	if (skyViewLutPipeline)
	{
		graphicsSystem->destroy(skyViewLutPipeline);
		skyViewLutPipeline = createSkyViewLutPipeline(skyViewLutFramebuffer, quality);
	}
	if (hdrSkyPipeline)
	{
		graphicsSystem->destroy(hdrSkyPipeline);
		hdrSkyPipeline = createHdrSkyPipeline(quality);
	}

	this->quality = quality;
}

//**********************************************************************************************************************
ID<Image> AtmosphereRenderSystem::getTransLUT()
{
	if (!transLUT)
		transLUT = createTransLUT(GraphicsSystem::Instance::get(), getTransLutFormat(quality));
	return transLUT;
}
ID<Image> AtmosphereRenderSystem::getMultiScatLUT()
{
	if (!multiScatLUT)
		multiScatLUT = createMultiScatLUT(GraphicsSystem::Instance::get());
	return multiScatLUT;
}
ID<Image> AtmosphereRenderSystem::getCameraVolume()
{
	if (!cameraVolume)
		cameraVolume = createCameraVolume(GraphicsSystem::Instance::get());
	return cameraVolume;
}
ID<Image> AtmosphereRenderSystem::getSkyViewLUT()
{
	if (!skyViewLUT)
		skyViewLUT = createSkyViewLUT(GraphicsSystem::Instance::get());
	return skyViewLUT;
}

void AtmosphereRenderSystem::getSliceQuality(GraphicsQuality quality, float& sliceCount, float& kmPerSlice) noexcept
{
	switch (quality)
	{
		case GraphicsQuality::PotatoPC: sliceCount = 8.0f; kmPerSlice = 12.0f; break;
		case GraphicsQuality::Ultra: sliceCount = 32.0f; kmPerSlice = 3.0f; break;
		default: sliceCount = 16.0f; kmPerSlice = 6.0f; break;
	}
}