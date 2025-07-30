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

#include "garden/system/render/oit.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/resource.hpp"
#include "garden/profiler.hpp"

using namespace garden;

static ID<GraphicsPipeline> createPipeline()
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	ResourceSystem::GraphicsOptions options;
	auto skyboxPipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline(
		"oit", deferredSystem->getHdrFramebuffer(), options);
	return skyboxPipeline;
}
static DescriptorSet::Uniforms getUniforms()
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto oitFramebufferView = GraphicsSystem::Instance::get()->get(deferredSystem->getOitFramebuffer());
	const auto& colorAttachments = oitFramebufferView->getColorAttachments();

	DescriptorSet::Uniforms uniforms =
	{ 
		{ "accumBuffer", DescriptorSet::Uniform(colorAttachments[0].imageView) },
		{ "revealBuffer", DescriptorSet::Uniform(colorAttachments[1].imageView) },
	};
	return uniforms;
}

OitRenderSystem::OitRenderSystem(bool setSingleton) : Singleton(setSingleton)
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", OitRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", OitRenderSystem::deinit);
}
OitRenderSystem::~OitRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", OitRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", OitRenderSystem::deinit);
	}

	unsetSingleton();
}

//**********************************************************************************************************************
void OitRenderSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("PreLdrRender", OitRenderSystem::preLdrRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", OitRenderSystem::gBufferRecreate);
}
void OitRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(descriptorSet);
		graphicsSystem->destroy(pipeline);

		ECSM_UNSUBSCRIBE_FROM_EVENT("PreLdrRender", OitRenderSystem::preLdrRender);
		ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", OitRenderSystem::gBufferRecreate);
	}
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

	if (!pipeline)
		pipeline = createPipeline();

	auto pipelineView = graphicsSystem->get(pipeline);
	if (!pipelineView->isReady())
		return;

	if (!descriptorSet)
	{
		auto uniforms = getUniforms();
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.oit");
	}

	auto framebufferView = graphicsSystem->get(deferredSystem->getHdrFramebuffer());

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		SET_GPU_DEBUG_LABEL("OIT Compose", Color::transparent);
		framebufferView->beginRenderPass(float4::zero);
		pipelineView->bind();
		pipelineView->setViewportScissor();
		pipelineView->bindDescriptorSet(descriptorSet);
		pipelineView->drawFullscreen();
		framebufferView->endRenderPass();
	}
	graphicsSystem->stopRecording();
}

void OitRenderSystem::gBufferRecreate()
{
	if (descriptorSet)
	{
		GraphicsSystem::Instance::get()->destroy(descriptorSet);
		descriptorSet = {};
	}
}

ID<GraphicsPipeline> OitRenderSystem::getPipeline()
{
	if (!pipeline)
		pipeline = createPipeline();
	return pipeline;
}