//--------------------------------------------------------------------------------------------------
// Copyright 2022-2024 Nikita Fediuchin. All rights reserved.
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
//--------------------------------------------------------------------------------------------------

/*
#include "garden/system/render/ssao.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/settings.hpp"
// TODO: garden/profiler.hpp

#include <random>

#if GARDEN_EDITOR
#include "garden/editor/system/render/ssao.hpp"
#endif

#define NOISE_SIZE 4

using namespace garden;

//--------------------------------------------------------------------------------------------------
static ID<Buffer> createSampleBuffer(uint32 sampleCount)
{
	uniform_real_distribution<float> randomFloats(0.0f, 1.0f);
	default_random_engine generator;
	vector<float4> ssaoKernel(sampleCount);

	for (uint32 i = 0; i < sampleCount; i++)
	{
		auto sample = normalize(float3(
			randomFloats(generator) * 2.0f - 1.0f,
			randomFloats(generator) * 2.0f - 1.0f,
			randomFloats(generator) * 2.0f - 1.0f));
		auto scale = (float)i / sampleCount;
		scale = lerp(0.1f, 1.0f, scale * scale);
		ssaoKernel[i] = float4(sample * scale * randomFloats(generator), 0.0f);
	}

	auto buffer = GraphicsSystem::Instance::get()->createBuffer(Buffer::Bind::Uniform |
		Buffer::Bind::TransferDst, Buffer::Access::None, ssaoKernel,
		0, 0, Buffer::Usage::PreferGPU, Buffer::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(buffer, "buffer.uniform.ssao.sample");
	return buffer;
}
static ID<Image> createNoiseTexture()
{
	uniform_real_distribution<float> randomFloats(0.0f, 1.0f);
	default_random_engine generator;
	vector<float4> ssaoNoise(NOISE_SIZE * NOISE_SIZE);

	for (uint32 i = 0; i < NOISE_SIZE * NOISE_SIZE; i++)
	{
		auto noise = float3(
			randomFloats(generator) * 2.0f - 1.0f,
			randomFloats(generator) * 2.0f - 1.0f, 0.0f);
		ssaoNoise[i] = float4(noise, 0.0f);
	}

	auto texture = GraphicsSystem::Instance::get()->createImage(Image::Format::SfloatR16G16B16A16,
		Image::Bind::TransferDst | Image::Bind::Sampled, { { ssaoNoise.data() } },
		uint2(NOISE_SIZE), Image::Strategy::Size, Image::Format::SfloatR32G32B32A32);
	SET_RESOURCE_DEBUG_NAME(texture, "image.ssao.random");
	return texture;
}

//--------------------------------------------------------------------------------------------------
static ID<GraphicsPipeline> createPipeline(uint32 sampleCount)
{
	map<string, Pipeline::SpecConst> specConsts =
	{ { "SAMPLE_COUNT", Pipeline::SpecConst(sampleCount) } };
	auto pbrLightingSystem = manager->get<PbrLightingRenderSystem>();
	lightingSystem->setConsts(lightingSystem->getUseShadowBuffer(), true);
	return ResourceSystem::getInstance()->loadGraphicsPipeline("ssao",
		lightingSystem->getAoFramebuffers()[0], false, true, 0, 0, specConsts);
}
static map<string, DescriptorSet::Uniform> getUniforms(ID<Buffer> sampleBuffer, ID<Image> noiseTexture)
{
	auto swapchainSize = graphicsSystem->getSwapchainSize();
	auto deferredSystem = manager->get<DeferredRenderSystem>();
	auto gFramebufferView = graphicsSystem->get(DeferredRenderSystem::getInstance()->getGFramebuffer());
	const auto& colorAttachments = gFramebufferView->getColorAttachments();
	auto depthStencilAttachment = gFramebufferView->getDepthStencilAttachment();

	map<string, DescriptorSet::Uniform> uniforms =
	{ 
		{ "gBufferNormals", DescriptorSet::Uniform(
			colorAttachments[DeferredRenderSystem::normalsGBuffer].imageView, 1, swapchainSize) },
		{ "depthBuffer", DescriptorSet::Uniform(
			depthStencilAttachment.imageView, 1, swapchainSize) },
		{ "samples", DescriptorSet::Uniform(
			sampleBuffer, 1, swapchainSize) },
		{ "noise", DescriptorSet::Uniform(
			graphicsSystem->get(noiseTexture)->getDefaultView(), 1, swapchainSize) },
		{ "cc", DescriptorSet::Uniform(graphicsSystem->getCameraConstantsBuffers()) }
	};

	return uniforms;
}

//--------------------------------------------------------------------------------------------------
void SsaoRenderSystem::initialize()
{
	auto manager = getManager();
	auto graphicsSystem = getGraphicsSystem();

	auto settingsSystem = SettingsSystem::Instance::tryGet();
	if (settingsSystem)
		settingsSystem->getBool("ssao.isEnabled", isEnabled);

	if (isEnabled)
	{
		if (!sampleBuffer)
			sampleBuffer = createSampleBuffer(graphicsSystem, sampleCount);
		if (!noiseTexture)
			noiseTexture = createNoiseTexture(graphicsSystem);
		if (!pipeline)
			pipeline = createPipeline(manager, sampleCount);
	}

	#if GARDEN_EDITOR
	editor = new SsaoEditor(this);
	#endif
}
void SsaoRenderSystem::terminate()
{
	#if GARDEN_EDITOR
	delete (SsaoEditor*)editor;
	#endif
}

//--------------------------------------------------------------------------------------------------
void SsaoRenderSystem::render()
{
	#if GARDEN_EDITOR
	((SsaoEditor*)editor)->render();
	#endif
}
void SsaoRenderSystem::preAoRender()
{
	if (!isEnabled)
		return;

	auto graphicsSystem = getGraphicsSystem();
	if (!sampleBuffer)
		sampleBuffer = createSampleBuffer(graphicsSystem, sampleCount);
	if (!noiseTexture)
		noiseTexture = createNoiseTexture(graphicsSystem);
	if (!pipeline)
		pipeline = createPipeline(getManager(), sampleCount);
}
bool SsaoRenderSystem::aoRender()
{
	if (!isEnabled || intensity == 0.0f)
		return false;
	
	auto graphicsSystem = getGraphicsSystem();
	auto pipelineView = graphicsSystem->get(pipeline);
	auto sampleBufferView = graphicsSystem->get(sampleBuffer);
	auto noiseTextureView = graphicsSystem->get(noiseTexture);

	if (!pipelineView->isReady() || !sampleBufferView->isReady() ||
		!noiseTextureView->isReady())
	{
		return false;
	}

	if (!descriptorSet)
	{
		auto uniforms = getUniforms(getManager(),
			graphicsSystem, sampleBuffer, noiseTexture);
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.ssao");
	}

	constexpr auto uvToNDC = float4x4
	(
		2.0f, 0.0f, 0.0f, -1.0f,
		0.0f, 2.0f, 0.0f, -1.0f,
		0.0f, 0.0f, 1.0f,  0.0f,
		0.0f, 0.0f, 0.0f,  1.0f
	);
	constexpr auto ndcToUV = float4x4
	(
		0.5f, 0.0f, 0.0f, 0.5f,
		0.0f, 0.5f, 0.0f, 0.5f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	);

	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	auto pushConstants = pipelineView->getPushConstants<PushConstants>();
	pushConstants->uvToView = cameraConstants.projInverse * uvToNDC;
	pushConstants->uvToView[0][3] = radius;
	pushConstants->uvToView[1][3] = -bias;
	pushConstants->uvToView[3][3] = intensity;
	pushConstants->viewToUv = ndcToUV * cameraConstants.projection;

	SET_GPU_DEBUG_LABEL("SSAO", Color::transparent);
	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->bindDescriptorSet(descriptorSet, graphicsSystem->getSwapchainIndex());
	pipelineView->pushConstants();
	pipelineView->drawFullscreen();
	return true;
}

//--------------------------------------------------------------------------------------------------
void SsaoRenderSystem::recreateSwapchain(const SwapchainChanges& changes)
{
	if ((changes.framebufferSize || changes.bufferCount) && descriptorSet)
	{
		auto graphicsSystem = getGraphicsSystem();
		auto descriptorSetView = graphicsSystem->get(descriptorSet);
		auto uniforms = getUniforms(getManager(),
			graphicsSystem, sampleBuffer, noiseTexture);
		descriptorSetView->recreate(std::move(uniforms));
	}
}

void SsaoRenderSystem::setConsts(uint32 sampleCount)
{
	if (this->sampleCount == sampleCount)
		return;
	this->sampleCount = sampleCount;

	if (!pipeline)
		return;

	auto graphicsSystem = getGraphicsSystem();
	graphicsSystem->destroy(descriptorSet);
	descriptorSet = {};

	if (this->sampleCount != sampleCount)
	{
		graphicsSystem->destroy(sampleBuffer);
		sampleBuffer = createSampleBuffer(graphicsSystem, sampleCount);
	}

	graphicsSystem->destroy(pipeline);
	pipeline = createPipeline(getManager(), sampleCount);
}

ID<Buffer> SsaoRenderSystem::getSampleBuffer()
{
	if (!sampleBuffer)
		sampleBuffer = createSampleBuffer(getGraphicsSystem(), sampleCount);
	return sampleBuffer;
}
ID<Image> SsaoRenderSystem::getNoiseTexture()
{
	if (!noiseTexture)
		noiseTexture = createNoiseTexture(getGraphicsSystem());
	return noiseTexture;
}
ID<GraphicsPipeline> SsaoRenderSystem::getPipeline()
{
	if (!pipeline)
		pipeline = createPipeline(getManager(), sampleCount);
	return pipeline;
}
*/