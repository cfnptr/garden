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

	constexpr float data[2] = { 1.0f / ToneMappingSystem::lumToExp, 1.0f };
	auto buffer = GraphicsSystem::Instance::get()->createBuffer(Buffer::Usage::Storage | Buffer::Usage::Uniform | 
		Buffer::Usage::TransferDst | Buffer::Usage::TransferQ | usage, Buffer::CpuAccess::None, data, 
		sizeof(ToneMappingSystem::LuminanceData), Buffer::Location::PreferGPU, Buffer::Strategy::Size);
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
	auto hdrBufferView = hdrFramebufferView->getColorAttachments()[0].imageView;

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
		{ "hdrBuffer", DescriptorSet::Uniform(hdrBufferView) },
		{ "bloomBuffer", DescriptorSet::Uniform(bloomBufferView) },
		{ "luminance", DescriptorSet::Uniform(luminanceBuffer) }
	};
	return uniforms;
}

static ID<GraphicsPipeline> createPipeline(bool useBloomBuffer, uint8 toneMapper)
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	Pipeline::SpecConstValues specConsts =
	{
		{ "USE_BLOOM_BUFFER", Pipeline::SpecConstValue(useBloomBuffer) },
		{ "TONE_MAPPER", Pipeline::SpecConstValue((uint32)toneMapper) }
	};

	ResourceSystem::GraphicsOptions options;
	options.specConstValues = &specConsts;
	options.useAsyncRecording = deferredSystem->useAsyncRecording();
	
	return ResourceSystem::Instance::get()->loadGraphicsPipeline(
		"tone-mapping", deferredSystem->getLdrFramebuffer(), options);
}

//**********************************************************************************************************************
ToneMappingSystem::ToneMappingSystem(bool useBloomBuffer, uint8 toneMapper, bool setSingleton) :
	Singleton(setSingleton), useBloomBuffer(useBloomBuffer), toneMapper(toneMapper)
{
	GARDEN_ASSERT(toneMapper < TONE_MAPPER_COUNT);

	ECSM_SUBSCRIBE_TO_EVENT("Init", ToneMappingSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", ToneMappingSystem::deinit);
}
ToneMappingSystem::~ToneMappingSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", ToneMappingSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", ToneMappingSystem::deinit);
	}

	unsetSingleton();
}

void ToneMappingSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("PreLdrRender", ToneMappingSystem::preLdrRender);
	ECSM_SUBSCRIBE_TO_EVENT("LdrRender", ToneMappingSystem::ldrRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", ToneMappingSystem::gBufferRecreate);

	if (!luminanceBuffer)
		luminanceBuffer = createLuminanceBuffer();
	if (!pipeline)
		pipeline = createPipeline(useBloomBuffer, toneMapper);
}
void ToneMappingSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(descriptorSet);
		graphicsSystem->destroy(pipeline);
		graphicsSystem->destroy(luminanceBuffer);

		ECSM_UNSUBSCRIBE_FROM_EVENT("PreLdrRender", ToneMappingSystem::preLdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("LdrRender", ToneMappingSystem::ldrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", ToneMappingSystem::gBufferRecreate);
	}
}

//**********************************************************************************************************************
void ToneMappingSystem::preLdrRender()
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
void ToneMappingSystem::ldrRender()
{
	SET_CPU_ZONE_SCOPED("Tone Mapping LDR Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(pipeline);
	auto luminanceBufferView = graphicsSystem->get(luminanceBuffer);

	if (!pipelineView->isReady() || !luminanceBufferView->isReady())
		return;

	auto bloomSystem = BloomRenderSystem::Instance::tryGet();

	PushConstants pc;
	pc.frameIndex = (uint32)graphicsSystem->getCurrentFrameIndex();
	pc.exposureFactor = exposureFactor;
	pc.ditherIntensity = ditherIntensity;
	pc.bloomIntensity = bloomSystem ? bloomSystem->intensity : 0.0f;

	SET_GPU_DEBUG_LABEL("Tone Mapping", Color::transparent);
	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->bindDescriptorSet(descriptorSet);
	pipelineView->pushConstants(&pc);
	pipelineView->drawFullscreen();
}

//**********************************************************************************************************************
void ToneMappingSystem::gBufferRecreate()
{
	if (descriptorSet)
	{
		GraphicsSystem::Instance::get()->destroy(descriptorSet);
		descriptorSet = {};
	}
}

void ToneMappingSystem::setConsts(bool useBloomBuffer, uint8 toneMapper)
{
	GARDEN_ASSERT(toneMapper < TONE_MAPPER_COUNT);

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

ID<GraphicsPipeline> ToneMappingSystem::getPipeline()
{
	if (!pipeline)
		pipeline = createPipeline(useBloomBuffer, toneMapper);
	return pipeline;
}
ID<Buffer> ToneMappingSystem::getLuminanceBuffer()
{
	if (!luminanceBuffer)
		luminanceBuffer = createLuminanceBuffer();
	return luminanceBuffer;
}

void ToneMappingSystem::setLuminance(float luminance)
{
	auto exposure = 1.0f / (luminance * lumToExp + 0.0001f);
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto luminanceBufferView = graphicsSystem->get(luminanceBuffer);

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		luminanceBufferView->fill(*((uint32*)&luminance), sizeof(float), 0);
		luminanceBufferView->fill(*((uint32*)&exposure), sizeof(float), sizeof(float));
	}
	graphicsSystem->stopRecording();
}
void ToneMappingSystem::setExposure(float exposure)
{
	auto luminance = (1.0f / exposure) * (1.0f / lumToExp) - 0.0001f;
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto luminanceBufferView = graphicsSystem->get(luminanceBuffer);

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		luminanceBufferView->fill(*((uint32*)&luminance), sizeof(float), 0);
		luminanceBufferView->fill(*((uint32*)&exposure), sizeof(float), sizeof(float));
	}
	graphicsSystem->stopRecording();
}