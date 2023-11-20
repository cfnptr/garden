//--------------------------------------------------------------------------------------------------
// Copyright 2022-2023 Nikita Fediuchin. All rights reserved.
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

#include "garden/system/graphics/tone-mapping.hpp"
#include "garden/system/graphics/editor/tone-mapping.hpp"
#include "garden/system/graphics/auto-exposure.hpp"
#include "garden/system/graphics/bloom.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

namespace
{
	struct PushConstants final
	{
		uint32 frameIndex;
		float exposureCoeff;
		float ditherStrength;
		float bloomStrength;
	};
}

//--------------------------------------------------------------------------------------------------
static map<string, DescriptorSet::Uniform> getUniforms(Manager* manager,
	GraphicsSystem* graphicsSystem, DeferredRenderSystem* deferredSystem)
{
	auto bloomSystem = manager->get<BloomRenderSystem>();
	auto autoExposureSystem = manager->get<AutoExposureRenderSystem>();
	auto hdrFramebufferView = graphicsSystem->get(deferredSystem->getHdrFramebuffer());
	auto bloomBufferView = graphicsSystem->get(bloomSystem->getBloomBuffer());

	map<string, DescriptorSet::Uniform> uniforms =
	{ 
		{ "hdrBuffer", DescriptorSet::Uniform(
			hdrFramebufferView->getColorAttachments()[0].imageView) },
		{ "bloomBuffer", DescriptorSet::Uniform(bloomBufferView->getDefaultView()) },
		{ "luminance", DescriptorSet::Uniform(autoExposureSystem->getLuminanceBuffer()) }
	};
	return uniforms;
}

//--------------------------------------------------------------------------------------------------
static ID<GraphicsPipeline> createPipeline(DeferredRenderSystem* deferredSystem)
{
	return ResourceSystem::getInstance()->loadGraphicsPipeline(
		"tone-mapping", deferredSystem->getLdrFramebuffer());
}

//--------------------------------------------------------------------------------------------------
void ToneMappingRenderSystem::initialize()
{
	auto manager = getManager();
	bloomSystem = manager->get<BloomRenderSystem>();
	autoExposureSystem = manager->get<AutoExposureRenderSystem>();
	if (!pipeline) pipeline = createPipeline(getDeferredSystem());

	#if GARDEN_EDITOR
	editor = new ToneMappingEditor(this);
	#endif
}
void ToneMappingRenderSystem::terminate()
{
	#if GARDEN_EDITOR
	delete (ToneMappingEditor*)editor;
	#endif
}

//--------------------------------------------------------------------------------------------------
void ToneMappingRenderSystem::render()
{
	#if GARDEN_EDITOR
	((ToneMappingEditor*)editor)->render();
	#endif
}
void ToneMappingRenderSystem::ldrRender()
{
	auto graphicsSystem = getGraphicsSystem();
	auto pipelineView = graphicsSystem->get(pipeline);
	if (!pipelineView->isReady()) return;

	auto deferredSystem = getDeferredSystem();
	if (!descriptorSet)
	{
		auto uniforms = getUniforms(getManager(), graphicsSystem, deferredSystem);
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, descriptorSet,
			"descriptorSet.deferred.toneMapping");
	}

	SET_GPU_DEBUG_LABEL("Tone Mapping", Color::transparent);
	pipelineView->bind();
	pipelineView->setViewportScissor(float4(float2(0),
		deferredSystem->getFramebufferSize()));
	pipelineView->bindDescriptorSet(descriptorSet);
	auto pushConstants = pipelineView->getPushConstants<PushConstants>();
	pushConstants->frameIndex = (uint32)graphicsSystem->getFrameIndex();
	pushConstants->exposureCoeff = exposureCoeff;
	pushConstants->ditherStrength = ditherStrength;
	pushConstants->bloomStrength = bloomSystem->strength;
	pipelineView->pushConstants();
	pipelineView->drawFullscreen();
}

//--------------------------------------------------------------------------------------------------
void ToneMappingRenderSystem::recreateSwapchain(const SwapchainChanges& changes)
{
	if (changes.framebufferSize && descriptorSet)
	{
		auto graphicsSystem = getGraphicsSystem();
		auto descriptorSetView = graphicsSystem->get(descriptorSet);
		auto uniforms = getUniforms(getManager(), graphicsSystem, getDeferredSystem());
		descriptorSetView->recreate(std::move(uniforms));
	}
}

//--------------------------------------------------------------------------------------------------
ID<GraphicsPipeline> ToneMappingRenderSystem::getPipeline()
{
	if (!pipeline) pipeline = createPipeline(getDeferredSystem());
	return pipeline;
}