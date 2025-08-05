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

#include "garden/system/render/hbao.hpp"
#include "garden/system/render/pbr-lighting.hpp"
#include "garden/system/render/hiz.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/settings.hpp"
#include "garden/system/camera.hpp"
#include "garden/profiler.hpp"
#include "ssao/defines.h"

#include <random>

using namespace garden;

static ID<Image> createNoiseImage()
{
	std::mt19937 rmt;
	float4 hbaoNoise[SSAO_NOISE_SIZE * SSAO_NOISE_SIZE];

	for (uint32 i = 0; i < SSAO_NOISE_SIZE * SSAO_NOISE_SIZE; i++)
	{
		float rand1 = static_cast<float>(rmt()) / 4294967296.0f;
		float rand2 = static_cast<float>(rmt()) / 4294967296.0f;
		float angle = (M_PI * 2.0 * rand1) / SSAO_DIRECTION_COUNT;
		hbaoNoise[i] = float4(cosf(angle), sinf(angle), rand2, 0.0f);
	}

	auto noiseImage = GraphicsSystem::Instance::get()->createImage(Image::Format::SfloatR16G16B16A16,
		Image::Usage::Sampled | Image::Usage::TransferDst, { { hbaoNoise } },
		uint2(SSAO_NOISE_SIZE), Image::Strategy::Size, Image::Format::SfloatR32G32B32A32);
	SET_RESOURCE_DEBUG_NAME(noiseImage, "image.hbao.noise");
	return noiseImage;
}

//**********************************************************************************************************************
static ID<GraphicsPipeline> createPipeline(uint32 stepCount)
{
	auto pbrLightingSystem = PbrLightingSystem::Instance::get();
	GARDEN_ASSERT(pbrLightingSystem->getOptions().useAoBuffer);

	Pipeline::SpecConstValues specConsts = { { "STEP_COUNT", Pipeline::SpecConstValue(stepCount) } };
	ResourceSystem::GraphicsOptions options;
	options.specConstValues = &specConsts;

	return ResourceSystem::Instance::get()->loadGraphicsPipeline(
		"hbao", pbrLightingSystem->getAoBaseFB(), options);
}
static DescriptorSet::Uniforms getUniforms(ID<Image> noiseImage)
{
	auto hizSystem = HizRenderSystem::Instance::get();
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto hizBufferView = hizSystem->getImageViews()[1];
	auto noiseView = graphicsSystem->get(noiseImage)->getDefaultView();

	DescriptorSet::Uniforms uniforms =
	{ 
		{ "hizBuffer", DescriptorSet::Uniform(hizBufferView) },
		{ "noise", DescriptorSet::Uniform(noiseView) },
	};
	return uniforms;
}

//**********************************************************************************************************************
HbaoRenderSystem::HbaoRenderSystem(bool setSingleton) : Singleton(setSingleton)
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", HbaoRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", HbaoRenderSystem::deinit);
}
HbaoRenderSystem::~HbaoRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", HbaoRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", HbaoRenderSystem::deinit);
	}

	unsetSingleton();
}

void HbaoRenderSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("PreAoRender", HbaoRenderSystem::preAoRender);
	ECSM_SUBSCRIBE_TO_EVENT("AoRender", HbaoRenderSystem::aoRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", HbaoRenderSystem::gBufferRecreate);

	auto settingsSystem = SettingsSystem::Instance::tryGet();
	if (settingsSystem)
		settingsSystem->getBool("hbao.isEnabled", isEnabled);

	auto pbrLightingSystem = PbrLightingSystem::Instance::get();
	if (isEnabled && pbrLightingSystem->getOptions().useAoBuffer)
	{
		if (!noiseImage)
			noiseImage = createNoiseImage();
		if (!pipeline)
			pipeline = createPipeline(stepCount);
		isInitialized = true;
	}
}
void HbaoRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(descriptorSet);
		graphicsSystem->destroy(pipeline);
		graphicsSystem->destroy(noiseImage);

		ECSM_UNSUBSCRIBE_FROM_EVENT("PreAoRender", HbaoRenderSystem::preAoRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("AoRender", HbaoRenderSystem::aoRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", HbaoRenderSystem::gBufferRecreate);
	}
}

//**********************************************************************************************************************
void HbaoRenderSystem::preAoRender()
{
	SET_CPU_ZONE_SCOPED("HBAO Pre AO Render");

	if (!isEnabled)
		return;

	if (!isInitialized)
	{
		if (!noiseImage)
			noiseImage = createNoiseImage();
		if (!pipeline)
			pipeline = createPipeline(stepCount);
		isInitialized = true;
	}
}
void HbaoRenderSystem::aoRender()
{
	SET_CPU_ZONE_SCOPED("HBAO AO Render");

	if (!isEnabled || intensity <= 0.0f)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(pipeline);
	auto noiseImageView = graphicsSystem->get(noiseImage);
	if (!pipelineView->isReady() || !noiseImageView->isReady())
		return;

	if (!descriptorSet)
	{
		auto uniforms = getUniforms(noiseImage);
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.hbao");
	}

	auto cameraView = CameraSystem::Instance::get()->getComponent(graphicsSystem->camera);
	auto framebufferView = graphicsSystem->get(graphicsSystem->getCurrentFramebuffer());
	auto aoFrameSize = framebufferView->getSize();
	auto& cameraConstants = graphicsSystem->getCameraConstants();
	auto& proj = cameraConstants.projection;

	PushConstants pc; float projScale;
	if (cameraView->type == ProjectionType::Perspective)
	{
		pc.projInfo = float4
		(
			2.0f / (proj.c0.getX()),
			2.0f / (proj.c1.getY()),
			-(1.0f - proj.c2.getX()) / proj.c0.getX(),
			-(1.0f + proj.c2.getY()) / proj.c1.getY()
		);

		projScale = (float)aoFrameSize.y / (std::tan(
			cameraView->p.perspective.fieldOfView * 0.5f) * 2.0f);
		pc.projOrtho = 0;
	}
	else
	{
		// TODO: check if ortho projInfo is correct.
		// If not, problem is in difference between GLM and our ortho.
		abort(); // TODO: support othrographic depth linearization.

		pc.projInfo = float4
		(
			2.0f / (proj.c0.getX()),
			2.0f / (proj.c1.getY()),
			-(1.0f + proj.c3.getX()) / proj.c0.getX(),
			-(1.0f - proj.c3.getY()) / proj.c1.getY()
		);

		projScale = (float)aoFrameSize.y / pc.projInfo[1];
		pc.projOrtho = 1;
	}

	pc.invFullRes = float2(1.0f) / aoFrameSize;
	pc.negInvR2 = -1.0f / (radius * radius);
	pc.radiusToScreen = radius * 0.5f * projScale;
	pc.powExponent = intensity;
	pc.novBias = clamp(bias, 0.0f, 1.0f);
	pc.aoMultiplier = 1.0f / (1.0f - pc.novBias);
	pc.nearPlane = cameraConstants.nearPlane;

	SET_GPU_DEBUG_LABEL("HBAO", Color::transparent);
	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->bindDescriptorSet(descriptorSet);
	pipelineView->pushConstants(&pc);
	pipelineView->drawFullscreen();

	PbrLightingSystem::Instance::get()->markAnyAO();
}

void HbaoRenderSystem::gBufferRecreate()
{
	if (descriptorSet)
	{
		GraphicsSystem::Instance::get()->destroy(descriptorSet);
		descriptorSet = {};
	}
}

//**********************************************************************************************************************
void HbaoRenderSystem::setConsts(uint32 stepCount)
{
	if (this->stepCount == stepCount)
		return;

	this->stepCount = stepCount;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	graphicsSystem->destroy(descriptorSet);
	descriptorSet = {};

	if (pipeline)
	{
		graphicsSystem->destroy(pipeline);
		pipeline = createPipeline(stepCount);
	}
}

ID<Image> HbaoRenderSystem::getNoiseImage()
{
	if (!noiseImage)
		noiseImage = createNoiseImage();
	return noiseImage;
}
ID<GraphicsPipeline> HbaoRenderSystem::getPipeline()
{
	if (!pipeline)
		pipeline = createPipeline(stepCount);
	return pipeline;
}