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

#include "garden/system/render/fxaa.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/settings.hpp"
#include "garden/profiler.hpp"

using namespace garden;

static ID<GraphicsPipeline> createPipeline()
{
	return ResourceSystem::Instance::get()->loadGraphicsPipeline(
		"fxaa", GraphicsSystem::Instance::get()->getSwapchainFramebuffer());
}
static map<string, DescriptorSet::Uniform> getUniforms()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto hdrFramebufferView = graphicsSystem->get(deferredSystem->getHdrFramebuffer());
	auto ldrFramebufferView = graphicsSystem->get(deferredSystem->getLdrFramebuffer());

	// TODO: support forward rendering too

	map<string, DescriptorSet::Uniform> uniforms =
	{ 
		{ "hdrBuffer", DescriptorSet::Uniform(hdrFramebufferView->getColorAttachments()[0].imageView) },
		{ "ldrBuffer", DescriptorSet::Uniform(ldrFramebufferView->getColorAttachments()[0].imageView) },
	};
	return uniforms;
}

//**********************************************************************************************************************
FxaaRenderSystem::FxaaRenderSystem(bool setSingleton) : Singleton(setSingleton)
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", FxaaRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", FxaaRenderSystem::deinit);
}
FxaaRenderSystem::~FxaaRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", FxaaRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", FxaaRenderSystem::deinit);
	}

	unsetSingleton();
}

void FxaaRenderSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("PreSwapchainRender", FxaaRenderSystem::preSwapchainRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", FxaaRenderSystem::gBufferRecreate);

	auto settingsSystem = SettingsSystem::Instance::tryGet();
	if (settingsSystem)
		settingsSystem->getBool("fxaa.isEnabled", isEnabled);

	if (isEnabled)
	{
		if (!pipeline)
			pipeline = createPipeline();
	}

	DeferredRenderSystem::Instance::get()->runSwapchainPass = false;
}
void FxaaRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		GraphicsSystem::Instance::get()->destroy(pipeline);

		ECSM_UNSUBSCRIBE_FROM_EVENT("PreSwapchainRender", FxaaRenderSystem::preSwapchainRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", FxaaRenderSystem::gBufferRecreate);
	}
}

//**********************************************************************************************************************
void FxaaRenderSystem::preSwapchainRender()
{
	SET_CPU_ZONE_SCOPED("FXAA Pre Swapchain Render");

	if (!isEnabled)
		return;
	
	if (!pipeline)
		pipeline = createPipeline();

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(pipeline);
	if (!pipelineView->isReady())
		return;

	if (!descriptorSet)
	{
		auto uniforms = getUniforms();
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.fxaa");
	}

	auto framebufferView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());
	auto pushConstants = pipelineView->getPushConstants<PushConstants>();
	pushConstants->invFrameSize = float2(1.0f) / graphicsSystem->getFramebufferSize();

	SET_GPU_DEBUG_LABEL("FXAA", Color::transparent);
	framebufferView->beginRenderPass(float4(0.0f));
	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->bindDescriptorSet(descriptorSet);
	pipelineView->pushConstants();
	pipelineView->drawFullscreen();
	framebufferView->endRenderPass();
}

//**********************************************************************************************************************
void FxaaRenderSystem::gBufferRecreate()
{
	if (descriptorSet)
	{
		auto descriptorSetView = GraphicsSystem::Instance::get()->get(descriptorSet);
		auto uniforms = getUniforms();
		descriptorSetView->recreate(std::move(uniforms));
	}
}

ID<GraphicsPipeline> FxaaRenderSystem::getPipeline()
{
	if (!pipeline)
		pipeline = createPipeline();
	return pipeline;
}