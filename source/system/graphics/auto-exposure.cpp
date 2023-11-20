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

#include "garden/system/graphics/auto-exposure.hpp"
#include "garden/system/graphics/editor/auto-exposure.hpp"
#include "garden/system/graphics/tone-mapping.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

namespace
{
	struct HistogramPC final
	{
		float minLogLum;
		float invLogLumRange;
	};
	struct AveragePC final
	{
		float minLogLum;
		float logLumRange;
		float pixelCount;
		float darkAdaptRate;
		float brightAdaptRate;
	};
}

//--------------------------------------------------------------------------------------------------
static ID<Buffer> createHistogramBuffer(GraphicsSystem* graphicsSystem)
{
	#if GARDEN_EDITOR
	const auto bind = Buffer::Bind::TransferSrc;
	#else
	const auto bind = Buffer::Bind::None;
	#endif
	
	auto buffer = graphicsSystem->createBuffer(
		Buffer::Bind::Storage | Buffer::Bind::TransferDst | bind,
		Buffer::Usage::GpuOnly, AE_HISTOGRAM_SIZE * sizeof(uint32));
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, buffer, "buffer.auto-exposure.histogram");
	return buffer;
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
		Buffer::Usage::GpuOnly, data, sizeof(ToneMappingRenderSystem::Luminance));
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, buffer, "buffer.toneMapping.luminance");
	return buffer;
}

//--------------------------------------------------------------------------------------------------
static map<string, DescriptorSet::Uniform> getHistogramUniforms(
	Manager* manager, GraphicsSystem* graphicsSystem, ID<Buffer> histogramBuffer)
{
	auto deferredSystem = manager->get<DeferredRenderSystem>();
	auto hdrFramebufferView = graphicsSystem->get(deferredSystem->getHdrFramebuffer());
	map<string, DescriptorSet::Uniform> uniforms =
	{ 
		{ "hdrBuffer", DescriptorSet::Uniform(
			hdrFramebufferView->getColorAttachments()[0].imageView) },
		{ "histogram", DescriptorSet::Uniform(histogramBuffer) }
	};
	return uniforms;
}
static map<string, DescriptorSet::Uniform> getAverageUniforms(
	ID<Buffer> histogramBuffer, ID<Buffer> luminanceBuffer)
{
	map<string, DescriptorSet::Uniform> uniforms =
	{ 
		{ "histogram", DescriptorSet::Uniform(histogramBuffer) },
		{ "luminance", DescriptorSet::Uniform(luminanceBuffer) }
	};
	return uniforms;
}

//--------------------------------------------------------------------------------------------------
static ID<ComputePipeline> createHistogramPipeline()
{
	return ResourceSystem::getInstance()->loadComputePipeline("auto-exposure/histogram");
}
static ID<ComputePipeline> createAveragePipeline()
{
	return ResourceSystem::getInstance()->loadComputePipeline("auto-exposure/average");
}

//--------------------------------------------------------------------------------------------------
void AutoExposureRenderSystem::initialize()
{
	auto graphicsSystem = getGraphicsSystem();
	deferredSystem = getManager()->get<DeferredRenderSystem>();

	if (!histogramBuffer) histogramBuffer = createHistogramBuffer(graphicsSystem);
	if (!luminanceBuffer) luminanceBuffer = createLuminanceBuffer(graphicsSystem);
	if (!histogramPipeline) histogramPipeline = createHistogramPipeline();
	if (!averagePipeline) averagePipeline = createAveragePipeline();

	#if GARDEN_EDITOR
	editor = new AutoExposureEditor(this);
	#endif
}
void AutoExposureRenderSystem::terminate()
{
	#if GARDEN_EDITOR
	delete (AutoExposureEditor*)editor;
	#endif
}

//--------------------------------------------------------------------------------------------------
static float calcTimeCoeff(float adaptationRate, float deltaTime)
{
	return std::clamp(1.0f - std::exp(-deltaTime * adaptationRate), 0.0f, 1.0f);
}

//--------------------------------------------------------------------------------------------------
void AutoExposureRenderSystem::render()
{
	auto graphicsSystem = getGraphicsSystem();
	auto histogramPipelineView = graphicsSystem->get(histogramPipeline);
	auto averagePipelineView = graphicsSystem->get(averagePipeline);
	auto luminanceBufferView = graphicsSystem->get(luminanceBuffer);
	
	if (!histogramPipelineView->isReady() || !averagePipelineView->isReady() ||
		!luminanceBufferView->isReady() || !graphicsSystem->camera) return;

	if (!histogramDescriptorSet)
	{
		auto manager = getManager();
		auto uniforms = getHistogramUniforms(
			manager, graphicsSystem, histogramBuffer);
		histogramDescriptorSet = graphicsSystem->createDescriptorSet(
			histogramPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, histogramDescriptorSet,
			"descriptorSet.auto-exposure.histogram");
		
		auto toneMappingSystem = manager->get<ToneMappingRenderSystem>();
		uniforms = getAverageUniforms(histogramBuffer, luminanceBuffer);
		averageDescriptorSet = graphicsSystem->createDescriptorSet(
			averagePipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, averageDescriptorSet,
			"descriptorSet.auto-exposure.average");
	}

	graphicsSystem->startRecording(CommandBufferType::Frame);

	if (isEnabled)
	{
		auto framebufferSize = deferredSystem->getFramebufferSize();
		auto logLumRange = maxLogLum - minLogLum;
		auto deltaTime = (float)graphicsSystem->getDeltaTime();

		SET_GPU_DEBUG_LABEL("Automatic Exposure", Color::transparent);
		auto histogramView = graphicsSystem->get(histogramBuffer);
		histogramView->fill(0);

		histogramPipelineView->bind();
		histogramPipelineView->bindDescriptorSet(histogramDescriptorSet);
		auto histogramPC = histogramPipelineView->getPushConstants<HistogramPC>();
		histogramPC->minLogLum = minLogLum;
		histogramPC->invLogLumRange = 1.0f / logLumRange;
		histogramPipelineView->pushConstants();
		histogramPipelineView->dispatch(int3(framebufferSize, 1));

		averagePipelineView->bind();
		averagePipelineView->bindDescriptorSet(averageDescriptorSet);
		auto averagePC = averagePipelineView->getPushConstants<AveragePC>();
		averagePC->minLogLum = minLogLum;
		averagePC->logLumRange = logLumRange;
		averagePC->pixelCount = (float)framebufferSize.x * framebufferSize.y;
		averagePC->darkAdaptRate = calcTimeCoeff(darkAdaptRate, deltaTime);
		averagePC->brightAdaptRate = calcTimeCoeff(brightAdaptRate, deltaTime);
		averagePipelineView->pushConstants();
		averagePipelineView->dispatch(int3(1));
	}

	#if GARDEN_EDITOR
	((AutoExposureEditor*)editor)->render();
	#endif

	graphicsSystem->stopRecording();
}

//--------------------------------------------------------------------------------------------------
void AutoExposureRenderSystem::recreateSwapchain(const SwapchainChanges& changes)
{
	if (changes.framebufferSize && histogramDescriptorSet)
	{
		auto graphicsSystem = getGraphicsSystem();
		auto descriptorSetView = graphicsSystem->get(histogramDescriptorSet);
		auto uniforms = getHistogramUniforms(getManager(), graphicsSystem, histogramBuffer);
		descriptorSetView->recreate(std::move(uniforms));
	}

	#if GARDEN_EDITOR
	((AutoExposureEditor*)editor)->recreateSwapchain(changes);
	#endif
}

//--------------------------------------------------------------------------------------------------
ID<ComputePipeline> AutoExposureRenderSystem::getHistogramPipeline()
{
	if (!histogramPipeline) histogramPipeline = createHistogramPipeline();
	return histogramPipeline;
}
ID<ComputePipeline> AutoExposureRenderSystem::getAveragePipeline()
{
	if (!averagePipeline) averagePipeline = createAveragePipeline();
	return averagePipeline;
}

//--------------------------------------------------------------------------------------------------
ID<Buffer> AutoExposureRenderSystem::getHistogramBuffer()
{
	if (!histogramBuffer) histogramBuffer = createHistogramBuffer(getGraphicsSystem());
	return histogramBuffer;
}
ID<Buffer> AutoExposureRenderSystem::getLuminanceBuffer()
{
	if (!luminanceBuffer) luminanceBuffer = createLuminanceBuffer(getGraphicsSystem());
	return luminanceBuffer;
}

//--------------------------------------------------------------------------------------------------
void AutoExposureRenderSystem::setLuminance(float luminance)
{
	auto exposure = 1.0f / (luminance * LUM_TO_EXP + 0.0001f);
	auto luminanceBufferView = getGraphicsSystem()->get(luminanceBuffer);
	luminanceBufferView->fill(*((uint32*)&luminance), sizeof(float), 0);
	luminanceBufferView->fill(*((uint32*)&exposure), sizeof(float), sizeof(float));
}
void AutoExposureRenderSystem::setExposure(float exposure)
{
	auto luminance = (1.0f / exposure) * (1.0f / LUM_TO_EXP) - 0.0001f;
	auto luminanceBufferView = getGraphicsSystem()->get(luminanceBuffer);
	luminanceBufferView->fill(*((uint32*)&luminance), sizeof(float), 0);
	luminanceBufferView->fill(*((uint32*)&exposure), sizeof(float), sizeof(float));
}