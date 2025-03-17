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

#include "garden/system/render/auto-exposure.hpp"
#include "garden/system/render/tone-mapping.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/settings.hpp"
#include "garden/profiler.hpp"

using namespace garden;

static ID<Buffer> createHistogramBuffer()
{
	auto bind = Buffer::Bind::Storage | Buffer::Bind::TransferDst;

	#if GARDEN_EDITOR
	bind |= Buffer::Bind::TransferSrc;
	#endif
	
	auto buffer = GraphicsSystem::Instance::get()->createBuffer(bind,
		Buffer::Access::None, AutoExposureRenderSystem::histogramSize * sizeof(uint32), 
		Buffer::Usage::PreferGPU, Buffer::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(buffer, "buffer.auto-exposure.histogram");
	return buffer;
}

//**********************************************************************************************************************
static map<string, DescriptorSet::Uniform> getHistogramUniforms(ID<Buffer> histogramBuffer)
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto hdrFramebufferView = GraphicsSystem::Instance::get()->get(deferredSystem->getHdrFramebuffer());
	map<string, DescriptorSet::Uniform> uniforms =
	{ 
		{ "hdrBuffer", DescriptorSet::Uniform(hdrFramebufferView->getColorAttachments()[0].imageView) },
		{ "histogram", DescriptorSet::Uniform(histogramBuffer) }
	};
	return uniforms;
}
static map<string, DescriptorSet::Uniform> getAverageUniforms(ID<Buffer> histogramBuffer)
{
	auto toneMappingSystem = ToneMappingRenderSystem::Instance::get();
	map<string, DescriptorSet::Uniform> uniforms =
	{ 
		{ "histogram", DescriptorSet::Uniform(histogramBuffer) },
		{ "luminance", DescriptorSet::Uniform(toneMappingSystem->getLuminanceBuffer()) }
	};
	return uniforms;
}

static ID<ComputePipeline> createHistogramPipeline()
{
	return ResourceSystem::Instance::get()->loadComputePipeline("auto-exposure/histogram");
}
static ID<ComputePipeline> createAveragePipeline()
{
	return ResourceSystem::Instance::get()->loadComputePipeline("auto-exposure/average");
}

//**********************************************************************************************************************
AutoExposureRenderSystem::AutoExposureRenderSystem(bool setSingleton) : Singleton(setSingleton)
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", AutoExposureRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", AutoExposureRenderSystem::deinit);
}
AutoExposureRenderSystem::~AutoExposureRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", AutoExposureRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", AutoExposureRenderSystem::deinit);
	}

	unsetSingleton();
}

void AutoExposureRenderSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("Render", AutoExposureRenderSystem::render);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", AutoExposureRenderSystem::gBufferRecreate);

	if (!histogramBuffer)
		histogramBuffer = createHistogramBuffer();
	if (!histogramPipeline)
		histogramPipeline = createHistogramPipeline();
	if (!averagePipeline)
		averagePipeline = createAveragePipeline();
}
void AutoExposureRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(averageDescriptorSet);
		graphicsSystem->destroy(histogramDescriptorSet);
		graphicsSystem->destroy(histogramPipeline);
		graphicsSystem->destroy(histogramBuffer);

		ECSM_UNSUBSCRIBE_FROM_EVENT("Render", AutoExposureRenderSystem::render);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", AutoExposureRenderSystem::gBufferRecreate);
	}
}

//**********************************************************************************************************************
static float calcTimeCoeff(float adaptationRate, float deltaTime) noexcept
{
	return std::clamp(1.0f - std::exp(-deltaTime * adaptationRate), 0.0f, 1.0f);
}

void AutoExposureRenderSystem::render()
{
	SET_CPU_ZONE_SCOPED("Auto Exposure Render");

	if (!isEnabled || !GraphicsSystem::Instance::get()->canRender())
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto histogramPipelineView = graphicsSystem->get(histogramPipeline);
	auto averagePipelineView = graphicsSystem->get(averagePipeline);
	if (!histogramPipelineView->isReady() || !averagePipelineView->isReady())
		return;

	if (!histogramDescriptorSet)
	{
		auto uniforms = getHistogramUniforms(histogramBuffer);
		histogramDescriptorSet = graphicsSystem->createDescriptorSet(histogramPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(histogramDescriptorSet, "descriptorSet.auto-exposure.histogram");
		
		uniforms = getAverageUniforms(histogramBuffer);
		averageDescriptorSet = graphicsSystem->createDescriptorSet(averagePipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(averageDescriptorSet, "descriptorSet.auto-exposure.average");
	}

	auto inputSystem = InputSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	auto logLumRange = maxLogLum - minLogLum;
	auto deltaTime = (float)inputSystem->getDeltaTime();

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		SET_GPU_DEBUG_LABEL("Automatic Exposure", Color::transparent);
		auto histogramView = graphicsSystem->get(histogramBuffer);
		histogramView->fill(0);

		auto histogramPC = histogramPipelineView->getPushConstants<HistogramPC>();
		histogramPC->minLogLum = minLogLum;
		histogramPC->invLogLumRange = 1.0f / logLumRange;

		histogramPipelineView->bind();
		histogramPipelineView->bindDescriptorSet(histogramDescriptorSet);
		histogramPipelineView->pushConstants();
		histogramPipelineView->dispatch(framebufferSize);

		auto averagePC = averagePipelineView->getPushConstants<AveragePC>();
		averagePC->minLogLum = minLogLum;
		averagePC->logLumRange = logLumRange;
		averagePC->pixelCount = (float)framebufferSize.x * framebufferSize.y;
		averagePC->darkAdaptRate = calcTimeCoeff(darkAdaptRate, deltaTime);
		averagePC->brightAdaptRate = calcTimeCoeff(brightAdaptRate, deltaTime);

		averagePipelineView->bind();
		averagePipelineView->bindDescriptorSet(averageDescriptorSet);
		averagePipelineView->pushConstants();
		averagePipelineView->dispatch(1);
	}
	graphicsSystem->stopRecording();
}

//**********************************************************************************************************************
void AutoExposureRenderSystem::gBufferRecreate()
{
	if (histogramDescriptorSet)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(histogramDescriptorSet);
		auto uniforms = getHistogramUniforms(histogramBuffer);
		histogramDescriptorSet = graphicsSystem->createDescriptorSet(histogramPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(histogramDescriptorSet, "descriptorSet.auto-exposure.histogram");
	}
}

ID<ComputePipeline> AutoExposureRenderSystem::getHistogramPipeline()
{
	if (!histogramPipeline)
		histogramPipeline = createHistogramPipeline();
	return histogramPipeline;
}
ID<ComputePipeline> AutoExposureRenderSystem::getAveragePipeline()
{
	if (!averagePipeline)
		averagePipeline = createAveragePipeline();
	return averagePipeline;
}

ID<Buffer> AutoExposureRenderSystem::getHistogramBuffer()
{
	if (!histogramBuffer)
		histogramBuffer = createHistogramBuffer();
	return histogramBuffer;
}