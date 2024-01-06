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

#include "garden/system/graphics/fxaa.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/settings.hpp"

using namespace garden;

namespace
{
	struct PushConstants final
	{
		float2 invFrameSize;
	};
}

//--------------------------------------------------------------------------------------------------
static ID<GraphicsPipeline> createPipeline(Manager* manager, GraphicsSystem* graphicsSystem)
{
	auto deferredSystem = manager->get<DeferredRenderSystem>();
	deferredSystem->runSwapchainPass = false;

	return ResourceSystem::getInstance()->loadGraphicsPipeline(
		"fxaa", graphicsSystem->getSwapchainFramebuffer());
}
static map<string, DescriptorSet::Uniform> getUniforms(
	GraphicsSystem* graphicsSystem, DeferredRenderSystem* deferredSystem)
{
	auto hdrFramebufferView = graphicsSystem->get(deferredSystem->getHdrFramebuffer());
	auto ldrFramebufferView = graphicsSystem->get(deferredSystem->getLdrFramebuffer());
	map<string, DescriptorSet::Uniform> uniforms =
	{ 
		{ "hdrBuffer", DescriptorSet::Uniform(
			hdrFramebufferView->getColorAttachments()[0].imageView) },
		{ "ldrBuffer", DescriptorSet::Uniform(
			ldrFramebufferView->getColorAttachments()[0].imageView) },
	};
	return uniforms;
}

//--------------------------------------------------------------------------------------------------
void FxaaRenderSystem::initialize()
{
	auto manager = getManager();
	auto settingsSystem = manager->tryGet<SettingsSystem>();
	if (settingsSystem) settingsSystem->getBool("useFXAA", isEnabled);

	if (isEnabled)
	{
		if (!pipeline) pipeline = createPipeline(manager, getGraphicsSystem());
	}
}

//--------------------------------------------------------------------------------------------------
void FxaaRenderSystem::preSwapchainRender()
{
	if (!isEnabled) return;
	
	auto graphicsSystem = getGraphicsSystem();
	if (!pipeline) pipeline = createPipeline(getManager(), graphicsSystem);

	auto pipelineView = graphicsSystem->get(pipeline);
	if (!pipelineView->isReady()) return;

	if (!descriptorSet)
	{
		auto uniforms = getUniforms(graphicsSystem, getDeferredSystem());
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, descriptorSet, "descriptorSet.fxaa");
	}

	auto framebufferView = graphicsSystem->get(
		graphicsSystem->getSwapchainFramebuffer());

	SET_GPU_DEBUG_LABEL("FXAA", Color::transparent);
	framebufferView->beginRenderPass(float4(0.0f));
	pipelineView->bind();
	pipelineView->setViewportScissor(float4(
		float2(0), framebufferView->getSize()));
	pipelineView->bindDescriptorSet(descriptorSet);
	auto pushConstants = pipelineView->getPushConstants<PushConstants>();
	pushConstants->invFrameSize = float2(1.0f) / framebufferView->getSize();
	pipelineView->pushConstants();
	pipelineView->drawFullscreen();
	framebufferView->endRenderPass();
}

//--------------------------------------------------------------------------------------------------
void FxaaRenderSystem::recreateSwapchain(const SwapchainChanges& changes)
{
	if (changes.framebufferSize && descriptorSet)
	{
		auto graphicsSystem = getGraphicsSystem();
		auto descriptorSetView = graphicsSystem->get(descriptorSet);
		auto uniforms = getUniforms(graphicsSystem, getDeferredSystem());
		descriptorSetView->recreate(std::move(uniforms));
	}
}

//--------------------------------------------------------------------------------------------------
ID<GraphicsPipeline> FxaaRenderSystem::getPipeline()
{
	if (!pipeline) pipeline = createPipeline(getManager(), getGraphicsSystem());
	return pipeline;
}