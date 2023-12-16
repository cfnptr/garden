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

static ID<Buffer> createLuminanceBuffer(GraphicsSystem* graphicsSystem)
{
	#if GARDEN_EDITOR
	const auto bind = Buffer::Bind::TransferSrc;
	#else
	const auto bind = Buffer::Bind::None;
	#endif

	const float data[2] = { 1.0f / LUM_TO_EXP, 1.0f };
	
	auto buffer = graphicsSystem->createBuffer(Buffer::Bind::Storage |
		Buffer::Bind::Uniform | Buffer::Bind::TransferDst | bind,
		Buffer::Access::None, data, sizeof(ToneMappingRenderSystem::Luminance),
		Buffer::Usage::PreferGPU, Buffer::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, buffer, "buffer.toneMapping.luminance");
	return buffer;
}

//--------------------------------------------------------------------------------------------------
static map<string, DescriptorSet::Uniform> getUniforms(
	GraphicsSystem* graphicsSystem, DeferredRenderSystem* deferredSystem,
	BloomRenderSystem* bloomSystem, ID<Buffer> luminanceBuffer)
{
	auto hdrFramebufferView = graphicsSystem->get(deferredSystem->getHdrFramebuffer());

	ID<ImageView> bloomBuffer;
	if (bloomSystem)
	{
		auto bloomBufferView = graphicsSystem->get(bloomSystem->getBloomBuffer());
		bloomBuffer = bloomBufferView->getDefaultView();
	}
	else
	{
		auto emptyTextureView = graphicsSystem->get(graphicsSystem->getEmptyTexture());
		bloomBuffer = emptyTextureView->getDefaultView();
	}

	map<string, DescriptorSet::Uniform> uniforms =
	{ 
		{ "hdrBuffer", DescriptorSet::Uniform(
			hdrFramebufferView->getColorAttachments()[0].imageView) },
		{ "bloomBuffer", DescriptorSet::Uniform(bloomBuffer) },
		{ "luminance", DescriptorSet::Uniform(luminanceBuffer) }
	};
	return uniforms;
}

static ID<GraphicsPipeline> createPipeline(DeferredRenderSystem* deferredSystem)
{
	return ResourceSystem::getInstance()->loadGraphicsPipeline(
		"tone-mapping", deferredSystem->getLdrFramebuffer());
}

//--------------------------------------------------------------------------------------------------
void ToneMappingRenderSystem::initialize()
{
	auto manager = getManager();
	bloomSystem = manager->tryGet<BloomRenderSystem>();
	
	if (!luminanceBuffer) luminanceBuffer = createLuminanceBuffer(getGraphicsSystem());
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

void ToneMappingRenderSystem::render()
{
	#if GARDEN_EDITOR
	auto graphicsSystem = getGraphicsSystem();
	graphicsSystem->startRecording(CommandBufferType::Frame);
	((ToneMappingEditor*)editor)->render();
	graphicsSystem->stopRecording();
	#endif
}
void ToneMappingRenderSystem::preLdrRender()
{
	if (!descriptorSet)
	{
		auto graphicsSystem = getGraphicsSystem();
		auto pipelineView = graphicsSystem->get(pipeline);
		if (!pipelineView->isReady()) return;
		
		auto uniforms = getUniforms(graphicsSystem,
			getDeferredSystem(), bloomSystem, luminanceBuffer);
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, descriptorSet,
			"descriptorSet.deferred.toneMapping");
	}
}
void ToneMappingRenderSystem::ldrRender()
{
	auto graphicsSystem = getGraphicsSystem();
	auto pipelineView = graphicsSystem->get(pipeline);
	auto luminanceBufferView = graphicsSystem->get(luminanceBuffer);
	if (!pipelineView->isReady() || !luminanceBufferView->isReady()) return;

	SET_GPU_DEBUG_LABEL("Tone Mapping", Color::transparent);
	pipelineView->bind();
	pipelineView->setViewportScissor(float4(float2(0),
		getDeferredSystem()->getFramebufferSize()));
	pipelineView->bindDescriptorSet(descriptorSet);
	auto pushConstants = pipelineView->getPushConstants<PushConstants>();
	pushConstants->frameIndex = (uint32)graphicsSystem->getFrameIndex();
	pushConstants->exposureCoeff = exposureCoeff;
	pushConstants->ditherStrength = ditherStrength;
	pushConstants->bloomStrength = bloomSystem ? bloomSystem->strength : 0.0f;
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
		auto uniforms = getUniforms(graphicsSystem,
			getDeferredSystem(), bloomSystem, luminanceBuffer);
		descriptorSetView->recreate(std::move(uniforms));
	}
}

ID<GraphicsPipeline> ToneMappingRenderSystem::getPipeline()
{
	if (!pipeline) pipeline = createPipeline(getDeferredSystem());
	return pipeline;
}
ID<Buffer> ToneMappingRenderSystem::getLuminanceBuffer()
{
	if (!luminanceBuffer) luminanceBuffer = createLuminanceBuffer(getGraphicsSystem());
	return luminanceBuffer;
}

void ToneMappingRenderSystem::setLuminance(float luminance)
{
	auto exposure = 1.0f / (luminance * LUM_TO_EXP + 0.0001f);
	auto luminanceBufferView = getGraphicsSystem()->get(luminanceBuffer);
	luminanceBufferView->fill(*((uint32*)&luminance), sizeof(float), 0);
	luminanceBufferView->fill(*((uint32*)&exposure), sizeof(float), sizeof(float));
}
void ToneMappingRenderSystem::setExposure(float exposure)
{
	auto luminance = (1.0f / exposure) * (1.0f / LUM_TO_EXP) - 0.0001f;
	auto luminanceBufferView = getGraphicsSystem()->get(luminanceBuffer);
	luminanceBufferView->fill(*((uint32*)&luminance), sizeof(float), 0);
	luminanceBufferView->fill(*((uint32*)&exposure), sizeof(float), sizeof(float));
}