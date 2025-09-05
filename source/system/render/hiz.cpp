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

#include "garden/system/render/hiz.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/resource.hpp"
#include "garden/profiler.hpp"
#include "hiz/variants.h"

using namespace garden;

static ID<Image> createHizBuffer(GraphicsSystem* graphicsSystem, vector<ID<ImageView>>& imageViews)
{
	auto hizBufferSize =  graphicsSystem->getScaledFrameSize();
	auto mipCount = calcMipCount(hizBufferSize);
	imageViews.resize(mipCount);

	Image::Mips mips(mipCount);
	for (uint8 i = 0; i < mipCount; i++)
		mips[i].resize(1);

	auto image = graphicsSystem->createImage(HizRenderSystem::bufferFormat, Image::Usage::ColorAttachment | 
		Image::Usage::Sampled | Image::Usage::TransferDst, mips, hizBufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.hiz.buffer");

	for (uint8 i = 0; i < mipCount; i++)
	{
		auto imageView = graphicsSystem->createImageView(image, 
			Image::Type::Texture2D, HizRenderSystem::bufferFormat, i, 1, 0, 1);
		SET_RESOURCE_DEBUG_NAME(imageView, "imageView.hiz.buffer" + to_string(i));
		imageViews[i] = imageView;
	}
	return image;
}

static void createHizFramebuffers(GraphicsSystem* graphicsSystem, 
	const vector<ID<ImageView>>& imageViews, vector<ID<Framebuffer>>& framebuffers)
{
	auto framebufferSize = graphicsSystem->getScaledFrameSize();
	auto mipCount = (uint8)imageViews.size();
	framebuffers.resize(mipCount);

	for (uint8 i = 0; i < mipCount; i++)
	{
		vector<Framebuffer::OutputAttachment> colorAttachments =
		{ Framebuffer::OutputAttachment(imageViews[i], { false, false, true }) };
		auto framebuffer = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
		SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.hiz" + to_string(i));
		framebuffers[i] = framebuffer;
		framebufferSize = max(framebufferSize / 2u, uint2::one);
	}
}

static DescriptorSet::Uniforms getUniforms(ID<ImageView> srcBuffer)
{
	return { { "srcBuffer", DescriptorSet::Uniform(srcBuffer) } };
}
static void createHizDescriptorSets(GraphicsSystem* graphicsSystem, ID<GraphicsPipeline> pipeline,  
	const vector<ID<ImageView>>& imageViews, vector<ID<DescriptorSet>>& descriptorSets)
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto mipCount = (uint8)imageViews.size();
	descriptorSets.resize(mipCount);

	auto uniforms = getUniforms(deferredSystem->getDepthImageView());
	auto descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
	SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.hiz0");
	descriptorSets[0] = descriptorSet;

	for (uint8 i = 1; i < mipCount; i++)
	{
		uniforms = getUniforms(imageViews[i - 1]);
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.hiz" + to_string(i));
		descriptorSets[i] = descriptorSet;
	}
}

static ID<GraphicsPipeline> createPipeline(ID<Framebuffer> framebuffer)
{
	ResourceSystem::GraphicsOptions options;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("hiz", framebuffer, options);
}

//**********************************************************************************************************************
HizRenderSystem::HizRenderSystem(bool setSingleton) : Singleton(setSingleton)
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", HizRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", HizRenderSystem::deinit);
}
HizRenderSystem::~HizRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", HizRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", HizRenderSystem::deinit);
	}

	unsetSingleton();
}

void HizRenderSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("PreHdrRender", HizRenderSystem::preHdrRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", HizRenderSystem::gBufferRecreate);
}
void HizRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(pipeline);
		graphicsSystem->destroy(framebuffers);
		graphicsSystem->destroy(imageViews);
		graphicsSystem->destroy(hizBuffer);

		ECSM_UNSUBSCRIBE_FROM_EVENT("PreHdrRender", HizRenderSystem::preHdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", HizRenderSystem::gBufferRecreate);
	}
}

void HizRenderSystem::downsampleHiz(uint8 levelCount)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!isEnabled)
	{
		if (hizBuffer)
		{
			auto imageView = graphicsSystem->get(hizBuffer);
			imageView->clear(float4::zero);
		}
		return;
	}

	if (!isInitialized)
	{
		if (!hizBuffer)
			hizBuffer = createHizBuffer(graphicsSystem, imageViews);
		if (framebuffers.empty())
			createHizFramebuffers(graphicsSystem, imageViews, framebuffers);
		if (!pipeline)
			pipeline = createPipeline(framebuffers[0]);
		isInitialized = true;
	}

	auto pipelineView = graphicsSystem->get(pipeline);
	if (!pipelineView->isReady())
		return;

	if (descriptorSets.empty())
		createHizDescriptorSets(graphicsSystem, pipeline, imageViews, descriptorSets);
	
	auto framebufferView = graphicsSystem->get(framebuffers[0]);
	pipelineView->updateFramebuffer(framebuffers[0]);

	if (levelCount > (uint8)imageViews.size())
		levelCount = (uint8)imageViews.size();

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		SET_GPU_DEBUG_LABEL("HiZ Downsample");
		{
			RenderPass renderPass(framebuffers[0], float4::zero);
			pipelineView->bind(HIZ_VARIANT_FIRST);
			pipelineView->setViewportScissor();
			pipelineView->bindDescriptorSet(descriptorSets[0]);
			pipelineView->drawFullscreen();
		}
		
		for (uint8 i = 1; i < levelCount; i++)
		{
			pipelineView->updateFramebuffer(framebuffers[i]);

			RenderPass renderPass(framebuffers[i], float4::zero);
			pipelineView->bind(HIZ_VARIANT_BASE);
			pipelineView->setViewportScissor();
			pipelineView->bindDescriptorSet(descriptorSets[i]);
			pipelineView->drawFullscreen();
		}
	}
	graphicsSystem->stopRecording();
}

//**********************************************************************************************************************
void HizRenderSystem::preHdrRender()
{
	SET_CPU_ZONE_SCOPED("HiZ Pre HDR Render");
	downsampleHiz(UINT8_MAX);
}

void HizRenderSystem::gBufferRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	graphicsSystem->destroy(descriptorSets);
	descriptorSets.clear();

	if (hizBuffer)
	{
		graphicsSystem->destroy(hizBuffer);
		graphicsSystem->destroy(imageViews);
		hizBuffer = createHizBuffer(graphicsSystem, imageViews);
	}
	if (!framebuffers.empty())
	{
		graphicsSystem->destroy(framebuffers);
		createHizFramebuffers(graphicsSystem, imageViews, framebuffers);

		if (pipeline)
		{
			auto pipelineView = graphicsSystem->get(pipeline);
			if (pipelineView->isReady())
				pipelineView->updateFramebuffer(framebuffers[0]);
		}
	}
}

ID<GraphicsPipeline> HizRenderSystem::getPipeline()
{
	if (!pipeline)
		pipeline = createPipeline(getFramebuffers()[0]);
	return pipeline;
}
ID<Image> HizRenderSystem::getHizBuffer()
{
	if (!hizBuffer)
		hizBuffer = createHizBuffer(GraphicsSystem::Instance::get(), imageViews);
	return hizBuffer;
}
const vector<ID<ImageView>>& HizRenderSystem::getImageViews()
{
	if (!hizBuffer)
		hizBuffer = createHizBuffer(GraphicsSystem::Instance::get(), imageViews);
	return imageViews;
}
const vector<ID<Framebuffer>>& HizRenderSystem::getFramebuffers()
{
	if (framebuffers.empty())
		createHizFramebuffers(GraphicsSystem::Instance::get(), imageViews, framebuffers);
	return framebuffers;
}
