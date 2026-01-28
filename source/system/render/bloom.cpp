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

static ID<Image> createBloomBuffer(GraphicsSystem* graphicsSystem, vector<ID<ImageView>>& imageViews, uint8 maxMipCount)
{
	auto bloomBufferSize = max(graphicsSystem->getFramebufferSize() / 2u, uint2::one);
	auto mipCount = std::min(calcMipCount(bloomBufferSize), maxMipCount);
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

static void createBloomFramebuffers(GraphicsSystem* graphicsSystem, 
	const vector<ID<ImageView>>& imageViews, vector<ID<Framebuffer>>& framebuffers)
{
	auto framebufferSize = max(graphicsSystem->getFramebufferSize() / 2u, uint2::one);
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

static void createBloomDescriptorSets(GraphicsSystem* graphicsSystem, 
	ID<GraphicsPipeline> downsamplePipeline, ID<GraphicsPipeline> upsamplePipeline, 
	const vector<ID<ImageView>>& imageViews, vector<ID<DescriptorSet>>& descriptorSets)
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto upscaledHdrFramebufferView = graphicsSystem->get(deferredSystem->getUpscaleHdrFramebuffer());
	auto mipCount = (uint8)imageViews.size();
	descriptorSets.resize(mipCount + (mipCount - 1));

	auto uniforms = getUniforms(upscaledHdrFramebufferView->getColorAttachments()[0].imageView);
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
static uint8 getMaxMipCount(GraphicsQuality quality) noexcept
{
	switch (quality)
	{
		case GraphicsQuality::PotatoPC: return 2;
		case GraphicsQuality::Low: return 4;
		case GraphicsQuality::Medium: return 5;
		case GraphicsQuality::High: return 7;
		case GraphicsQuality::Ultra: return 7;
		default: abort();
	}
}

static ID<GraphicsPipeline> createDownsamplePipeline(
	ID<Framebuffer> framebuffer, bool useThreshold, bool useAntiFlickering)
{
	auto toneMappingSystem = ToneMappingSystem::Instance::get();
	auto tmOptions = toneMappingSystem->getOptions();
	tmOptions.useBloomBuffer = true;
	toneMappingSystem->setOptions(tmOptions);

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
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", BloomRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", BloomRenderSystem::deinit);

	manager->registerEvent("BloomRecreate");
}
BloomRenderSystem::~BloomRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		manager->unregisterEvent("BloomRecreate");

		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", BloomRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", BloomRenderSystem::deinit);
	}

	unsetSingleton();
}

void BloomRenderSystem::init()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("PreLdrRender", BloomRenderSystem::preLdrRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", BloomRenderSystem::gBufferRecreate);
	ECSM_SUBSCRIBE_TO_EVENT("QualityChange", BloomRenderSystem::qualityChange);

	auto settingsSystem = SettingsSystem::Instance::tryGet();
	if (settingsSystem)
	{
		settingsSystem->getBool("bloom.enabled", isEnabled);
		settingsSystem->getType("bloom.quality", quality, graphicsQualityNames, (uint32)GraphicsQuality::Count);
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

		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreLdrRender", BloomRenderSystem::preLdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", BloomRenderSystem::gBufferRecreate);
		ECSM_UNSUBSCRIBE_FROM_EVENT("QualityChange", BloomRenderSystem::qualityChange);
	}
}

//**********************************************************************************************************************
void BloomRenderSystem::preLdrRender()
{
	SET_CPU_ZONE_SCOPED("Bloom Pre LDR Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!isEnabled || intensity == 0.0f)
	{
		if (bloomBuffer)
		{
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
			bloomBuffer = createBloomBuffer(graphicsSystem, imageViews, getMaxMipCount(quality));
		if (framebuffers.empty())
			createBloomFramebuffers(graphicsSystem, imageViews, framebuffers);
		if (!downsamplePipeline)
			downsamplePipeline = createDownsamplePipeline(framebuffers[0], useThreshold, useAntiFlickering);
		if (!upsamplePipeline)
			upsamplePipeline = createUpsamplePipeline(framebuffers[0]);
		isInitialized = true;
	}
	
	auto downsamplePipelineView = graphicsSystem->get(downsamplePipeline);
	auto upsamplePipelineView = graphicsSystem->get(upsamplePipeline);
	if (!downsamplePipelineView->isReady() || !upsamplePipelineView->isReady())
		return;

	if (descriptorSets.empty())
		createBloomDescriptorSets(graphicsSystem, downsamplePipeline, upsamplePipeline, imageViews, descriptorSets);
	
	auto mipCount = (uint8)imageViews.size();
	downsamplePipelineView->updateFramebuffer(framebuffers[0]);

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		SET_GPU_DEBUG_LABEL("Bloom");
		{
			SET_GPU_DEBUG_LABEL("Downsample");

			PushConstants pc;
			pc.threshold = saturate(threshold);
			downsamplePipelineView->pushConstants(&pc);
			{
				RenderPass renderPass(framebuffers[0], float4::zero);
				downsamplePipelineView->bind(BLOOM_DOWNSAMPLE_FIRST);
				downsamplePipelineView->setViewportScissor();
				downsamplePipelineView->bindDescriptorSet(descriptorSets[0]);
				downsamplePipelineView->drawFullscreen();
			}

			for (uint8 i = 1; i < mipCount; i++)
			{
				auto framebufferView = graphicsSystem->get(framebuffers[i - 1]);
				auto framebufferSize = framebufferView->getSize();
				downsamplePipelineView->updateFramebuffer(framebuffers[i]);

				RenderPass renderPass(framebuffers[i], float4::zero);
				downsamplePipelineView->bind(framebufferSize.x & 1 || framebufferSize.y & 1 ? 
					BLOOM_DOWNSAMPLE_BASE : BLOOM_DOWNSAMPLE_6X6);
				downsamplePipelineView->setViewportScissor();
				downsamplePipelineView->bindDescriptorSet(descriptorSets[i]);
				downsamplePipelineView->drawFullscreen();
			}
		}
		{
			SET_GPU_DEBUG_LABEL("Upsample");

			for (int8 i = mipCount - 2; i >= 0; i--)
			{
				upsamplePipelineView->updateFramebuffer(framebuffers[i]);

				RenderPass renderPass(framebuffers[i], float4::zero);
				upsamplePipelineView->bind();
				upsamplePipelineView->setViewportScissor();
				upsamplePipelineView->bindDescriptorSet(descriptorSets[mipCount + i]);
				upsamplePipelineView->drawFullscreen();
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
		bloomBuffer = createBloomBuffer(graphicsSystem, imageViews, getMaxMipCount(quality));
	}
	if (!framebuffers.empty())
	{
		graphicsSystem->destroy(framebuffers);
		createBloomFramebuffers(graphicsSystem, imageViews, framebuffers);

		if (downsamplePipeline)
		{
			auto pipelineView = graphicsSystem->get(downsamplePipeline);
			if (pipelineView->isReady())
				pipelineView->updateFramebuffer(framebuffers[0]);
		}
		if (upsamplePipeline)
		{
			auto pipelineView = graphicsSystem->get(upsamplePipeline);
			if (pipelineView->isReady())
				pipelineView->updateFramebuffer(framebuffers[0]);
		}
	}
}
void BloomRenderSystem::qualityChange()
{
	setQuality(GraphicsSystem::Instance::get()->quality);
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

void BloomRenderSystem::setQuality(GraphicsQuality quality)
{
	if (this->quality == quality)
		return;

	this->quality = quality;
	gBufferRecreate();

	Manager::Instance::get()->runEvent("BloomRecreate");
}

//**********************************************************************************************************************
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
		bloomBuffer = createBloomBuffer(GraphicsSystem::Instance::get(), imageViews, getMaxMipCount(quality));
	return bloomBuffer;
}
const vector<ID<ImageView>>& BloomRenderSystem::getImageViews()
{
	if (!bloomBuffer)
		bloomBuffer = createBloomBuffer(GraphicsSystem::Instance::get(), imageViews, getMaxMipCount(quality));
	return imageViews;
}
const vector<ID<Framebuffer>>& BloomRenderSystem::getFramebuffers()
{
	if (framebuffers.empty())
		createBloomFramebuffers(GraphicsSystem::Instance::get(), imageViews, framebuffers);
	return framebuffers;
}