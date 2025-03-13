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

// TODO: further improve bloom using these approaches: https://github.com/google/filament/commit/9f62dc2f2f531999aebdfabcf848696c7edaccfa
// We can use downsample2x to improve bloom quality by increasing it resolution.

using namespace garden;

//**********************************************************************************************************************
static ID<Image> createBloomBuffer(vector<ID<ImageView>>& imageViews)
{
	constexpr auto bufferFormat = Image::Format::UfloatB10G11R11;
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto bloomBufferSize =  max(graphicsSystem->getScaledFramebufferSize() / 2u, uint2::one);
	auto mipCount = std::min(BloomRenderSystem::maxBloomMipCount, (uint8)calcMipCount(bloomBufferSize));
	imageViews.resize(mipCount);

	Image::Mips mips(mipCount);
	for (uint8 i = 0; i < mipCount; i++)
		mips[i].push_back(nullptr);

	auto image = graphicsSystem->createImage(bufferFormat, Image::Bind::ColorAttachment | 
		Image::Bind::Sampled | Image::Bind::TransferDst, mips, bloomBufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.bloom.buffer");

	for (uint8 i = 0; i < mipCount; i++)
	{
		auto imageView = graphicsSystem->createImageView(
			image, Image::Type::Texture2D, bufferFormat, i, 1, 0, 1);
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
		{ Framebuffer::OutputAttachment(imageViews[i], false, true, true) };
		auto framebuffer = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
		SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.bloom" + to_string(i));
		framebuffers[i] = framebuffer;
		framebufferSize = max(framebufferSize / 2u, uint2::one);
	}
}

//**********************************************************************************************************************
static map<string, DescriptorSet::Uniform> getUniforms(ID<ImageView> srcTexture)
{
	map<string, DescriptorSet::Uniform> uniforms = { { "srcTexture", DescriptorSet::Uniform(srcTexture) } };
	return uniforms;
}

static void createBloomDescriptorSets(ID<Image> bloomBuffer, 
	ID<GraphicsPipeline> downsamplePipeline, ID<GraphicsPipeline> upsamplePipeline, 
	const vector<ID<ImageView>>& imageViews, vector<ID<DescriptorSet>>& descriptorSets)
{
	auto mipCount = (uint8)imageViews.size();
	descriptorSets.resize(mipCount + (mipCount - 1));

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto hdrFramebufferView = graphicsSystem->get(DeferredRenderSystem::Instance::get()->getHdrFramebuffer());
	auto uniforms = getUniforms(hdrFramebufferView->getColorAttachments()[0].imageView);
	descriptorSets[0] = graphicsSystem->createDescriptorSet(downsamplePipeline, std::move(uniforms));
	SET_RESOURCE_DEBUG_NAME(descriptorSets[0], "descriptorSet.bloom.downsample0");

	auto mipCountMinOne = mipCount - 1;
	for (uint8 i = 1; i < mipCount; i++)
	{
		uniforms = getUniforms(imageViews[i - 1]);
		auto descriptorSet = graphicsSystem->createDescriptorSet(downsamplePipeline, std::move(uniforms));
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
	map<string, Pipeline::SpecConstValue> specConsts =
	{
		{ "USE_THRESHOLD", Pipeline::SpecConstValue(useThreshold) },
		{ "USE_ANTI_FLICKERING", Pipeline::SpecConstValue(useAntiFlickering) }
	};

	auto toneMappingSystem = ToneMappingRenderSystem::Instance::get();
	toneMappingSystem->setConsts(true, toneMappingSystem->getToneMapper());

	return ResourceSystem::Instance::get()->loadGraphicsPipeline(
		"bloom/downsample", framebuffer, false, true, 0, 0, specConsts);
}
static ID<GraphicsPipeline> createUpsamplePipeline(ID<Framebuffer> framebuffer)
{
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("bloom/upsample", framebuffer);
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
			auto imageView = GraphicsSystem::Instance::get()->get(bloomBuffer);
			imageView->clear(f32x4::zero);
		}
		return;
	}
	
	if (!bloomBuffer)
		bloomBuffer = createBloomBuffer(imageViews);
	if (framebuffers.empty())
		createBloomFramebuffers(imageViews, framebuffers);
	if (!downsamplePipeline)
		downsamplePipeline = createDownsamplePipeline(framebuffers[0], useThreshold, useAntiFlickering);
	if (!upsamplePipeline)
		upsamplePipeline = createUpsamplePipeline(framebuffers[0]);
	
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto downsamplePipelineView = graphicsSystem->get(downsamplePipeline);
	auto upsamplePipelineView = graphicsSystem->get(upsamplePipeline);
	
	if (!downsamplePipelineView->isReady() || !upsamplePipelineView->isReady())
		return;

	if (descriptorSets.empty())
		createBloomDescriptorSets(bloomBuffer, downsamplePipeline, upsamplePipeline, imageViews, descriptorSets);
	
	auto mipCount = (uint8)imageViews.size();
	auto framebufferView = graphicsSystem->get(framebuffers[0]);

	SET_GPU_DEBUG_LABEL("Bloom", Color::transparent);
	{
		auto pushConstants = downsamplePipelineView->getPushConstants<PushConstants>();
		pushConstants->threshold = threshold;

		SET_GPU_DEBUG_LABEL("Downsample", Color::transparent);
		framebufferView->beginRenderPass(f32x4::zero);
		downsamplePipelineView->bind(downsampleFirstVariant);
		downsamplePipelineView->setViewportScissor();
		downsamplePipelineView->bindDescriptorSet(descriptorSets[0]);
		downsamplePipelineView->pushConstants();
		downsamplePipelineView->drawFullscreen();
		framebufferView->endRenderPass();

		for (uint8 i = 1; i < mipCount; i++)
		{
			framebufferView = graphicsSystem->get(framebuffers[i - 1]);
			auto framebufferSize = framebufferView->getSize();

			framebufferView = graphicsSystem->get(framebuffers[i]);
			downsamplePipelineView->bind(framebufferSize.x & 1 || framebufferSize.y & 1 ? 
				downsampleBaseVariant : downsample6x6Variant);
			framebufferView->beginRenderPass(f32x4::zero);
			downsamplePipelineView->setViewportScissor();
			downsamplePipelineView->bindDescriptorSet(descriptorSets[i]);
			downsamplePipelineView->drawFullscreen();
			framebufferView->endRenderPass();
		}
	}
	{
		SET_GPU_DEBUG_LABEL("Upsample", Color::transparent);
		upsamplePipelineView->bind();

		for (int8 i = mipCount - 2; i >= 0; i--)
		{
			framebufferView = graphicsSystem->get(framebuffers[i]);
			framebufferView->beginRenderPass(f32x4::zero);
			upsamplePipelineView->setViewportScissor();
			upsamplePipelineView->bindDescriptorSet(descriptorSets[mipCount + i]);
			upsamplePipelineView->drawFullscreen();
			framebufferView->endRenderPass();
		}
	}
}

//**********************************************************************************************************************
void BloomRenderSystem::gBufferRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	graphicsSystem->destroy(descriptorSets);
	descriptorSets.clear();
	graphicsSystem->destroy(framebuffers);
	graphicsSystem->destroy(imageViews);
	graphicsSystem->destroy(bloomBuffer);

	if (bloomBuffer)
		bloomBuffer = createBloomBuffer(imageViews);
	if (!framebuffers.empty())
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
const vector<ID<Framebuffer>>& BloomRenderSystem::getFramebuffers()
{
	if (framebuffers.empty())
		createBloomFramebuffers(imageViews, framebuffers);
	return framebuffers;
}