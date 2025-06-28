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

#include "garden/system/render/bloom.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/render/tone-mapping.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/settings.hpp"
#include "garden/profiler.hpp"
#include "bloom/variants.h"

// TODO: further improve bloom using these approaches: https://github.com/google/filament/commit/9f62dc2f2f531999aebdfabcf848696c7edaccfa
// We can use downsample2x to improve bloom quality by increasing it resolution.

using namespace garden;

static ID<Image> createBloomBuffer(vector<ID<ImageView>>& imageViews)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto bloomBufferSize = max(graphicsSystem->getScaledFramebufferSize() / 2u, uint2::one);
	auto mipCount = std::min(BloomRenderSystem::maxBloomMipCount, calcMipCount(bloomBufferSize));
	imageViews.resize(mipCount);

	Image::Mips mips(mipCount);
	for (uint8 i = 0; i < mipCount; i++)
		mips[i].resize(1);

	auto image = graphicsSystem->createImage(BloomRenderSystem::bufferFormat, Image::Usage::ColorAttachment | 
		Image::Usage::Sampled | Image::Usage::TransferDst, mips, bloomBufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.bloom.buffer");

	for (uint8 i = 0; i < mipCount; i++)
	{
		auto imageView = graphicsSystem->createImageView(image, 
			Image::Type::Texture2D, BloomRenderSystem::bufferFormat, i, 1, 0, 1);
		SET_RESOURCE_DEBUG_NAME(imageView, "imageView.bloom.buffer" + to_string(i));
		imageViews[i] = imageView;
	}
	return image;
}

static void createBloomFramebuffers(const vector<ID<ImageView>>& imageViews, vector<ID<Framebuffer>>& framebuffers)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = max(graphicsSystem->getScaledFramebufferSize() / 2u, uint2::one);
	auto mipCount = (uint8)imageViews.size();
	framebuffers.resize(mipCount);

	for (uint8 i = 0; i < mipCount; i++)
	{
		vector<Framebuffer::OutputAttachment> colorAttachments =
		{ Framebuffer::OutputAttachment(imageViews[i], { false, true, true }) };
		auto framebuffer = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
		SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.bloom" + to_string(i));
		framebuffers[i] = framebuffer;
		framebufferSize = max(framebufferSize / 2u, uint2::one);
	}
}

//**********************************************************************************************************************
static DescriptorSet::Uniforms getUniforms(ID<ImageView> srcBuffer)
{
	return { { "srcBuffer", DescriptorSet::Uniform(srcBuffer) } };
}

static void createBloomDescriptorSets(ID<GraphicsPipeline> downsamplePipeline, ID<GraphicsPipeline> upsamplePipeline, 
	 const vector<ID<ImageView>>& imageViews, vector<ID<DescriptorSet>>& descriptorSets)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto hdrFramebufferView = graphicsSystem->get(deferredSystem->getHdrFramebuffer());
	auto mipCount = (uint8)imageViews.size();
	descriptorSets.resize(mipCount + (mipCount - 1));

	auto uniforms = getUniforms(hdrFramebufferView->getColorAttachments()[0].imageView);
	auto descriptorSet = graphicsSystem->createDescriptorSet(downsamplePipeline, std::move(uniforms));
	SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.bloom.downsample0");
	descriptorSets[0] = descriptorSet;

	auto mipCountMinOne = mipCount - 1;
	for (uint8 i = 1; i < mipCount; i++)
	{
		uniforms = getUniforms(imageViews[i - 1]);
		descriptorSet = graphicsSystem->createDescriptorSet(downsamplePipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.bloom.downsample" + to_string(i));
		descriptorSets[i] = descriptorSet;

		uniforms = getUniforms(imageViews[mipCount - i]);
		auto index = mipCount + (mipCountMinOne - i);
		descriptorSet = graphicsSystem->createDescriptorSet(upsamplePipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.bloom.upsample" + to_string(i));
		descriptorSets[index] = descriptorSet;
	}
}

static ID<GraphicsPipeline> createDownsamplePipeline(
	ID<Framebuffer> framebuffer, bool useThreshold, bool useAntiFlickering)
{
	auto toneMappingSystem = ToneMappingRenderSystem::Instance::get();
	toneMappingSystem->setConsts(true, toneMappingSystem->getToneMapper());

	Pipeline::SpecConstValues specConsts =
	{
		{ "USE_THRESHOLD", Pipeline::SpecConstValue(useThreshold) },
		{ "USE_ANTI_FLICKERING", Pipeline::SpecConstValue(useAntiFlickering) }
	};

	ResourceSystem::GraphicsOptions options;
	options.specConstValues = &specConsts;

	return ResourceSystem::Instance::get()->loadGraphicsPipeline("bloom/downsample", framebuffer, options);
}
static ID<GraphicsPipeline> createUpsamplePipeline(ID<Framebuffer> framebuffer)
{
	ResourceSystem::GraphicsOptions options;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("bloom/upsample", framebuffer, options);
}

//**********************************************************************************************************************
BloomRenderSystem::BloomRenderSystem(bool useThreshold, bool useAntiFlickering, bool setSingleton) :
	Singleton(setSingleton), useThreshold(useThreshold), useAntiFlickering(useAntiFlickering)
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", BloomRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", BloomRenderSystem::deinit);
}
BloomRenderSystem::~BloomRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", BloomRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", BloomRenderSystem::deinit);
	}

	unsetSingleton();
}

void BloomRenderSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("PreLdrRender", BloomRenderSystem::preLdrRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", BloomRenderSystem::gBufferRecreate);

	auto settingsSystem = SettingsSystem::Instance::tryGet();
	if (settingsSystem)
		settingsSystem->getBool("bloom.isEnabled", isEnabled);

	if (isEnabled)
	{
		if (!bloomBuffer)
			bloomBuffer = createBloomBuffer(imageViews);
		if (framebuffers.empty())
			createBloomFramebuffers(imageViews, framebuffers);
		if (!downsamplePipeline)
			downsamplePipeline = createDownsamplePipeline(framebuffers[0], useThreshold, useAntiFlickering);
		if (!upsamplePipeline)
			upsamplePipeline = createUpsamplePipeline(framebuffers[0]);
		isInitialized = true;
	}
}
void BloomRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(upsamplePipeline);
		graphicsSystem->destroy(downsamplePipeline);
		graphicsSystem->destroy(framebuffers);
		graphicsSystem->destroy(imageViews);
		graphicsSystem->destroy(bloomBuffer);

		ECSM_UNSUBSCRIBE_FROM_EVENT("PreLdrRender", BloomRenderSystem::preLdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", BloomRenderSystem::gBufferRecreate);
	}
}

//**********************************************************************************************************************
void BloomRenderSystem::preLdrRender()
{
	SET_CPU_ZONE_SCOPED("Bloom Pre LDR Render");

	if (!isEnabled || intensity == 0.0f)
	{
		if (bloomBuffer)
		{
			auto graphicsSystem = GraphicsSystem::Instance::get();
			auto imageView = graphicsSystem->get(bloomBuffer);
			graphicsSystem->startRecording(CommandBufferType::Frame);
			imageView->clear(float4::zero);
			graphicsSystem->stopRecording();
		}
		return;
	}
	
	if (!isInitialized)
	{
		if (!bloomBuffer)
			bloomBuffer = createBloomBuffer(imageViews);
		if (framebuffers.empty())
			createBloomFramebuffers(imageViews, framebuffers);
		if (!downsamplePipeline)
			downsamplePipeline = createDownsamplePipeline(framebuffers[0], useThreshold, useAntiFlickering);
		if (!upsamplePipeline)
			upsamplePipeline = createUpsamplePipeline(framebuffers[0]);
		isInitialized = true;
	}
	
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto downsamplePipelineView = graphicsSystem->get(downsamplePipeline);
	auto upsamplePipelineView = graphicsSystem->get(upsamplePipeline);
	
	if (!downsamplePipelineView->isReady() || !upsamplePipelineView->isReady())
		return;

	if (descriptorSets.empty())
		createBloomDescriptorSets(downsamplePipeline, upsamplePipeline, imageViews, descriptorSets);
	
	auto mipCount = (uint8)imageViews.size();
	auto framebufferView = graphicsSystem->get(framebuffers[0]);
	downsamplePipelineView->updateFramebuffer(framebuffers[0]);

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		SET_GPU_DEBUG_LABEL("Bloom", Color::transparent);
		{
			SET_GPU_DEBUG_LABEL("Downsample", Color::transparent);

			PushConstants pc;
			pc.threshold = threshold;
			downsamplePipelineView->pushConstants(&pc);

			framebufferView->beginRenderPass(float4::zero);
			downsamplePipelineView->bind(BLOOM_DOWNSAMPLE_FIRST);
			downsamplePipelineView->setViewportScissor();
			downsamplePipelineView->bindDescriptorSet(descriptorSets[0]);
			downsamplePipelineView->drawFullscreen();
			framebufferView->endRenderPass();

			for (uint8 i = 1; i < mipCount; i++)
			{
				framebufferView = graphicsSystem->get(framebuffers[i - 1]);
				auto framebufferSize = framebufferView->getSize();

				framebufferView = graphicsSystem->get(framebuffers[i]);
				downsamplePipelineView->updateFramebuffer(framebuffers[i]);
				framebufferView->beginRenderPass(float4::zero);
				downsamplePipelineView->bind(framebufferSize.x & 1 || framebufferSize.y & 1 ? 
					BLOOM_DOWNSAMPLE_BASE : BLOOM_DOWNSAMPLE_6X6);
				downsamplePipelineView->setViewportScissor();
				downsamplePipelineView->bindDescriptorSet(descriptorSets[i]);
				downsamplePipelineView->drawFullscreen();
				framebufferView->endRenderPass();
			}
		}
		{
			SET_GPU_DEBUG_LABEL("Upsample", Color::transparent);

			for (int8 i = mipCount - 2; i >= 0; i--)
			{
				framebufferView = graphicsSystem->get(framebuffers[i]);
				upsamplePipelineView->updateFramebuffer(framebuffers[i]);

				framebufferView->beginRenderPass(float4::zero);
				upsamplePipelineView->bind();
				upsamplePipelineView->setViewportScissor();
				upsamplePipelineView->bindDescriptorSet(descriptorSets[mipCount + i]);
				upsamplePipelineView->drawFullscreen();
				framebufferView->endRenderPass();
			}
		}
	}
	graphicsSystem->stopRecording();
}

//**********************************************************************************************************************
void BloomRenderSystem::gBufferRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	graphicsSystem->destroy(descriptorSets);
	descriptorSets.clear();

	if (bloomBuffer)
	{
		graphicsSystem->destroy(bloomBuffer);
		graphicsSystem->destroy(imageViews);
		bloomBuffer = createBloomBuffer(imageViews);
	}
	if (!framebuffers.empty())
	{
		graphicsSystem->destroy(framebuffers);
		createBloomFramebuffers(imageViews, framebuffers);

		if (downsamplePipeline)
		{
			auto pipelineView = graphicsSystem->get(downsamplePipeline);
			pipelineView->updateFramebuffer(framebuffers[0]);
		}
		if (upsamplePipeline)
		{
			auto pipelineView = graphicsSystem->get(upsamplePipeline);
			pipelineView->updateFramebuffer(framebuffers[0]);
		}
	}
}

void BloomRenderSystem::setConsts(bool useThreshold, bool useAntiFlickering)
{
	if (this->useThreshold == useThreshold && this->useAntiFlickering == useAntiFlickering)
		return;

	this->useThreshold = useThreshold;
	this->useAntiFlickering = useAntiFlickering;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	graphicsSystem->destroy(descriptorSets);
	descriptorSets.clear();
	
	if (downsamplePipeline)
	{
		graphicsSystem->destroy(downsamplePipeline);
		downsamplePipeline = createDownsamplePipeline(getFramebuffers()[0], useThreshold, useAntiFlickering);
	}
}

ID<GraphicsPipeline> BloomRenderSystem::getDownsamplePipeline()
{
	if (!downsamplePipeline)
		downsamplePipeline = createDownsamplePipeline(getFramebuffers()[0], useThreshold, useAntiFlickering);
	return downsamplePipeline;
}
ID<GraphicsPipeline> BloomRenderSystem::getUpsamplePipeline()
{
	if (!upsamplePipeline)
		upsamplePipeline = createUpsamplePipeline(getFramebuffers()[0]);
	return upsamplePipeline;
}

ID<Image> BloomRenderSystem::getBloomBuffer()
{
	if (!bloomBuffer)
		bloomBuffer = createBloomBuffer(imageViews);
	return bloomBuffer;
}
const vector<ID<ImageView>>& BloomRenderSystem::getImageViews()
{
	if (!bloomBuffer)
		bloomBuffer = createBloomBuffer(imageViews);
	return imageViews;
}
const vector<ID<Framebuffer>>& BloomRenderSystem::getFramebuffers()
{
	if (framebuffers.empty())
		createBloomFramebuffers(imageViews, framebuffers);
	return framebuffers;
}