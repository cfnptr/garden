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

#include "garden/system/render/tone-mapping.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/render/bloom.hpp"
#include "garden/system/resource.hpp"
#include "garden/profiler.hpp"

using namespace garden;

static ID<Buffer> createLuminanceBuffer()
{
	#if GARDEN_EDITOR
	constexpr auto usage = Buffer::Usage::TransferSrc;
	#else
	constexpr auto usage = Buffer::Usage::None;
	#endif

	constexpr float data[2] = { 1.0f / ToneMappingRenderSystem::lumToExp, 1.0f };
	auto buffer = GraphicsSystem::Instance::get()->createBuffer(Buffer::Usage::Storage | 
		Buffer::Usage::Uniform | Buffer::Usage::TransferDst | usage, Buffer::CpuAccess::None, data, 
		sizeof(ToneMappingRenderSystem::LuminanceData), Buffer::Location::PreferGPU, Buffer::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(buffer, "buffer.toneMapping.luminance");
	return buffer;
}

//**********************************************************************************************************************
static DescriptorSet::Uniforms getUniforms(ID<Buffer> luminanceBuffer, bool useBloomBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto bloomSystem = BloomRenderSystem::Instance::tryGet();
	auto hdrFramebufferView = graphicsSystem->get(deferredSystem->getHdrFramebuffer());

	ID<ImageView> bloomBufferView;
	if (bloomSystem && useBloomBuffer)
	{
		auto bloomBuffer = bloomSystem->getBloomBuffer();
		bloomBufferView = graphicsSystem->get(bloomBuffer)->getDefaultView();
	}
	else
	{
		bloomBufferView = graphicsSystem->getEmptyTexture();
	}

	DescriptorSet::Uniforms uniforms =
	{ 
		{ "hdrBuffer", DescriptorSet::Uniform(hdrFramebufferView->getColorAttachments()[0].imageView) },
		{ "bloomBuffer", DescriptorSet::Uniform(bloomBufferView) },
		{ "luminance", DescriptorSet::Uniform(luminanceBuffer) }
	};
	return uniforms;
}

static ID<GraphicsPipeline> createPipeline(bool useBloomBuffer, ToneMapper toneMapper)
{
	Pipeline::SpecConstValues specConsts =
	{
		{ "USE_BLOOM_BUFFER", Pipeline::SpecConstValue(useBloomBuffer) },
		{ "TONE_MAPPER", Pipeline::SpecConstValue((uint32)toneMapper) }
	};

	auto deferredSystem = DeferredRenderSystem::Instance::get();
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("tone-mapping",
		deferredSystem->getLdrFramebuffer(), deferredSystem->useAsyncRecording(), true, 0, 0, &specConsts);
}

//**********************************************************************************************************************
ToneMappingRenderSystem::ToneMappingRenderSystem(bool useBloomBuffer, ToneMapper toneMapper, bool setSingleton) :
	Singleton(setSingleton), useBloomBuffer(useBloomBuffer), toneMapper(toneMapper)
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", ToneMappingRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", ToneMappingRenderSystem::deinit);
}
ToneMappingRenderSystem::~ToneMappingRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", ToneMappingRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", ToneMappingRenderSystem::deinit);
	}

	unsetSingleton();
}

void ToneMappingRenderSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("PreLdrRender", ToneMappingRenderSystem::preLdrRender);
	ECSM_SUBSCRIBE_TO_EVENT("LdrRender", ToneMappingRenderSystem::ldrRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", ToneMappingRenderSystem::gBufferRecreate);

	if (!luminanceBuffer)
		luminanceBuffer = createLuminanceBuffer();
	if (!pipeline)
		pipeline = createPipeline(useBloomBuffer, toneMapper);
}
void ToneMappingRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(descriptorSet);
		graphicsSystem->destroy(pipeline);
		graphicsSystem->destroy(luminanceBuffer);

		ECSM_UNSUBSCRIBE_FROM_EVENT("PreLdrRender", ToneMappingRenderSystem::preLdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("LdrRender", ToneMappingRenderSystem::ldrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", ToneMappingRenderSystem::gBufferRecreate);
	}
}

//**********************************************************************************************************************
void ToneMappingRenderSystem::preLdrRender()
{
	SET_CPU_ZONE_SCOPED("Tone Mapping Pre LDR Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(pipeline);

	if (pipelineView->isReady() && !descriptorSet)
	{
		auto uniforms = getUniforms(luminanceBuffer, useBloomBuffer);
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.deferred.toneMapping");
	}
}
void ToneMappingRenderSystem::ldrRender()
{
	SET_CPU_ZONE_SCOPED("Tone Mapping LDR Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(pipeline);
	auto luminanceBufferView = graphicsSystem->get(luminanceBuffer);

	if (!pipelineView->isReady() || !luminanceBufferView->isReady())
		return;

	auto bloomSystem = BloomRenderSystem::Instance::tryGet();
	auto pushConstants = pipelineView->getPushConstants<PushConstants>();
	pushConstants->frameIndex = (uint32)graphicsSystem->getFrameIndex();
	pushConstants->exposureFactor = exposureFactor;
	pushConstants->ditherIntensity = ditherIntensity;
	pushConstants->bloomIntensity = bloomSystem ? bloomSystem->intensity : 0.0f;

	SET_GPU_DEBUG_LABEL("Tone Mapping", Color::transparent);
	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->bindDescriptorSet(descriptorSet);
	pipelineView->pushConstants();
	pipelineView->drawFullscreen();
}

//**********************************************************************************************************************
void ToneMappingRenderSystem::gBufferRecreate()
{
	if (descriptorSet)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(descriptorSet);
		auto uniforms = getUniforms(luminanceBuffer, useBloomBuffer);
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.deferred.toneMapping");
	}
}

void ToneMappingRenderSystem::setConsts(bool useBloomBuffer, ToneMapper toneMapper)
{
	if (this->useBloomBuffer == useBloomBuffer && this->toneMapper == toneMapper)
		return;

	this->useBloomBuffer = useBloomBuffer;
	this->toneMapper = toneMapper;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	graphicsSystem->destroy(descriptorSet);
	descriptorSet = {};

	if (pipeline)
	{
		graphicsSystem->destroy(pipeline);
		pipeline = createPipeline(useBloomBuffer, toneMapper);
	}
}

ID<GraphicsPipeline> ToneMappingRenderSystem::getPipeline()
{
	if (!pipeline)
		pipeline = createPipeline(useBloomBuffer, toneMapper);
	return pipeline;
}
ID<Buffer> ToneMappingRenderSystem::getLuminanceBuffer()
{
	if (!luminanceBuffer)
		luminanceBuffer = createLuminanceBuffer();
	return luminanceBuffer;
}

void ToneMappingRenderSystem::setLuminance(float luminance)
{
	auto exposure = 1.0f / (luminance * lumToExp + 0.0001f);
	auto luminanceBufferView = GraphicsSystem::Instance::get()->get(luminanceBuffer);
	luminanceBufferView->fill(*((uint32*)&luminance), sizeof(float), 0);
	luminanceBufferView->fill(*((uint32*)&exposure), sizeof(float), sizeof(float));
}
void ToneMappingRenderSystem::setExposure(float exposure)
{
	auto luminance = (1.0f / exposure) * (1.0f / lumToExp) - 0.0001f;
	auto luminanceBufferView = GraphicsSystem::Instance::get()->get(luminanceBuffer);
	luminanceBufferView->fill(*((uint32*)&luminance), sizeof(float), 0);
	luminanceBufferView->fill(*((uint32*)&exposure), sizeof(float), sizeof(float));
}