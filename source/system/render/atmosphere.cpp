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
#include "garden/system/resource.hpp"

using namespace garden;

static ID<Image> createTransLUT()
{
	auto transLUT = GraphicsSystem::Instance::get()->createImage(Image::Format::SfloatR16G16B16A16, 
		Image::Usage::Sampled | Image::Usage::ColorAttachment, { { nullptr } }, 
		AtmosphereRenderSystem::transLutSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(transLUT, "image.atmosphere.transLUT");
	return transLUT;
}
static ID<Image> createMultiScatLUT()
{
	auto multiScatLUT = GraphicsSystem::Instance::get()->createImage(Image::Format::SfloatR16G16B16A16,
		Image::Usage::Sampled | Image::Usage::Storage, { { nullptr } },
		uint2(AtmosphereRenderSystem::multiScatLutLength), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(multiScatLUT, "image.atmosphere.multiScatLUT");
	return multiScatLUT;
}
static ID<Image> createCameraVolume()
{
	auto cameraVolume = GraphicsSystem::Instance::get()->createImage(Image::Format::SfloatR16G16B16A16,
		Image::Usage::Sampled | Image::Usage::Storage, { { nullptr } },
		uint3(AtmosphereRenderSystem::cameraVolumeLength), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(cameraVolume, "image.atmosphere.cameraVolume");
	return cameraVolume;
}
static ID<Image> createSkyViewLUT()
{
	auto size = uint2(192, 108); // TODO: framebufferSize / 10.
	auto skyViewLUT = GraphicsSystem::Instance::get()->createImage(Image::Format::UfloatB10G11R11,
		Image::Usage::Sampled | Image::Usage::ColorAttachment, { { nullptr } }, size, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(skyViewLUT, "image.atmosphere.skyViewLUT");
	return skyViewLUT;
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

static void createSkyboxFramebuffers(ID<Framebuffer>* framebuffers)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	for (uint8 i = 0; i < 6; i++)
	{
		vector<Framebuffer::OutputAttachment> colorAttachments =
		{ Framebuffer::OutputAttachment({}, {false, false, true}) };
		auto framebuffer = graphicsSystem->createFramebuffer(uint2(16), std::move(colorAttachments));
		SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.atmosphere.skybox" + to_string(i));
		framebuffers[i] = framebuffer;
	}
}
static void destroySkyboxFramebuffers(ID<Framebuffer>* framebuffers)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	for (uint8 i = 0; i < 6; i++)
		graphicsSystem->destroy(framebuffers[i]);
}

//**********************************************************************************************************************
static ID<GraphicsPipeline> createTransLutPipeline(ID<Framebuffer> transLutFramebuffer)
{
	ResourceSystem::GraphicsOptions options;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline(
		"atmosphere/transmittance", transLutFramebuffer, options);
}
static ID<ComputePipeline> createMultiScatLutPipeline()
{
	ResourceSystem::ComputeOptions options;
	return ResourceSystem::Instance::get()->loadComputePipeline("atmosphere/multi-scattering", options);
}
static ID<ComputePipeline> createCameraVolumePipeline()
{
	ResourceSystem::ComputeOptions options;
	return ResourceSystem::Instance::get()->loadComputePipeline("atmosphere/camera-volume", options);
}
static ID<GraphicsPipeline> createSkyViewLutPipeline(ID<Framebuffer> skyViewFramebuffer)
{
	ResourceSystem::GraphicsOptions options;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline(
		"atmosphere/sky-view", skyViewFramebuffer, options);
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
}
void AtmosphereRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(skyViewLutDS);
		graphicsSystem->destroy(cameraVolumeDS);
		graphicsSystem->destroy(multiScatLutDS);
		graphicsSystem->destroy(skyViewLutPipeline);
		graphicsSystem->destroy(cameraVolumePipeline);
		graphicsSystem->destroy(multiScatLutPipeline);
		graphicsSystem->destroy(transLutPipeline);
		destroySkyboxFramebuffers(skyboxFramebuffers);
		graphicsSystem->destroy(skyViewLutFramebuffer);
		graphicsSystem->destroy(transLutFramebuffer);
		graphicsSystem->destroy(skyViewLUT);
		graphicsSystem->destroy(cameraVolume);
		graphicsSystem->destroy(multiScatLUT);
		graphicsSystem->destroy(transLUT);

		ECSM_UNSUBSCRIBE_FROM_EVENT("PreDeferredRender", AtmosphereRenderSystem::preDeferredRender);
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
			transLUT = createTransLUT();
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
			transLutPipeline = createTransLutPipeline(transLutFramebuffer);
		if (!multiScatLutPipeline)
			multiScatLutPipeline = createMultiScatLutPipeline();
		if (!cameraVolumePipeline)
			cameraVolumePipeline = createCameraVolumePipeline();
		if (!skyViewLutPipeline)
			skyViewLutPipeline = createSkyViewLutPipeline(skyViewLutFramebuffer);
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
	auto cameraPos = (float3)fma(cc.cameraPos, f32x4(0.001f), f32x4(0.0f, groundRadius, 0.0f));
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
		pc.multiScatFactor = multiScatFactor;

		SET_GPU_DEBUG_LABEL("Multi Scat LUT", Color::transparent);
		multiScatLutPipelineView->bind();
		multiScatLutPipelineView->bindDescriptorSet(multiScatLutDS);
		multiScatLutPipelineView->pushConstants(&pc);
		multiScatLutPipelineView->dispatch(uint2(multiScatLutLength), false);
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
		cameraVolumePipelineView->dispatch(uint3(cameraVolumeLength));
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
	if (pbrLightingView)
	{
		if (pbrLightingView->cubemap)
		{
			// TODO:
		}
	}

	graphicsSystem->startRecording(CommandBufferType::Frame);
	END_GPU_DEBUG_LABEL();
	graphicsSystem->stopRecording();
}

//**********************************************************************************************************************
void AtmosphereRenderSystem::depthHdrRender()
{
	if (!isEnabled)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	
}

ID<Image> AtmosphereRenderSystem::getTransLUT()
{
	if (!transLUT)
		transLUT = createTransLUT();
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