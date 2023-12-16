//--------------------------------------------------------------------------------------------------
// Copyright 2022-2023 Nikita Fediuchin. All rights reserved.
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

#include "garden/system/graphics/ssao.hpp"
#include "garden/system/graphics/editor/ssao.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/settings.hpp"
#include <random>

// TODO: or may be use even lower 24, 20, 16?
#define SAMPLE_COUNT 32
#define NOISE_SIZE 4

using namespace garden;

namespace
{
	struct PushConstants final
	{
		float4x4 uvToView;
		float4x4 viewToUv;
	};
}

//--------------------------------------------------------------------------------------------------
static ID<Buffer> createSampleBuffer(GraphicsSystem* graphicsSystem)
{
	uniform_real_distribution<float> randomFloats(0.0f, 1.0f);
	default_random_engine generator;
	vector<float4> ssaoKernel(SAMPLE_COUNT);

	for (uint32 i = 0; i < SAMPLE_COUNT; i++)
	{
		auto sample = normalize(float3(
			randomFloats(generator) * 2.0f - 1.0f,
			randomFloats(generator) * 2.0f - 1.0f,
			randomFloats(generator)));
		auto scale = (float)i / SAMPLE_COUNT; 
		scale = lerp(0.1f, 1.0f, scale * scale);
		ssaoKernel[i] = float4(sample * scale * randomFloats(generator), 0.0f);
	}

	auto buffer = graphicsSystem->createBuffer(Buffer::Bind::Uniform |
		Buffer::Bind::TransferDst, Buffer::Access::None, ssaoKernel,
		0, 0, Buffer::Usage::PreferGPU, Buffer::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, buffer, "buffer.uniform.ssao.sample");
	return buffer;
}
static ID<Image> createNoiseTexture(GraphicsSystem* graphicsSystem)
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

	auto texture = graphicsSystem->createImage(Image::Format::SfloatR16G16B16A16,
		Image::Bind::TransferDst | Image::Bind::Sampled, { { ssaoNoise.data() } },
		int2(NOISE_SIZE), Image::Strategy::Size, Image::Format::SfloatR32G32B32A32);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, texture, "image.ssao.random");
	return texture;
}

//--------------------------------------------------------------------------------------------------
static ID<GraphicsPipeline> createPipeline(LightingRenderSystem* lightingSystem)
{
	return ResourceSystem::getInstance()->loadGraphicsPipeline(
		"ssao", lightingSystem->getAoFramebuffers()[0]);
}
static map<string, DescriptorSet::Uniform> getUniforms(Manager* manager,
	GraphicsSystem* graphicsSystem, ID<Buffer> sampleBuffer, ID<Image> noiseTexture)
{
	auto swapchainSize = graphicsSystem->getSwapchainSize();
	auto deferredSystem = manager->get<DeferredRenderSystem>();
	auto gFramebufferView = graphicsSystem->get(deferredSystem->getGFramebuffer());
	auto& colorAttachments = gFramebufferView->getColorAttachments();
	auto depthStencilAttahcment = gFramebufferView->getDepthStencilAttachment();
	auto noiseTextureView = graphicsSystem->get(noiseTexture);

	map<string, DescriptorSet::Uniform> uniforms =
	{ 
		{ "gBuffer1", DescriptorSet::Uniform(
			colorAttachments[0].imageView, 1, swapchainSize) },
		{ "depthBuffer", DescriptorSet::Uniform(
			depthStencilAttahcment.imageView, 1, swapchainSize) },
		{ "samples", DescriptorSet::Uniform(
			sampleBuffer, 1, swapchainSize) },
		{ "noise", DescriptorSet::Uniform(
			noiseTextureView->getDefaultView(), 1, swapchainSize) },
		{ "cc", DescriptorSet::Uniform(graphicsSystem->getCameraConstantsBuffers()) }
	};

	return uniforms;
}

//--------------------------------------------------------------------------------------------------
void SsaoRenderSystem::initialize()
{
	auto manager = getManager();
	auto graphicsSystem = getGraphicsSystem();

	if (!sampleBuffer)sampleBuffer = createSampleBuffer(graphicsSystem);
	if (!noiseTexture) noiseTexture = createNoiseTexture(graphicsSystem);
	if (!pipeline) pipeline = createPipeline(manager->get<LightingRenderSystem>());

	auto settingsSystem = manager->tryGet<SettingsSystem>();
	if (settingsSystem) settingsSystem->getBool("useSSAO", isEnabled);

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
bool SsaoRenderSystem::aoRender()
{
	if (!isEnabled) return false;
	
	auto graphicsSystem = getGraphicsSystem();
	auto pipelineView = graphicsSystem->get(pipeline);
	auto sampleBufferView = graphicsSystem->get(sampleBuffer);
	auto noiseTextureView = graphicsSystem->get(noiseTexture);

	if (!pipelineView->isReady() || !sampleBufferView->isReady() ||
		!noiseTextureView->isReady()) return false;

	if (!descriptorSet)
	{
		auto uniforms = getUniforms(getManager(),
			graphicsSystem, sampleBuffer, noiseTexture);
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, descriptorSet, "descriptorSet.ssao");
	}

	const auto uvToNDC = float4x4
	(
		2.0f, 0.0f, 0.0f, -1.0f,
		0.0f, 2.0f, 0.0f, -1.0f,
		0.0f, 0.0f, 1.0f,  0.0f,
		0.0f, 0.0f, 0.0f,  1.0f
	);
	const auto ndcToUV = float4x4
	(
		0.5f, 0.0f, 0.0f, 0.5f,
		0.0f, 0.5f, 0.0f, 0.5f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	);

	auto framebufferView = graphicsSystem->get(
		getLightingSystem()->getAoFramebuffers()[0]);
	auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();

	SET_GPU_DEBUG_LABEL("SSAO", Color::transparent);
	pipelineView->bind();
	pipelineView->setViewportScissor(float4(
		float2(0), framebufferView->getSize()));
	pipelineView->bindDescriptorSet(descriptorSet,
		graphicsSystem->getSwapchainIndex());
	auto pushConstants = pipelineView->getPushConstants<PushConstants>();
	pushConstants->uvToView = cameraConstants.projInverse * uvToNDC;
	pushConstants->uvToView[0][3] = radius;
	pushConstants->uvToView[1][3] = -bias;
	pushConstants->uvToView[3][3] = intensity;
	pushConstants->viewToUv = ndcToUV * cameraConstants.projection;
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

//--------------------------------------------------------------------------------------------------
ID<Buffer> SsaoRenderSystem::getSampleBuffer()
{
	if (!sampleBuffer) sampleBuffer = createSampleBuffer(getGraphicsSystem());
	return sampleBuffer;
}
ID<Image> SsaoRenderSystem::getNoiseTexture()
{
	if (!noiseTexture) noiseTexture = createNoiseTexture(getGraphicsSystem());
	return noiseTexture;
}
ID<GraphicsPipeline> SsaoRenderSystem::getPipeline()
{
	if (!pipeline) pipeline = createPipeline(getManager()->get<LightingRenderSystem>());
	return pipeline;
}