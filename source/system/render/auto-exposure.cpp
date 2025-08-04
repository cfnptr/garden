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
#include "garden/profiler.hpp"

using namespace garden;

static ID<Buffer> createHistogramBuffer()
{
	auto usage = Buffer::Usage::Storage | Buffer::Usage::TransferDst;
	#if GARDEN_EDITOR
	usage |= Buffer::Usage::TransferSrc;
	#endif
	
	auto buffer = GraphicsSystem::Instance::get()->createBuffer(usage, Buffer::CpuAccess::None, 
		AutoExposureSystem::histogramSize * sizeof(uint32), Buffer::Location::PreferGPU, Buffer::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(buffer, "buffer.autoExposure.histogram");
	return buffer;
}

//**********************************************************************************************************************
static DescriptorSet::Uniforms getHistogramUniforms(ID<Buffer> histogramBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
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
	ECSM_SUBSCRIBE_TO_EVENT("Init", AutoExposureSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", AutoExposureSystem::deinit);
}
AutoExposureSystem::~AutoExposureSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", AutoExposureSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", AutoExposureSystem::deinit);
	}

	unsetSingleton();
}

void AutoExposureSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("Render", AutoExposureSystem::render);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", AutoExposureSystem::gBufferRecreate);

	if (!histogramBuffer)
		histogramBuffer = createHistogramBuffer();
	if (!histogramPipeline)
		histogramPipeline = createHistogramPipeline();
	if (!averagePipeline)
		averagePipeline = createAveragePipeline();
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

	if (!isEnabled || !GraphicsSystem::Instance::get()->canRender())
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto toneMappingSystem = ToneMappingSystem::Instance::get();
	auto histogramPipelineView = graphicsSystem->get(histogramPipeline);
	auto averagePipelineView = graphicsSystem->get(averagePipeline);
	auto luminanceBufferView = graphicsSystem->get(toneMappingSystem->getLuminanceBuffer());
	if (!histogramPipelineView->isReady() || !averagePipelineView->isReady() || !luminanceBufferView->isReady())
		return;

	if (!histogramDS)
	{
		auto uniforms = getHistogramUniforms(histogramBuffer);
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
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	auto logLumRange = maxLogLum - minLogLum;
	auto deltaTime = (float)inputSystem->getDeltaTime();

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		SET_GPU_DEBUG_LABEL("Automatic Exposure", Color::transparent);
		auto histogramView = graphicsSystem->get(histogramBuffer);
		histogramView->fill(0);

		HistogramPC histogramPC;
		histogramPC.minLogLum = minLogLum;
		histogramPC.invLogLumRange = 1.0f / logLumRange;

		histogramPipelineView->bind();
		histogramPipelineView->bindDescriptorSet(histogramDS);
		histogramPipelineView->pushConstants(&histogramPC);
		histogramPipelineView->dispatch(framebufferSize);

		AveragePC averagePC;
		averagePC.minLogLum = minLogLum;
		averagePC.logLumRange = logLumRange;
		averagePC.pixelCount = (float)framebufferSize.x * framebufferSize.y;
		averagePC.darkAdaptRate = calcTimeCoeff(darkAdaptRate, deltaTime);
		averagePC.brightAdaptRate = calcTimeCoeff(brightAdaptRate, deltaTime);

		averagePipelineView->bind();
		averagePipelineView->bindDescriptorSet(averageDS);
		averagePipelineView->pushConstants(&averagePC);
		averagePipelineView->dispatch(1);
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
		histogramBuffer = createHistogramBuffer();
	return histogramBuffer;
}