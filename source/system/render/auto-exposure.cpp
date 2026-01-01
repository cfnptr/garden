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

#include "garden/system/render/auto-exposure.hpp"
#include "garden/system/render/tone-mapping.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/resource.hpp"
#include "garden/profiler.hpp"

using namespace garden;

static ID<Buffer> createHistogramBuffer(GraphicsSystem* graphicsSystem)
{
	auto usage = Buffer::Usage::Storage | Buffer::Usage::TransferDst;
	#if GARDEN_EDITOR
	usage |= Buffer::Usage::TransferSrc;
	#endif
	
	auto buffer = graphicsSystem->createBuffer(usage, Buffer::CpuAccess::None, 
		AutoExposureSystem::histogramSize * sizeof(uint32), Buffer::Location::PreferGPU, Buffer::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(buffer, "buffer.autoExposure.histogram");
	return buffer;
}

//**********************************************************************************************************************
static DescriptorSet::Uniforms getHistogramUniforms(GraphicsSystem* graphicsSystem, ID<Buffer> histogramBuffer)
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto hdrFramebufferView = graphicsSystem->get(deferredSystem->getHdrFramebuffer());
	auto hdrBufferView = hdrFramebufferView->getColorAttachments()[0].imageView;

	DescriptorSet::Uniforms uniforms =
	{ 
		{ "hdrBuffer", DescriptorSet::Uniform(hdrBufferView) },
		{ "histogram", DescriptorSet::Uniform(histogramBuffer) }
	};
	return uniforms;
}
static DescriptorSet::Uniforms getAverageUniforms(ID<Buffer> histogramBuffer)
{
	auto toneMappingSystem = ToneMappingSystem::Instance::get();
	DescriptorSet::Uniforms uniforms =
	{ 
		{ "histogram", DescriptorSet::Uniform(histogramBuffer) },
		{ "luminance", DescriptorSet::Uniform(toneMappingSystem->getLuminanceBuffer()) }
	};
	return uniforms;
}

static ID<ComputePipeline> createHistogramPipeline()
{
	ResourceSystem::ComputeOptions options;
	return ResourceSystem::Instance::get()->loadComputePipeline("auto-exposure/histogram", options);
}
static ID<ComputePipeline> createAveragePipeline()
{
	ResourceSystem::ComputeOptions options;
	return ResourceSystem::Instance::get()->loadComputePipeline("auto-exposure/average", options);
}

//**********************************************************************************************************************
AutoExposureSystem::AutoExposureSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", AutoExposureSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", AutoExposureSystem::deinit);
}
AutoExposureSystem::~AutoExposureSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", AutoExposureSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", AutoExposureSystem::deinit);
	}

	unsetSingleton();
}

void AutoExposureSystem::init()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Render", AutoExposureSystem::render);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", AutoExposureSystem::gBufferRecreate);
}
void AutoExposureSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(averageDS);
		graphicsSystem->destroy(histogramDS);
		graphicsSystem->destroy(histogramPipeline);
		graphicsSystem->destroy(histogramBuffer);

		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Render", AutoExposureSystem::render);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", AutoExposureSystem::gBufferRecreate);
	}
}

//**********************************************************************************************************************
static float calcTimeCoeff(float adaptationRate, float deltaTime) noexcept
{
	return std::clamp(1.0f - std::exp(-deltaTime * adaptationRate), 0.0f, 1.0f);
}

void AutoExposureSystem::render()
{
	SET_CPU_ZONE_SCOPED("Auto Exposure Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!isEnabled || !graphicsSystem->canRender())
		return;

	if (!isInitialized)
	{
		if (!histogramBuffer)
			histogramBuffer = createHistogramBuffer(graphicsSystem);
		if (!histogramPipeline)
			histogramPipeline = createHistogramPipeline();
		if (!averagePipeline)
			averagePipeline = createAveragePipeline();
		isInitialized = true;
	}

	auto toneMappingSystem = ToneMappingSystem::Instance::get();
	auto histogramPipelineView = graphicsSystem->get(histogramPipeline);
	auto averagePipelineView = graphicsSystem->get(averagePipeline);
	auto luminanceBufferView = graphicsSystem->get(toneMappingSystem->getLuminanceBuffer());
	if (!histogramPipelineView->isReady() || !averagePipelineView->isReady() || !luminanceBufferView->isReady())
		return;

	if (!histogramDS)
	{
		auto uniforms = getHistogramUniforms(graphicsSystem, histogramBuffer);
		histogramDS = graphicsSystem->createDescriptorSet(histogramPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(histogramDS, "descriptorSet.autoExposure.histogram");
	}
	if (!averageDS)
	{
		auto uniforms = getAverageUniforms(histogramBuffer);
		averageDS = graphicsSystem->createDescriptorSet(averagePipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(averageDS, "descriptorSet.autoExposure.average");
	}

	auto inputSystem = InputSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getScaledFrameSize();
	auto logLumRange = maxLogLum - minLogLum;
	auto deltaTime = (float)inputSystem->getDeltaTime();

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		SET_GPU_DEBUG_LABEL("Automatic Exposure");
		{
			auto histogramView = graphicsSystem->get(histogramBuffer);
			HistogramPC pc;
			pc.minLogLum = minLogLum;
			pc.invLogLumRange = 1.0f / logLumRange;

			SET_GPU_DEBUG_LABEL("Histogram");
			histogramView->fill(0);
			histogramPipelineView->bind();
			histogramPipelineView->bindDescriptorSet(histogramDS);
			histogramPipelineView->pushConstants(&pc);
			histogramPipelineView->dispatch(framebufferSize);
		}
		{
			AveragePC pc;
			pc.minLogLum = minLogLum;
			pc.logLumRange = logLumRange;
			pc.pixelCount = (float)framebufferSize.x * framebufferSize.y;
			pc.darkAdaptRate = calcTimeCoeff(darkAdaptRate, deltaTime);
			pc.brightAdaptRate = calcTimeCoeff(brightAdaptRate, deltaTime);

			SET_GPU_DEBUG_LABEL("Average");
			averagePipelineView->bind();
			averagePipelineView->bindDescriptorSet(averageDS);
			averagePipelineView->pushConstants(&pc);
			averagePipelineView->dispatch(1);
		}
	}
	graphicsSystem->stopRecording();
}

//**********************************************************************************************************************
void AutoExposureSystem::gBufferRecreate()
{
	if (histogramDS)
	{
		GraphicsSystem::Instance::get()->destroy(histogramDS);
		histogramDS = {};
	}
}

ID<ComputePipeline> AutoExposureSystem::getHistogramPipeline()
{
	if (!histogramPipeline)
		histogramPipeline = createHistogramPipeline();
	return histogramPipeline;
}
ID<ComputePipeline> AutoExposureSystem::getAveragePipeline()
{
	if (!averagePipeline)
		averagePipeline = createAveragePipeline();
	return averagePipeline;
}

ID<Buffer> AutoExposureSystem::getHistogramBuffer()
{
	if (!histogramBuffer)
		histogramBuffer = createHistogramBuffer(GraphicsSystem::Instance::get());
	return histogramBuffer;
}