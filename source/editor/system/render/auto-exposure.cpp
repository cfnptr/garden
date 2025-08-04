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

#include "garden/editor/system/render/auto-exposure.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/auto-exposure.hpp"
#include "garden/system/render/tone-mapping.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

static ID<Buffer> createReadbackBuffer()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto size = (sizeof(ToneMappingSystem::LuminanceData) + 
		AutoExposureSystem::histogramSize * sizeof(uint32)) * graphicsSystem->getInFlightCount();
	auto buffer = graphicsSystem->createBuffer(Buffer::Usage::TransferDst, 
		Buffer::CpuAccess::RandomReadWrite, size, Buffer::Location::PreferGPU, Buffer::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(buffer, "buffer.editor.autoExposure.readback");
	return buffer;
}

static DescriptorSet::Uniforms getLimitsUniforms()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto toneMappingSystem = ToneMappingSystem::Instance::get();
	auto hdrFramebufferView = graphicsSystem->get(deferredSystem->getHdrFramebuffer());
	auto hdrBufferView = hdrFramebufferView->getColorAttachments()[0].imageView;
	auto luminanceBuffer = toneMappingSystem->getLuminanceBuffer();
				
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
	ECSM_SUBSCRIBE_TO_EVENT("Init", AutoExposureEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", AutoExposureEditorSystem::deinit);
}
AutoExposureEditorSystem::~AutoExposureEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", AutoExposureEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", AutoExposureEditorSystem::deinit);
	}
}

void AutoExposureEditorSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("PreUiRender", AutoExposureEditorSystem::preUiRender);
	ECSM_SUBSCRIBE_TO_EVENT("UiRender", AutoExposureEditorSystem::uiRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", AutoExposureEditorSystem::gBufferRecreate);
	ECSM_SUBSCRIBE_TO_EVENT("EditorBarToolPP", AutoExposureEditorSystem::editorBarToolPP);
}
void AutoExposureEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(limitsDS);
		graphicsSystem->destroy(limitsPipeline);
		graphicsSystem->destroy(readbackBuffer);
		
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreUiRender", AutoExposureEditorSystem::preUiRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("UiRender", AutoExposureEditorSystem::uiRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", AutoExposureEditorSystem::gBufferRecreate);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorBarToolPP", AutoExposureEditorSystem::editorBarToolPP);
	}
}

//**********************************************************************************************************************
void AutoExposureEditorSystem::preUiRender()
{
	if (!showWindow)
		return;

	if (ImGui::Begin("Automatic Exposure (AE)", &showWindow, ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (!readbackBuffer)
		{
			readbackBuffer = createReadbackBuffer();
			histogramSamples.resize(AutoExposureSystem::histogramSize);
		}

		auto autoExposureSystem = AutoExposureSystem::Instance::get();
		ImGui::Checkbox("Enabled", &autoExposureSystem->isEnabled);
		ImGui::DragFloat("Min Log Luminance", &autoExposureSystem->minLogLum, 0.1f);
		ImGui::DragFloat("Max Log Luminance", &autoExposureSystem->maxLogLum, 0.1f);
		ImGui::DragFloat("Dark Adaptation Rate", &autoExposureSystem->darkAdaptRate, 0.01f, 0.001f);
		ImGui::DragFloat("Bright Adaptation Rate", &autoExposureSystem->brightAdaptRate, 0.01f, 0.001f);

		auto graphicsSystem = GraphicsSystem::Instance::get();
		auto readbackBufferView = graphicsSystem->get(readbackBuffer);
		auto size = sizeof(ToneMappingSystem::LuminanceData) + AutoExposureSystem::histogramSize * sizeof(uint32);
		auto offset = size * graphicsSystem->getInFlightIndex();
		readbackBufferView->invalidate(size, offset);
		auto map = readbackBufferView->getMap() + offset;
		auto luminance = (const ToneMappingSystem::LuminanceData*)map;
		auto histogram = (const uint32*)(map + sizeof(ToneMappingSystem::LuminanceData));
		uint32 maxHistogramValue = 0;

		for (uint16 i = 0; i < AutoExposureSystem::histogramSize; i++)
		{
			if (histogram[i] > maxHistogramValue)
				maxHistogramValue = histogram[i];
		}

		for (uint16 i = 0; i < AutoExposureSystem::histogramSize; i++)
			histogramSamples[i] = (float)histogram[i] / maxHistogramValue;

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

		ImGui::PlotHistogram("Histogram", histogramSamples.data(),
			AutoExposureSystem::histogramSize, 0, nullptr, 0.0f, 1.0f, { 320.0f, 64.0f });

		if (visualizeLimits)
		{
			if (!limitsPipeline)
			{	
				auto deferredSystem = DeferredRenderSystem::Instance::get();
				ResourceSystem::GraphicsOptions options;
				limitsPipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline(
					"editor/auto-exposure-limits", deferredSystem->getUiFramebuffer(), options);
			}
			auto pipelineView = graphicsSystem->get(limitsPipeline);
			if (pipelineView->isReady())
			{
				if (!limitsDS)
				{
					auto uniforms = getLimitsUniforms();
					limitsDS = graphicsSystem->createDescriptorSet(limitsPipeline, std::move(uniforms));
					SET_RESOURCE_DEBUG_NAME(limitsDS, "descriptorSet.editor.autoExposure.limits");
				}
			}
			else
			{
				ImGui::TextDisabled("Limits pipeline is loading...");
			}
		}

		auto toneMappingSystem = ToneMappingSystem::Instance::get();
		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("Readback Auto Exposure Data", Color::transparent);
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
void AutoExposureEditorSystem::uiRender()
{
	if (!visualizeLimits)
		return;
	
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(limitsPipeline);
	if (!pipelineView->isReady())
		return;

	auto autoExposureSystem = AutoExposureSystem::Instance::get();

	PushConstants pc;
	pc.minLum = std::exp2(autoExposureSystem->minLogLum);
	pc.maxLum = std::exp2(autoExposureSystem->maxLogLum);

	SET_GPU_DEBUG_LABEL("Auto Exposure Limits", Color::transparent);
	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->bindDescriptorSet(limitsDS);
	pipelineView->pushConstants(&pc);
	pipelineView->drawFullscreen();
}

//**********************************************************************************************************************
void AutoExposureEditorSystem::gBufferRecreate()
{
	if (limitsDS)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(limitsDS);
		auto uniforms = getLimitsUniforms();
		limitsDS = graphicsSystem->createDescriptorSet(limitsPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(limitsDS, "descriptorSet.editor.autoExposure.limits");
	}
}

void AutoExposureEditorSystem::editorBarToolPP()
{
	if (ImGui::MenuItem("Automatic Exposure (AE)"))
		showWindow = true;
}
#endif