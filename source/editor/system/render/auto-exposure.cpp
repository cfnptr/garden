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

#include "garden/editor/system/render/auto-exposure.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/auto-exposure.hpp"
#include "garden/system/render/tone-mapping.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

//**********************************************************************************************************************
static ID<Buffer> createReadbackBuffer(GraphicsSystem* graphicsSystem)
{
	auto size = (sizeof(ToneMappingSystem::LuminanceData) + 
		AutoExposureSystem::histogramSize * sizeof(uint32)) * graphicsSystem->getInFlightCount();
	auto buffer = graphicsSystem->createBuffer(Buffer::Usage::TransferDst, 
		Buffer::CpuAccess::RandomReadWrite, size, Buffer::Location::PreferGPU, Buffer::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(buffer, "buffer.editor.autoExposure.readback");
	return buffer;
}

static DescriptorSet::Uniforms getLimitsUniforms(GraphicsSystem* graphicsSystem)
{
	auto hdrBufferView = DeferredRenderSystem::Instance::get()->getHdrImageView();
	auto luminanceBuffer = ToneMappingSystem::Instance::get()->getLuminanceBuffer();
				
	DescriptorSet::Uniforms uniforms =
	{ 
		{ "hdrBuffer", DescriptorSet::Uniform(hdrBufferView) },
		{ "luminance", DescriptorSet::Uniform(luminanceBuffer) }
	};
	return uniforms;
}

//**********************************************************************************************************************
AutoExposureEditorSystem::AutoExposureEditorSystem()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", AutoExposureEditorSystem::init);
}
void AutoExposureEditorSystem::init()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("PreUiRender", AutoExposureEditorSystem::preUiRender);
	ECSM_SUBSCRIBE_TO_EVENT("UiRender", AutoExposureEditorSystem::uiRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", AutoExposureEditorSystem::gBufferRecreate);
	ECSM_SUBSCRIBE_TO_EVENT("EditorBarToolPP", AutoExposureEditorSystem::editorBarToolPP);
}

void AutoExposureEditorSystem::preUiRender()
{
	if (!showWindow)
		return;

	if (ImGui::Begin("Automatic Exposure (AE)", &showWindow, ImGuiWindowFlags_AlwaysAutoResize))
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		if (!readbackBuffer)
		{
			readbackBuffer = createReadbackBuffer(graphicsSystem);
			histogramSamples.resize(AutoExposureSystem::histogramSize);
		}

		auto autoExposureSystem = AutoExposureSystem::Instance::get();
		ImGui::Checkbox("Enabled", &autoExposureSystem->isEnabled);
		ImGui::DragFloat("Min Log Luminance", &autoExposureSystem->minLogLum, 0.1f);
		ImGui::DragFloat("Max Log Luminance", &autoExposureSystem->maxLogLum, 0.1f);
		ImGui::DragFloat("Dark Adaptation Rate", &autoExposureSystem->darkAdaptRate, 0.01f, 0.001f);
		ImGui::DragFloat("Bright Adaptation Rate", &autoExposureSystem->brightAdaptRate, 0.01f, 0.001f);

		auto readbackBufferView = graphicsSystem->get(readbackBuffer);
		auto size = sizeof(ToneMappingSystem::LuminanceData) + AutoExposureSystem::histogramSize * sizeof(uint32);
		auto offset = size * graphicsSystem->getInFlightIndex();
		readbackBufferView->invalidate(size, offset);
		auto map = readbackBufferView->getMap() + offset;
		auto luminance = (const ToneMappingSystem::LuminanceData*)map;
		auto histogram = (const uint32*)(map + sizeof(ToneMappingSystem::LuminanceData));

		uint32 maxHistogramValue = 0;
		for (uint16 i = 1; i < AutoExposureSystem::histogramSize; i++)
		{
			if (histogram[i] > maxHistogramValue)
				maxHistogramValue = histogram[i];
		}

		auto histogramSampleData = histogramSamples.data();
		histogramSampleData[0] = 0.0f; // Note: Reserved for too dark.
		for (uint16 i = 1; i < AutoExposureSystem::histogramSize; i++)
			histogramSampleData[i] = (float)histogram[i] / maxHistogramValue;

		ImGui::SeparatorText("Visualizer");
		ImGui::Checkbox("Visualize Luminance Limits", &visualizeLimits);
		if (ImGui::BeginItemTooltip())
		{
			ImGui::Text("Blue < Min / Max < Red");
			ImGui::EndTooltip();
		}
		ImGui::Spacing();

		ImGui::Text("Average Luminance: %f, Exposure: %f",
			luminance->avgLuminance, luminance->exposure);
		if (ImGui::BeginItemTooltip())
		{
			ImGui::Text("Histogram Range: %.3f / %.3f (log2(luminance))",
				autoExposureSystem->minLogLum, autoExposureSystem->maxLogLum);
			ImGui::EndTooltip();
		}

		ImGui::PlotHistogram("Histogram", histogramSampleData,
			AutoExposureSystem::histogramSize, 0, nullptr, 0.0f, 1.0f, { 320.0f, 64.0f });

		if (visualizeLimits)
		{
			if (!limitsPipeline)
			{	
				auto deferredSystem = DeferredRenderSystem::Instance::get();
				ResourceSystem::GraphicsOptions options;
				options.useAsyncRecording = deferredSystem->getOptions().useAsyncRecording;
				limitsPipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline(
					"editor/auto-exposure-limits", deferredSystem->getUiFramebuffer(), options);
			}

			auto pipelineView = graphicsSystem->get(limitsPipeline);
			if (!pipelineView->isReady())
				ImGui::TextDisabled("Limits pipeline is loading...");
		}

		auto toneMappingSystem = ToneMappingSystem::Instance::get();
		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("Readback Auto Exposure Data");
			Buffer::CopyRegion copyRegion;
			copyRegion.dstOffset = offset;
			copyRegion.size = sizeof(ToneMappingSystem::LuminanceData);
			Buffer::copy(toneMappingSystem->getLuminanceBuffer(), readbackBuffer, copyRegion);

			copyRegion.dstOffset = offset + sizeof(ToneMappingSystem::LuminanceData);
			copyRegion.size = AutoExposureSystem::histogramSize * sizeof(uint32);
			Buffer::copy(autoExposureSystem->getHistogramBuffer(), readbackBuffer, copyRegion);
		}
		graphicsSystem->stopRecording();
	}
	ImGui::End();
}

//**********************************************************************************************************************
void AutoExposureEditorSystem::uiRender()
{
	if (!visualizeLimits)
		return;
	
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(limitsPipeline);
	if (!pipelineView->isReady())
		return;

	if (!limitsDS)
	{
		auto uniforms = getLimitsUniforms(graphicsSystem);
		limitsDS = graphicsSystem->createDescriptorSet(limitsPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(limitsDS, "descriptorSet.editor.autoExposure.limits");
	}

	auto autoExposureSystem = AutoExposureSystem::Instance::get();

	PushConstants pc;
	pc.minLum = std::exp2(autoExposureSystem->minLogLum);
	pc.maxLum = std::exp2(autoExposureSystem->maxLogLum);

	if (graphicsSystem->isRenderPassAsync())
	{
		SET_GPU_DEBUG_LABEL_ASYNC("Auto Exposure Limits", INT32_MAX);
		pipelineView->bindAsync(0, INT32_MAX);
		pipelineView->setViewportScissorAsync(float4::zero, INT32_MAX);
		pipelineView->bindDescriptorSetAsync(limitsDS, 0, INT32_MAX);
		pipelineView->pushConstantsAsync(&pc, INT32_MAX);
		pipelineView->drawFullscreenAsync(INT32_MAX);
	}
	else
	{
		SET_GPU_DEBUG_LABEL("Auto Exposure Limits");
		pipelineView->bind();
		pipelineView->setViewportScissor();
		pipelineView->bindDescriptorSet(limitsDS);
		pipelineView->pushConstants(&pc);
		pipelineView->drawFullscreen();
	}
}

//**********************************************************************************************************************
void AutoExposureEditorSystem::gBufferRecreate()
{
	GraphicsSystem::Instance::get()->destroy(limitsDS);
}

void AutoExposureEditorSystem::editorBarToolPP()
{
	if (ImGui::MenuItem("Automatic Exposure (AE)"))
		showWindow = true;
}
#endif