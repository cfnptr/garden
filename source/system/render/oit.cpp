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

#include "garden/system/render/oit.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/resource.hpp"
#include "garden/profiler.hpp"

using namespace garden;

//**********************************************************************************************************************
static ID<Framebuffer> createFramebuffer(GraphicsSystem* graphicsSystem, DeferredRenderSystem* deferredSystem)
{
	vector<Framebuffer::Attachment> colorAttachments =
	{
		Framebuffer::Attachment(deferredSystem->getHdrImageView())
	};
	Framebuffer::Attachment depthStencilAttachment(deferredSystem->getDepthStencilIV(),
		Framebuffer::LoadOp::Load, Framebuffer::StoreOp::None);
	auto framebuffer = graphicsSystem->createFramebuffer(graphicsSystem->getScaledFrameSize(), 
		std::move(colorAttachments), depthStencilAttachment);
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.oit.compose");
	return framebuffer;
}
static ID<GraphicsPipeline> createPipeline(ID<Framebuffer> framebuffer)
{
	ResourceSystem::GraphicsOptions options;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("oit", framebuffer, options);
}
static DescriptorSet::Uniforms getUniforms(GraphicsSystem* graphicsSystem, DeferredRenderSystem* deferredSystem)
{
	auto accumBufferView = deferredSystem->getOitAccumIV();
	auto revealBufferView = deferredSystem->getOitRevealIV();

	DescriptorSet::Uniforms uniforms =
	{ 
		{ "accumBuffer", DescriptorSet::Uniform(accumBufferView) },
		{ "revealBuffer", DescriptorSet::Uniform(revealBufferView) },
	};
	return uniforms;
}

OitRenderSystem::OitRenderSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", OitRenderSystem::init);
}
void OitRenderSystem::init()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("PreLdrRender", OitRenderSystem::preLdrRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", OitRenderSystem::gBufferRecreate);
}

//**********************************************************************************************************************
void OitRenderSystem::preLdrRender()
{
	SET_CPU_ZONE_SCOPED("OIT Pre LDR Render");

	if (!isEnabled)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	if (!graphicsSystem->camera || !deferredSystem->hasAnyOIT())
		return;

	if (!isInitialized)
	{
		if (!framebuffer)
			framebuffer = createFramebuffer(graphicsSystem, deferredSystem);
		if (!pipeline)
			pipeline = createPipeline(framebuffer);
	}

	auto pipelineView = graphicsSystem->get(pipeline);
	if (!pipelineView->isReady())
		return;

	if (!descriptorSet)
	{
		auto uniforms = getUniforms(graphicsSystem, deferredSystem);
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.oit");
	}

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		SET_GPU_DEBUG_LABEL("OIT Compose");
		{
			RenderPass renderPass(framebuffer, float4::zero);
			pipelineView->bind();
			pipelineView->setViewportScissor();
			pipelineView->bindDescriptorSet(descriptorSet);
			pipelineView->drawFullscreen();
		}
	}
	graphicsSystem->stopRecording();
}

//**********************************************************************************************************************
void OitRenderSystem::gBufferRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	graphicsSystem->destroy(descriptorSet);

	if (framebuffer)
	{
		auto deferredSystem = DeferredRenderSystem::Instance::get();
		auto framebufferView = graphicsSystem->get(framebuffer);
		framebufferView->update(graphicsSystem->getScaledFrameSize(), 
			deferredSystem->getHdrImageView(), deferredSystem->getDepthStencilIV());
	}
}

ID<Framebuffer> OitRenderSystem::getFramebuffer()
{
	if (!framebuffer)
		framebuffer = createFramebuffer(GraphicsSystem::Instance::get(), DeferredRenderSystem::Instance::get());
	return framebuffer;
}
ID<GraphicsPipeline> OitRenderSystem::getPipeline()
{
	if (!pipeline)
		pipeline = createPipeline(getFramebuffer());
	return pipeline;
}