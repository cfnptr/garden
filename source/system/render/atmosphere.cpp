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
static ID<Framebuffer> createTransLutFramebuffer(ID<Image> transLUT)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto transLutView = graphicsSystem->get(transLUT)->getDefaultView();
	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(transLutView, { false, false, true }) };
	auto framebuffer = graphicsSystem->createFramebuffer(
		AtmosphereRenderSystem::transLutSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.atmosphere.transLUT");
	return framebuffer;
}

static ID<GraphicsPipeline> createTransLutPipeline(ID<Framebuffer> transLutFramebuffer)
{
	ResourceSystem::GraphicsOptions options;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline(
		"atmosphere/transmittance", transLutFramebuffer, options);
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
		graphicsSystem->destroy(transLutPipeline);
		graphicsSystem->destroy(transLutFramebuffer);
		graphicsSystem->destroy(transLUT);

		ECSM_UNSUBSCRIBE_FROM_EVENT("Render", AtmosphereRenderSystem::render);
	}
}

void AtmosphereRenderSystem::render()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!isEnabled || !graphicsSystem->canRender())
		return;

	if (!isInitialized)
	{
		if (!transLUT)
			transLUT = createTransLUT();
		if (!transLutFramebuffer)
			transLutFramebuffer = createTransLutFramebuffer(transLUT);
		if (!transLutPipeline)
			transLutPipeline = createTransLutPipeline(transLutFramebuffer);
		isInitialized = true;
	}

	auto transLutPipelineView = graphicsSystem->get(transLutPipeline);
	if (!transLutPipelineView->isReady())
		return;

	graphicsSystem->startRecording(CommandBufferType::Frame);
	BEGIN_GPU_DEBUG_LABEL("Atmosphere", Color::transparent);
	graphicsSystem->stopRecording();

	auto framebufferView = graphicsSystem->get(transLutFramebuffer);


	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		TransmittancePC pc; // TODO: get from system vars.
		pc.rayleighScattering = float3(0.00164437352f, 0.00384254009f, 0.00938103534f);
		pc.rayDensityExpScale = -0.125f;
		pc.mieExtinction = float3(0.00443999982f);
		pc.mieDensityExpScale = -0.833333313f;
		pc.absorptionExtinction = float3(0.000650000002f, 0.00188100000f, 8.49999997e-05f);
		pc.miePhaseG = 0.800000012f;
		pc.sunDir = float3(0.0f, 0.900447130, 0.434965521);
		pc.absDensity0LayerWidth = 25.0000000f;
		pc.absDensity0ConstantTerm = -0.666666687f;
		pc.absDensity0LinearTerm = 0.0666666701f;
		pc.absDensity1ConstantTerm = 2.66666675f;
		pc.absDensity1LinearTerm = -0.0666666701f;
		pc.bottomRadius = 6360.00000f;
		pc.topRadius = 6460.00000f;

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
	graphicsSystem->stopRecording();

	graphicsSystem->startRecording(CommandBufferType::Frame);
	END_GPU_DEBUG_LABEL();
	graphicsSystem->stopRecording();
}