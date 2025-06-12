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

#include "garden/system/render/ssao.hpp"
#include "garden/system/render/pbr-lighting.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/settings.hpp"
#include "garden/profiler.hpp"

#include <random>

#define NOISE_SIZE 4
using namespace garden;

static ID<Buffer> createSampleBuffer(uint32 sampleCount)
{
	uniform_real_distribution<float> randomFloats(0.0f, 1.0f);
	default_random_engine generator;
	vector<f32x4> ssaoKernel(sampleCount);

	for (uint32 i = 0; i < sampleCount; i++)
	{
		auto sample = normalize3(f32x4(randomFloats(generator) * 2.0f - 1.0f,
			randomFloats(generator) * 2.0f - 1.0f, randomFloats(generator) * 2.0f - 1.0f));
		auto scale = (float)i / sampleCount;
		scale = lerp(0.1f, 1.0f, scale * scale);
		ssaoKernel[i] = f32x4(sample * scale * randomFloats(generator), 0.0f);
	}

	auto buffer = GraphicsSystem::Instance::get()->createBuffer(Buffer::Usage::Uniform | 
		Buffer::Usage::TransferDst | Buffer::Usage::TransferQ, Buffer::CpuAccess::None, 
		ssaoKernel, 0, 0, Buffer::Location::PreferGPU, Buffer::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(buffer, "buffer.uniform.ssao.sample");
	return buffer;
}
static ID<Image> createNoiseTexture()
{
	uniform_real_distribution<float> randomFloats(0.0f, 1.0f);
	default_random_engine generator;
	vector<f32x4> ssaoNoise(NOISE_SIZE * NOISE_SIZE);

	for (uint32 i = 0; i < NOISE_SIZE * NOISE_SIZE; i++)
	{
		auto noise = f32x4(randomFloats(generator) * 2.0f - 1.0f,
			randomFloats(generator) * 2.0f - 1.0f, 0.0f);
		ssaoNoise[i] = f32x4(noise, 0.0f);
	}

	auto texture = GraphicsSystem::Instance::get()->createImage(Image::Format::SfloatR16G16B16A16,
		Image::Usage::Sampled | Image::Usage::TransferDst, { { ssaoNoise.data() } },
		uint2(NOISE_SIZE), Image::Strategy::Size, Image::Format::SfloatR32G32B32A32);
	SET_RESOURCE_DEBUG_NAME(texture, "image.ssao.random");
	return texture;
}

//**********************************************************************************************************************
static ID<GraphicsPipeline> createPipeline(uint32 sampleCount)
{
	Pipeline::SpecConstValues specConsts = { { "SAMPLE_COUNT", Pipeline::SpecConstValue(sampleCount) } };
	auto pbrLightingSystem = PbrLightingRenderSystem::Instance::get();
	GARDEN_ASSERT(pbrLightingSystem->useAoBuffer());
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("ssao",
		pbrLightingSystem->getAoBaseFB(), false, true, 0, 0, &specConsts);
}
static DescriptorSet::Uniforms getUniforms(ID<Buffer> sampleBuffer, ID<Image> noiseTexture)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto gFramebufferView = graphicsSystem->get(deferredSystem->getGFramebuffer());
	auto inFlightCount = graphicsSystem->getInFlightCount();

	DescriptorSet::Uniforms uniforms =
	{ 
		{ "gBufferNormals", DescriptorSet::Uniform(gFramebufferView->getColorAttachments()[
			DeferredRenderSystem::normalsGBuffer].imageView, 1, inFlightCount) },
		{ "depthBuffer", DescriptorSet::Uniform(
			gFramebufferView->getDepthStencilAttachment().imageView, 1, inFlightCount) },
		{ "samples", DescriptorSet::Uniform(sampleBuffer, 1, inFlightCount) },
		{ "noise", DescriptorSet::Uniform(graphicsSystem->get(noiseTexture)->getDefaultView(), 1, inFlightCount) },
		{ "cc", DescriptorSet::Uniform(graphicsSystem->getCameraConstantsBuffers()) }
	};

	return uniforms;
}

//**********************************************************************************************************************
SsaoRenderSystem::SsaoRenderSystem(bool setSingleton) : Singleton(setSingleton)
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", SsaoRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", SsaoRenderSystem::deinit);
}
SsaoRenderSystem::~SsaoRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", SsaoRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", SsaoRenderSystem::deinit);
	}

	unsetSingleton();
}

void SsaoRenderSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("PreAoRender", SsaoRenderSystem::preAoRender);
	ECSM_SUBSCRIBE_TO_EVENT("AoRender", SsaoRenderSystem::aoRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", SsaoRenderSystem::gBufferRecreate);

	auto settingsSystem = SettingsSystem::Instance::tryGet();
	if (settingsSystem)
		settingsSystem->getBool("ssao.isEnabled", isEnabled);

	auto pbrLightingSystem = PbrLightingRenderSystem::Instance::get();
	if (isEnabled && pbrLightingSystem->useAoBuffer())
	{
		if (!sampleBuffer)
			sampleBuffer = createSampleBuffer(sampleCount);
		if (!noiseTexture)
			noiseTexture = createNoiseTexture();
		if (!pipeline)
			pipeline = createPipeline(sampleCount);
		isInitialized = true;
	}
}
void SsaoRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(descriptorSet);
		graphicsSystem->destroy(pipeline);
		graphicsSystem->destroy(noiseTexture);
		graphicsSystem->destroy(sampleBuffer);

		ECSM_UNSUBSCRIBE_FROM_EVENT("PreAoRender", SsaoRenderSystem::preAoRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("AoRender", SsaoRenderSystem::aoRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", SsaoRenderSystem::gBufferRecreate);
	}
}

//**********************************************************************************************************************
void SsaoRenderSystem::preAoRender()
{
	SET_CPU_ZONE_SCOPED("SSAO Pre AO Render");

	if (!isEnabled)
		return;

	if (!isInitialized)
	{
		if (!sampleBuffer)
			sampleBuffer = createSampleBuffer(sampleCount);
		if (!noiseTexture)
			noiseTexture = createNoiseTexture();
		if (!pipeline)
			pipeline = createPipeline(sampleCount);
		isInitialized = true;
	}
}
void SsaoRenderSystem::aoRender()
{
	SET_CPU_ZONE_SCOPED("SSAO AO Render");

	if (!isEnabled || intensity <= 0.0f)
		return;
	
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(pipeline);
	auto sampleBufferView = graphicsSystem->get(sampleBuffer);
	auto noiseTextureView = graphicsSystem->get(noiseTexture);

	if (!pipelineView->isReady() || !sampleBufferView->isReady() || !noiseTextureView->isReady())
		return;

	if (!descriptorSet)
	{
		auto uniforms = getUniforms(sampleBuffer, noiseTexture);
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.ssao");
	}

	static const auto uvToNDC = f32x4x4
	(
		2.0f, 0.0f, 0.0f, -1.0f,
		0.0f, 2.0f, 0.0f, -1.0f,
		0.0f, 0.0f, 1.0f,  0.0f,
		0.0f, 0.0f, 0.0f,  1.0f
	);
	static const auto ndcToUV = f32x4x4
	(
		0.5f, 0.0f, 0.0f, 0.5f,
		0.0f, 0.5f, 0.0f, 0.5f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	);

	auto inFlightIndex = graphicsSystem->getInFlightIndex();
	const auto& cameraConstants = graphicsSystem->getCameraConstants();

	PushConstants pc;
	pc.uvToView = (float4x4)(cameraConstants.inverseProj * uvToNDC);
	pc.uvToView[0][3] = radius;
	pc.uvToView[1][3] = -bias;
	pc.uvToView[3][3] = intensity;
	pc.viewToUv = (float4x4)(ndcToUV * cameraConstants.projection);

	SET_GPU_DEBUG_LABEL("SSAO", Color::transparent);
	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->bindDescriptorSet(descriptorSet, inFlightIndex);
	pipelineView->pushConstants(&pc);
	pipelineView->drawFullscreen();

	PbrLightingRenderSystem::Instance::get()->markAnyAO();
}

void SsaoRenderSystem::gBufferRecreate()
{
	if (descriptorSet)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(descriptorSet);
		auto uniforms = getUniforms(sampleBuffer, noiseTexture);
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.ssao");
	}
}

//**********************************************************************************************************************
void SsaoRenderSystem::setConsts(uint32 sampleCount)
{
	if (this->sampleCount == sampleCount)
		return;

	this->sampleCount = sampleCount;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	graphicsSystem->destroy(descriptorSet);
	descriptorSet = {};

	if (sampleBuffer)
	{
		graphicsSystem->destroy(sampleBuffer);
		sampleBuffer = createSampleBuffer(sampleCount);
	}
	if (pipeline)
	{
		graphicsSystem->destroy(pipeline);
		pipeline = createPipeline(sampleCount);
	}
}

ID<Buffer> SsaoRenderSystem::getSampleBuffer()
{
	if (!sampleBuffer)
		sampleBuffer = createSampleBuffer(sampleCount);
	return sampleBuffer;
}
ID<Image> SsaoRenderSystem::getNoiseTexture()
{
	if (!noiseTexture)
		noiseTexture = createNoiseTexture();
	return noiseTexture;
}
ID<GraphicsPipeline> SsaoRenderSystem::getPipeline()
{
	if (!pipeline)
		pipeline = createPipeline(sampleCount);
	return pipeline;
}