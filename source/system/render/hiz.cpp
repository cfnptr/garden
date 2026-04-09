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

#include "garden/system/render/hiz.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/resource.hpp"
#include "garden/profiler.hpp"
#include "hiz/variants.h"

using namespace garden;

//**********************************************************************************************************************
static ID<Image> createHizBuffer(GraphicsSystem* graphicsSystem)
{
	auto frameSize = graphicsSystem->getScaledFrameSize();
	auto mipCount = calcMipCount(frameSize);

	Image::Mips mips(mipCount);
	for (auto& mip : mips) mip.resize(1);

	auto image = graphicsSystem->createImage(HizRenderSystem::bufferFormat, Image::Usage::ColorAttachment | 
		Image::Usage::Sampled | Image::Usage::TransferDst, mips, frameSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.hiz.buffer");
	return image;
}

static void createHizFramebuffers(GraphicsSystem* graphicsSystem, 
	ID<Image> hizBuffer, vector<ID<Framebuffer>>& framebuffers)
{
	auto frameSize = graphicsSystem->getScaledFrameSize();
	auto hisBufferView = graphicsSystem->get(hizBuffer);
	auto mipCount = hisBufferView->getMipCount();
	framebuffers.resize(mipCount); auto framebufferData = framebuffers.data();

	for (uint8 i = 0; i < mipCount; i++)
	{
		vector<Framebuffer::Attachment> colorAttachments =
		{
			Framebuffer::Attachment(hisBufferView->getView(0, i), 
				Framebuffer::LoadOp::DontCare, Framebuffer::StoreOp::Store)
		};
		auto framebuffer = graphicsSystem->createFramebuffer(frameSize, std::move(colorAttachments));
		SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.hiz" + to_string(i));
		framebufferData[i] = framebuffer; frameSize = max(frameSize / 2u, uint2::one);
	}
}

static DescriptorSet::Uniforms getUniforms(ID<ImageView> srcBuffer)
{
	return { { "srcBuffer", DescriptorSet::Uniform(srcBuffer) } };
}
static void createHizDescriptorSets(GraphicsSystem* graphicsSystem, ID<GraphicsPipeline> pipeline,  
	ID<Image> hizBuffer, vector<ID<DescriptorSet>>& descriptorSets)
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto hisBufferView = graphicsSystem->get(hizBuffer);
	auto mipCount = hisBufferView->getMipCount();
	descriptorSets.resize(mipCount); auto descriptorSetData = descriptorSets.data();

	auto uniforms = getUniforms(deferredSystem->getDepthOnlyIV());
	auto descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
	SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.hiz0");
	descriptorSetData[0] = descriptorSet;

	for (uint8 i = 1; i < mipCount; i++)
	{
		uniforms = getUniforms(hisBufferView->getView(0, i - 1));
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.hiz" + to_string(i));
		descriptorSetData[i] = descriptorSet;
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
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", HizRenderSystem::init);
}
void HizRenderSystem::init()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("PreHdrRender", HizRenderSystem::preHdrRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", HizRenderSystem::gBufferRecreate);
}

void HizRenderSystem::downsampleHiz(uint8 mipCount)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!isEnabled)
	{
		if (hizBuffer)
		{
			auto hizBufferView = graphicsSystem->get(hizBuffer);
			hizBufferView->clear(float4::zero);
		}
		return;
	}

	if (!isInitialized)
	{
		if (!hizBuffer)
			hizBuffer = createHizBuffer(graphicsSystem);
		if (framebuffers.empty())
			createHizFramebuffers(graphicsSystem, hizBuffer, framebuffers);
		if (!pipeline)
			pipeline = createPipeline(framebuffers[0]);
		isInitialized = true;
	}

	auto pipelineView = graphicsSystem->get(pipeline);
	if (!pipelineView->isReady())
		return;

	if (descriptorSets.empty())
		createHizDescriptorSets(graphicsSystem, pipeline, hizBuffer, descriptorSets);

	auto framebufferData = framebuffers.data();
	auto descriptorSetData = descriptorSets.data();
	auto framebufferView = graphicsSystem->get(framebufferData[0]);
	pipelineView->updateFramebuffer(framebufferData[0]);

	auto hizBufferView = graphicsSystem->get(hizBuffer);
	if (mipCount > hizBufferView->getMipCount())
		mipCount = hizBufferView->getMipCount();

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		SET_GPU_DEBUG_LABEL("HiZ Downsample");
		{
			RenderPass renderPass(framebufferData[0], float4::zero);
			pipelineView->bind(HIZ_VARIANT_FIRST);
			pipelineView->setViewportScissor();
			pipelineView->bindDescriptorSet(descriptorSets[0]);
			pipelineView->drawFullscreen();
		}
		
		for (uint8 i = 1; i < mipCount; i++)
		{
			pipelineView->updateFramebuffer(framebufferData[i]);

			RenderPass renderPass(framebufferData[i], float4::zero);
			pipelineView->bind(HIZ_VARIANT_BASE);
			pipelineView->setViewportScissor();
			pipelineView->bindDescriptorSet(descriptorSetData[i]);
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
	graphicsSystem->destroy(descriptorSets); descriptorSets.clear();

	if (hizBuffer)
	{
		graphicsSystem->destroy(hizBuffer);
		hizBuffer = createHizBuffer(graphicsSystem);
	}
	if (!framebuffers.empty())
	{
		graphicsSystem->destroy(framebuffers);
		createHizFramebuffers(graphicsSystem, getHizBuffer(), framebuffers);

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
		hizBuffer = createHizBuffer(GraphicsSystem::Instance::get());
	return hizBuffer;
}
const vector<ID<Framebuffer>>& HizRenderSystem::getFramebuffers()
{
	if (framebuffers.empty())
		createHizFramebuffers(GraphicsSystem::Instance::get(), getHizBuffer(), framebuffers);
	return framebuffers;
}
