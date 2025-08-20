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
static ID<Image> createSkyViewLUT()
{
	auto size = uint2(192, 108); // TODO: framebufferSize / 10.
	auto skyViewLUT = GraphicsSystem::Instance::get()->createImage(Image::Format::UfloatB10G11R11,
		Image::Usage::Sampled | Image::Usage::ColorAttachment, { { nullptr } }, size, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(skyViewLUT, "image.atmosphere.skyViewLUT");
	return skyViewLUT;
}

//**********************************************************************************************************************
static ID<Framebuffer> createScatterFramebuffer(ID<Image> lut, const char* debugName)
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

static ID<GraphicsPipeline> createTransLutPipeline(ID<Framebuffer> transLutFramebuffer)
{
	ResourceSystem::GraphicsOptions options;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline(
		"atmosphere/transmittance", transLutFramebuffer, options);
}
static ID<ComputePipeline> createMultiScatPipeline()
{
	ResourceSystem::ComputeOptions options;
	return ResourceSystem::Instance::get()->loadComputePipeline("atmosphere/multi-scattering", options);
}
static ID<GraphicsPipeline> createSkyViewPipeline(ID<Framebuffer> skyViewFramebuffer)
{
	ResourceSystem::GraphicsOptions options;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline(
		"atmosphere/sky-view", skyViewFramebuffer, options);
}

static DescriptorSet::Uniforms getScatterUniforms(ID<Image> transLUT, ID<Image> multiScatLUT)
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
	ECSM_SUBSCRIBE_TO_EVENT("Render", AtmosphereRenderSystem::render);

	auto earthAirMolarMass = calcEarthAirMolarMass();
	auto molecularDensity = calcMolecularDensity(earthAirDensity, earthAirMolarMass);
	auto test = calcRayleighScattering(earthAirIOR, molecularDensity) * 1e6f;
	test += test; // TODO: remove
}
void AtmosphereRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(skyViewDS);
		graphicsSystem->destroy(multiScatDS);
		graphicsSystem->destroy(skyViewPipeline);
		graphicsSystem->destroy(multiScatPipeline);
		graphicsSystem->destroy(transLutPipeline);
		graphicsSystem->destroy(skyViewFramebuffer);
		graphicsSystem->destroy(transLutFramebuffer);
		graphicsSystem->destroy(skyViewLUT);
		graphicsSystem->destroy(multiScatLUT);
		graphicsSystem->destroy(transLUT);

		ECSM_UNSUBSCRIBE_FROM_EVENT("Render", AtmosphereRenderSystem::render);
	}
}

//**********************************************************************************************************************
void AtmosphereRenderSystem::render()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!isEnabled || !graphicsSystem->canRender())
		return;

	if (!isInitialized)
	{
		if (!transLUT)
			transLUT = createTransLUT();
		if (!multiScatLUT)
			multiScatLUT = createMultiScatLUT();
		if (!skyViewLUT)
			skyViewLUT = createSkyViewLUT();
		if (!transLutFramebuffer)
			transLutFramebuffer = createScatterFramebuffer(transLUT, "transLUT");
		if (!skyViewFramebuffer)
			skyViewFramebuffer = createScatterFramebuffer(skyViewLUT, "skyViewLUT");
		if (!transLutPipeline)
			transLutPipeline = createTransLutPipeline(transLutFramebuffer);
		if (!multiScatPipeline)
			multiScatPipeline = createMultiScatPipeline();
		if (!skyViewPipeline)
			skyViewPipeline = createSkyViewPipeline(skyViewFramebuffer);
		isInitialized = true;
	}

	auto transLutPipelineView = graphicsSystem->get(transLutPipeline);
	auto multiScatPipelineView = graphicsSystem->get(multiScatPipeline);
	auto skyViewPipelineView = graphicsSystem->get(skyViewPipeline);
	if (!transLutPipelineView->isReady() || !multiScatPipelineView->isReady() || !skyViewPipelineView->isReady())
		return;

	if (!multiScatDS)
	{
		auto uniforms = getScatterUniforms(transLUT, multiScatLUT);
		multiScatDS = graphicsSystem->createDescriptorSet(multiScatPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(multiScatDS, "descriptorSet.atmosphere.multiScatLUT");
	}
	if (!skyViewDS)
	{
		auto uniforms = getScatterUniforms(transLUT, multiScatLUT);
		skyViewDS = graphicsSystem->createDescriptorSet(skyViewPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(skyViewDS, "descriptorSet.atmosphere.skyViewLUT");
	}

	const auto& cc = graphicsSystem->getCommonConstants();
	graphicsSystem->startRecording(CommandBufferType::Frame);
	BEGIN_GPU_DEBUG_LABEL("Atmosphere", Color::transparent);
	graphicsSystem->stopRecording();

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		auto framebufferView = graphicsSystem->get(transLutFramebuffer);
		TransmittancePC pc; // TODO: get from system vars.
		pc.rayleighScattering = float3(0.0058, 0.01356, 0.0331);
		pc.rayDensityExpScale = -0.125f;
		pc.mieExtinction = float3(0.00443999982f);
		pc.mieDensityExpScale = -0.833333313f;
		pc.absorptionExtinction = float3(0.000650000002f, 0.00188100000f, 8.49999997e-05f);
		pc.miePhaseG = 0.8f;
		pc.sunDir = float3(0.0f, 0.434965521, 0.900447130);
		pc.absDensity0LayerWidth = 25.0f;
		pc.absDensity0ConstantTerm = -0.666666687f;
		pc.absDensity0LinearTerm = 0.0666666701f;
		pc.absDensity1ConstantTerm = 2.66666675f;
		pc.absDensity1LinearTerm = -0.0666666701f;
		pc.bottomRadius = 6360.0f;
		pc.topRadius = 6460.0f;

		//Parameters.RayleighDensityExpScale = rayleigh_density[1].w;
		//Parameters.MieDensityExpScale = mie_density[1].w;
		//Parameters.AbsorptionDensity0LayerWidth = absorption_density[0].x;
		//Parameters.AbsorptionDensity0ConstantTerm = absorption_density[1].x;
		//Parameters.AbsorptionDensity0LinearTerm = absorption_density[0].w;
		//Parameters.AbsorptionDensity1ConstantTerm = absorption_density[2].y;
		//Parameters.AbsorptionDensity1LinearTerm = absorption_density[2].x;

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
		pc.rayleighScattering = float3(0.0058f, 0.01356f, 0.0331f);
		pc.rayDensityExpScale = -0.125f;
		pc.mieExtinction = float3(0.00443999982f);
		pc.mieDensityExpScale = -0.833333313f;
		pc.absorptionExtinction = float3(0.000650000002f, 0.00188100000f, 8.49999997e-05f);
		pc.miePhaseG = 0.8f;
		pc.mieScattering = float3(0.004f, 0.004f, 0.004f);
		pc.absDensity0LayerWidth = 25.0f;
		pc.groundAlbedo = float3(0.0f);
		pc.absDensity0ConstantTerm = -0.666666687f;
		pc.absDensity0LinearTerm = 0.0666666701f;
		pc.absDensity1ConstantTerm = 2.66666675f;
		pc.absDensity1LinearTerm = -0.0666666701f;
		pc.bottomRadius = 6360.0f;
		pc.topRadius = 6460.0f;
		pc.multiScatFactor = 1.0f;

		SET_GPU_DEBUG_LABEL("Multi Scat LUT", Color::transparent);
		multiScatPipelineView->bind();
		multiScatPipelineView->bindDescriptorSet(multiScatDS);
		multiScatPipelineView->pushConstants(&pc);
		multiScatPipelineView->dispatch(uint2(multiScatLutLength), false);
	}
	{
		auto framebufferView = graphicsSystem->get(skyViewFramebuffer);
		SkyViewPC pc;
		pc.rayleighScattering = float3(0.0058f, 0.01356f, 0.0331f);
		pc.rayDensityExpScale = -0.125f;
		pc.mieExtinction = float3(0.00443999982f);
		pc.mieDensityExpScale = -0.833333313f;
		pc.absorptionExtinction = float3(0.000650000002f, 0.00188100000f, 8.49999997e-05f);
		pc.miePhaseG = 0.8f;
		pc.mieScattering = float3(0.004f, 0.004f, 0.004f);
		pc.absDensity0LayerWidth = 25.0f;
		pc.groundAlbedo = float3(0.0f);
		pc.absDensity0ConstantTerm = -0.666666687f;
		pc.sunDir = float3(0.0f, 0.434965521f, 0.900447130f);
		pc.absDensity0LinearTerm = 0.0666666701f;
		pc.cameraPos = (float3)(cc.cameraPos + f32x4(0.0f, 6360.0f, 0.0f));
		//pc.skyViewLutSize = framebufferView->getSize();
		pc.absDensity1ConstantTerm = 2.66666675f;
		pc.absDensity1LinearTerm = -0.0666666701f;
		pc.bottomRadius = 6360.0f;
		pc.topRadius = 6460.0f;

		SET_GPU_DEBUG_LABEL("Sky View LUT", Color::transparent);
		framebufferView->beginRenderPass(float4::zero);
		skyViewPipelineView->bind();
		skyViewPipelineView->setViewportScissor();
		skyViewPipelineView->bindDescriptorSet(skyViewDS);
		skyViewPipelineView->pushConstants(&pc);
		skyViewPipelineView->drawFullscreen();
		framebufferView->endRenderPass();
	}
	graphicsSystem->stopRecording();

	graphicsSystem->startRecording(CommandBufferType::Frame);
	END_GPU_DEBUG_LABEL();
	graphicsSystem->stopRecording();
}