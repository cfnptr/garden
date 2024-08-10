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
#include "garden/system/render/bloom.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/render/tone-mapping.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/settings.hpp"

#if GARDEN_EDITOR
#include "garden/editor/system/render/bloom.hpp"
#endif

using namespace garden;

namespace
{
	struct PushConstants final
	{
		float threshold;
	};
}

//--------------------------------------------------------------------------------------------------
static ID<Image> createBloomBufferData(int2 framebufferSize, vector<ID<ImageView>>& imageViews, vector<int2>& sizeBuffer)
{
	const auto bufferFormat = Image::Format::UfloatB10G11R11;
	auto bloomBufferSize =  max(framebufferSize / 2, int2(1));
	auto mipCount = std::min((uint8)MAX_BLOOM_MIP_COUNT,
		(uint8)calcMipCount(bloomBufferSize));
	Image::Mips mips(mipCount);
	for (uint8 i = 0; i < mipCount; i++)
		mips[i].push_back(nullptr);

	auto graphicsSystem = GraphicsSystem::get();
	auto image = graphicsSystem->createImage(bufferFormat,
		Image::Bind::ColorAttachment | Image::Bind::Sampled |
		Image::Bind::TransferDst, mips, bloomBufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.bloom.buffer");

	imageViews.resize(mipCount);
	sizeBuffer.resize(mipCount + 1);
	sizeBuffer[0] = framebufferSize;

	for (uint8 i = 0; i < mipCount; i++)
	{
		auto imageView = graphicsSystem->createImageView(
			image, Image::Type::Texture2D, bufferFormat, i, 1, 0, 1);
		SET_RESOURCE_DEBUG_NAME(imageView, "imageView.bloom.buffer" + to_string(i));
		imageViews[i] = imageView;

		sizeBuffer[i + 1] = bloomBufferSize;
		bloomBufferSize = max(bloomBufferSize / 2, int2(1));
	}
	return image;
}

//--------------------------------------------------------------------------------------------------
static void createBloomFramebuffers(int2 framebufferSize,
	const vector<ID<ImageView>>& imageViews, vector<ID<Framebuffer>>& framebuffers)
{
	auto graphicsSystem = GraphicsSystem::get();
	auto mipCount = (uint8)imageViews.size();
	framebufferSize = max(framebufferSize / 2, int2(1));
	framebuffers.resize(imageViews.size());

	for (uint8 i = 0; i < mipCount; i++)
	{
		vector<Framebuffer::OutputAttachment> colorAttachments =
		{ Framebuffer::OutputAttachment(imageViews[i], false, true, true) };
		auto framebuffer = graphicsSystem->createFramebuffer(
			framebufferSize, std::move(colorAttachments));
		SET_RESOURCE_DEBUG_NAME(framebuffer,
			"framebuffer.bloom" + to_string(i));
		framebuffers[i] = framebuffer;
		framebufferSize = max(framebufferSize / 2, int2(1));
	}
}

//--------------------------------------------------------------------------------------------------
static map<string, DescriptorSet::Uniform> getUniforms(ID<ImageView> srcTexture)
{
	map<string, DescriptorSet::Uniform> uniforms =
	{ { "srcTexture", DescriptorSet::Uniform(srcTexture) } };
	return uniforms;
}

static void createBloomDescriptorSets(ID<Image> bloomBuffer,
	ID<GraphicsPipeline> downsample0Pipeline, ID<GraphicsPipeline> downsamplePipeline,
	ID<GraphicsPipeline> upsamplePipeline, const vector<ID<ImageView>>& imageViews,
	vector<ID<DescriptorSet>>& descriptorSets)
{
	auto mipCount = (uint8)imageViews.size();
	descriptorSets.resize(mipCount + (mipCount - 1));

	auto hdrFramebufferView = graphicsSystem->get(deferredSystem->getHdrFramebuffer());
	auto uniforms = getUniforms(hdrFramebufferView->getColorAttachments()[0].imageView);
	descriptorSets[0] = graphicsSystem->createDescriptorSet(
		downsample0Pipeline, std::move(uniforms));
	SET_RESOURCE_DEBUG_NAME(descriptorSets[0],
		"descriptorSet.bloom.downsample0");

	auto mipCountMinOne = mipCount - 1;
	for (uint8 i = 1; i < mipCount; i++)
	{
		uniforms = getUniforms(imageViews[i - 1]);
		auto descriptorSet = graphicsSystem->createDescriptorSet(
			downsamplePipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet,
			"descriptorSet.bloom.downsample" + to_string(i));
		descriptorSets[i] = descriptorSet;

		uniforms = getUniforms(imageViews[mipCount - i]);
		auto index = mipCount + (mipCountMinOne - i);
		descriptorSet = graphicsSystem->createDescriptorSet(
			upsamplePipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet,
			"descriptorSet.bloom.upsample" + to_string(i));
		descriptorSets[index] = descriptorSet;
	}
}

//--------------------------------------------------------------------------------------------------
static ID<GraphicsPipeline> createDownsample0Pipeline(
	ID<Framebuffer> framebuffer, bool useThreshold, bool useAntiFlickering)
{
	map<string, Pipeline::SpecConst> specConsts =
	{
		{ "USE_THRESHOLD", Pipeline::SpecConst(useThreshold) },
		{ "USE_ANTI_FLICKERING", Pipeline::SpecConst(useAntiFlickering) }
	};

	auto toneMappingSystem = manager->get<ToneMappingRenderSystem>();
	toneMappingSystem->setConsts(true, toneMappingSystem->getToneMapper());

	return ResourceSystem::getInstance()->loadGraphicsPipeline(
		"bloom/downsample0", framebuffer, false, true, 0, 0, specConsts);
}
static ID<GraphicsPipeline> createDownsamplePipeline(ID<Framebuffer> framebuffer)
{
	return ResourceSystem::getInstance()->loadGraphicsPipeline(
		"bloom/downsample", framebuffer);
}
static ID<GraphicsPipeline> createUpsamplePipeline(ID<Framebuffer> framebuffer)
{
	return ResourceSystem::getInstance()->loadGraphicsPipeline(
		"bloom/upsample", framebuffer);
}

//--------------------------------------------------------------------------------------------------
void BloomRenderSystem::initialize()
{
	auto settingsSystem = Manager::getInstance()->tryGet<SettingsSystem>();
	if (settingsSystem)
		settingsSystem->getBool("useBloom", isEnabled);

	if (isEnabled)
	{
		auto graphicsSystem = getGraphicsSystem();
		auto framebufferSize = getDeferredSystem()->getFramebufferSize();

		if (!bloomBuffer)
		{
			bloomBuffer = createBloomBufferData(graphicsSystem,
				framebufferSize, imageViews, sizeBuffer);
		}
		if (framebuffers.empty())
		{
			createBloomFramebuffers(graphicsSystem,
				framebufferSize, imageViews, framebuffers);
		}

		if (!downsample0Pipeline)
		{
			downsample0Pipeline = createDownsample0Pipeline(
				framebuffers[0], useThreshold, useAntiFlickering);
		}
		if (!downsamplePipeline)
			downsamplePipeline = createDownsamplePipeline(framebuffers[0]);
		if (!upsamplePipeline)
			upsamplePipeline = createUpsamplePipeline(framebuffers[0]);
	}

	#if GARDEN_EDITOR
	editor = new BloomEditor(this);
	#endif
}
void BloomRenderSystem::terminate()
{
	#if GARDEN_EDITOR
	delete (BloomEditor*)editor;
	#endif
}

//--------------------------------------------------------------------------------------------------
void BloomRenderSystem::render()
{
	#if GARDEN_EDITOR
	((BloomEditor*)editor)->render();
	#endif
}

void BloomRenderSystem::preLdrRender()
{
	auto graphicsSystem = getGraphicsSystem();
	if (!isEnabled || intensity == 0.0f)
	{
		if (bloomBuffer)
		{
			auto imageView = graphicsSystem->get(bloomBuffer);
			imageView->clear(float4(0.0f));
		}
		return;
	}
	
	if (!bloomBuffer)
	{
		bloomBuffer = createBloomBufferData(graphicsSystem,
			getDeferredSystem()->getFramebufferSize(), imageViews, sizeBuffer);
	}
	if (framebuffers.empty())
	{
		createBloomFramebuffers(graphicsSystem,
			getDeferredSystem()->getFramebufferSize(), imageViews, framebuffers);
	}

	if (!downsample0Pipeline)
	{
		downsample0Pipeline = createDownsample0Pipeline(
			framebuffers[0], useThreshold, useAntiFlickering);
	}
	if (!downsamplePipeline)
		downsamplePipeline = createDownsamplePipeline(framebuffers[0]);
	if (!upsamplePipeline)
		upsamplePipeline = createUpsamplePipeline(framebuffers[0]);
	
	auto downsample0PipelineView = graphicsSystem->get(downsample0Pipeline);
	auto downsamplePipelineView = graphicsSystem->get(downsamplePipeline);
	auto upsamplePipelineView = graphicsSystem->get(upsamplePipeline);
	
	if (!downsample0PipelineView->isReady() || !downsamplePipelineView->isReady() ||
		!upsamplePipelineView->isReady() || !graphicsSystem->camera) return;

	if (descriptorSets.empty())
	{
		createBloomDescriptorSets(bloomBuffer, downsample0Pipeline,
			downsamplePipeline, upsamplePipeline, imageViews, descriptorSets);
	}
	
	auto mipCount = (uint8)imageViews.size();
	auto framebufferView = graphicsSystem->get(framebuffers[0]);

	SET_GPU_DEBUG_LABEL("Bloom", Color::transparent);
	{
		SET_GPU_DEBUG_LABEL("Downsample", Color::transparent);
		framebufferView->beginRenderPass(float4(0.0f));
		downsample0PipelineView->bind();
		downsample0PipelineView->setViewportScissor(float4(float2(0), sizeBuffer[1])); // TODO: try to use default float(0.0f) constructor and maybe remove sizeBuffer?
		downsample0PipelineView->bindDescriptorSet(descriptorSets[0]);
		auto pushConstants = downsample0PipelineView->getPushConstants<PushConstants>();
		pushConstants->threshold = threshold;
		downsample0PipelineView->pushConstants();
		downsample0PipelineView->drawFullscreen();
		framebufferView->endRenderPass();

		downsamplePipelineView->bind();
		for (uint8 i = 1; i < mipCount; i++)
		{
			framebufferView = graphicsSystem->get(framebuffers[i]);
			framebufferView->beginRenderPass(float4(0.0f));
			downsamplePipelineView->setViewportScissor(float4(float2(0), sizeBuffer[i + 1]));
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
			framebufferView->beginRenderPass(float4(0.0f));
			upsamplePipelineView->setViewportScissor(float4(float2(0), sizeBuffer[i + 1]));
			upsamplePipelineView->bindDescriptorSet(descriptorSets[mipCount + i]);
			upsamplePipelineView->drawFullscreen();
			framebufferView->endRenderPass();
		}
	}
}
void BloomRenderSystem::recreateSwapchain(const SwapchainChanges& changes)
{
	if (changes.framebufferSize)
	{
		auto graphicsSystem = getGraphicsSystem();
		auto framebufferSize = getDeferredSystem()->getFramebufferSize();

		if (!descriptorSets.empty())
		{
			for (auto descriptorSet : descriptorSets)
				graphicsSystem->destroy(descriptorSet);
			descriptorSets.clear();
		}

		if (!framebuffers.empty())
		{
			for (auto framebuffer : framebuffers)
				graphicsSystem->destroy(framebuffer);
		}
		if (!imageViews.empty())
		{
			for (auto imageView : imageViews)
				graphicsSystem->destroy(imageView);
		}

		if (bloomBuffer)
		{
			graphicsSystem->destroy(bloomBuffer);
			bloomBuffer = createBloomBufferData(graphicsSystem,
				framebufferSize, imageViews, sizeBuffer);
		}
		if (!framebuffers.empty())
		{
			createBloomFramebuffers(graphicsSystem,
				framebufferSize, imageViews, framebuffers);
		}

		if (downsample0Pipeline)
		{
			auto pipelineView = graphicsSystem->get(downsample0Pipeline);
			pipelineView->updateFramebuffer(framebuffers[0]);
		}
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

//--------------------------------------------------------------------------------------------------
void BloomRenderSystem::setConsts(bool useThreshold, bool useAntiFlickering)
{
	if ((this->useThreshold == useThreshold &&
		this->useAntiFlickering == useAntiFlickering)) return;
	this->useThreshold = useThreshold; this->useAntiFlickering = useAntiFlickering;
	if (!downsample0Pipeline)
		return;

	auto graphicsSystem = getGraphicsSystem();
	for (auto descriptorSet : descriptorSets)
		graphicsSystem->destroy(descriptorSet);
	descriptorSets.clear();
	graphicsSystem->destroy(downsample0Pipeline);

	downsample0Pipeline = createDownsample0Pipeline(
		getFramebuffers()[0], useThreshold, useAntiFlickering);
}

//--------------------------------------------------------------------------------------------------
ID<GraphicsPipeline> BloomRenderSystem::getDownsample0Pipeline()
{
	if (!downsample0Pipeline)
	{
		downsample0Pipeline = createDownsample0Pipeline(
			getFramebuffers()[0], useThreshold, useAntiFlickering);
	}
	return downsample0Pipeline;
}
ID<GraphicsPipeline> BloomRenderSystem::getDownsamplePipeline()
{
	if (!downsamplePipeline)
		downsamplePipeline = createDownsamplePipeline(getFramebuffers()[0]);
	return downsamplePipeline;
}
ID<GraphicsPipeline> BloomRenderSystem::getUpsamplePipeline()
{
	if (!upsamplePipeline)
		upsamplePipeline = createUpsamplePipeline(getFramebuffers()[0]);
	return upsamplePipeline;
}

//--------------------------------------------------------------------------------------------------
ID<Image> BloomRenderSystem::getBloomBuffer()
{
	if (!bloomBuffer)
	{
		bloomBuffer = createBloomBufferData(getGraphicsSystem(),
			getDeferredSystem()->getFramebufferSize(), imageViews, sizeBuffer);
	}
	return bloomBuffer;
}
const vector<ID<Framebuffer>>& BloomRenderSystem::getFramebuffers()
{
	if (framebuffers.empty())
	{
		createBloomFramebuffers(getGraphicsSystem(),
			getDeferredSystem()->getFramebufferSize(), imageViews, framebuffers);
	}
	return framebuffers;
}
*/