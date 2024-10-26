// Copyright 2022-2024 Nikita Fediuchin. All rights reserved.
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
	auto size = (sizeof(ToneMappingRenderSystem::LuminanceData) + 
		AutoExposureRenderSystem::histogramSize * sizeof(uint32)) * graphicsSystem->getSwapchainSize();
	auto buffer = graphicsSystem->createBuffer(Buffer::Bind::TransferDst, 
		Buffer::Access::RandomReadWrite, size, Buffer::Usage::PreferGPU, Buffer::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(buffer, "buffer.auto-exposure.editor.readback");
	return buffer;
}

static map<string, DescriptorSet::Uniform> getLimitsUniforms()
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto toneMappingSystem = ToneMappingRenderSystem::Instance::get();
	auto hdrFramebufferView = GraphicsSystem::Instance::get()->get(deferredSystem->getHdrFramebuffer());
				
	map<string, DescriptorSet::Uniform> uniforms =
	{ 
		{ "hdrBuffer", DescriptorSet::Uniform(hdrFramebufferView->getColorAttachments()[0].imageView) },
		{ "luminance", DescriptorSet::Uniform(toneMappingSystem->getLuminanceBuffer()) }
	};
	return uniforms;
}

//**********************************************************************************************************************
AutoExposureRenderEditorSystem::AutoExposureRenderEditorSystem()
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", AutoExposureRenderEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", AutoExposureRenderEditorSystem::deinit);
}
AutoExposureRenderEditorSystem::~AutoExposureRenderEditorSystem()
{
	if (Manager::Instance::get()->isRunning())
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", AutoExposureRenderEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", AutoExposureRenderEditorSystem::deinit);
	}
}

void AutoExposureRenderEditorSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("EditorRender", AutoExposureRenderEditorSystem::editorRender);
	ECSM_SUBSCRIBE_TO_EVENT("SwapchainRecreate", AutoExposureRenderEditorSystem::swapchainRecreate);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", AutoExposureRenderEditorSystem::gBufferRecreate);
	ECSM_SUBSCRIBE_TO_EVENT("EditorBarTool", AutoExposureRenderEditorSystem::editorBarTool);
}
void AutoExposureRenderEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning())
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorRender", AutoExposureRenderEditorSystem::editorRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("SwapchainRecreate", AutoExposureRenderEditorSystem::swapchainRecreate);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", AutoExposureRenderEditorSystem::gBufferRecreate);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorBarTool", AutoExposureRenderEditorSystem::editorBarTool);
	}
}

//**********************************************************************************************************************
void AutoExposureRenderEditorSystem::editorRender()
{
	if (!GraphicsSystem::Instance::get()->canRender())
		return;

	if (showWindow)
	{
		if (ImGui::Begin("Automatic Exposure (AE)", &showWindow, ImGuiWindowFlags_AlwaysAutoResize))
		{
			if (!readbackBuffer)
			{
				readbackBuffer = createReadbackBuffer();
				histogramSamples.resize(AutoExposureRenderSystem::histogramSize);
			}

			auto autoExposureSystem = AutoExposureRenderSystem::Instance::get();
			ImGui::Checkbox("Enabled", &autoExposureSystem->isEnabled);
			ImGui::DragFloat("Min Log Luminance", &autoExposureSystem->minLogLum, 0.1f);
			ImGui::DragFloat("Max Log Luminance", &autoExposureSystem->maxLogLum, 0.1f);
			ImGui::DragFloat("Dark Adaptation Rate", &autoExposureSystem->darkAdaptRate, 0.01f, 0.001f);
			ImGui::DragFloat("Bright Adaptation Rate", &autoExposureSystem->brightAdaptRate, 0.01f, 0.001f);

			auto graphicsSystem = GraphicsSystem::Instance::get();
			auto readbackBufferView = graphicsSystem->get(readbackBuffer);
			auto size = sizeof(ToneMappingRenderSystem::LuminanceData) +
				AutoExposureRenderSystem::histogramSize * sizeof(uint32);
			auto offset = size * graphicsSystem->getSwapchainIndex();
			readbackBufferView->invalidate(size, offset);
			auto map = readbackBufferView->getMap() + offset;
			auto luminance = (const ToneMappingRenderSystem::LuminanceData*)map;
			auto histogram = (const uint32*)(map + sizeof(ToneMappingRenderSystem::LuminanceData));
			uint32 maxHistogramValue = 0;

			for (uint16 i = 0; i < AutoExposureRenderSystem::histogramSize; i++)
			{
				if (histogram[i] > maxHistogramValue)
					maxHistogramValue = histogram[i];
			}

			for (uint16 i = 0; i < AutoExposureRenderSystem::histogramSize; i++)
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
				AutoExposureRenderSystem::histogramSize, 0, nullptr, 0.0f, 1.0f, { 320.0f, 64.0f });

			if (limitsPipeline)
			{
				auto pipelineView = graphicsSystem->get(limitsPipeline);
				if (!pipelineView->isReady())
					ImGui::TextDisabled("Limits pipeline is loading...");
			}

			auto toneMappingSystem = ToneMappingRenderSystem::Instance::get();

			graphicsSystem->startRecording(CommandBufferType::Frame);
			{
				SET_GPU_DEBUG_LABEL("Readback Auto Exposure Data", Color::transparent);
				Buffer::CopyRegion copyRegion;
				copyRegion.dstOffset = offset;
				copyRegion.size = sizeof(ToneMappingRenderSystem::LuminanceData);
				Buffer::copy(toneMappingSystem->getLuminanceBuffer(), readbackBuffer, copyRegion);

				copyRegion.dstOffset = offset + sizeof(ToneMappingRenderSystem::LuminanceData);
				copyRegion.size = AutoExposureRenderSystem::histogramSize * sizeof(uint32);
				Buffer::copy(autoExposureSystem->getHistogramBuffer(), readbackBuffer, copyRegion);
			}
			graphicsSystem->stopRecording();
		}
		ImGui::End();
	}

	if (visualizeLimits)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		if (!limitsPipeline)
		{
			limitsPipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline(
				"editor/auto-exposure-limits", graphicsSystem->getSwapchainFramebuffer());
		}
		
		auto pipelineView = graphicsSystem->get(limitsPipeline);
		if (pipelineView->isReady())
		{
			if (!limitsDescriptorSet)
			{
				auto uniforms = getLimitsUniforms();
				limitsDescriptorSet = graphicsSystem->createDescriptorSet(limitsPipeline, std::move(uniforms));
				SET_RESOURCE_DEBUG_NAME(limitsDescriptorSet, "descriptorSet.auto-exposure.editor.limits");
			}

			auto autoExposureSystem = AutoExposureRenderSystem::Instance::get();
			auto framebufferView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());

			graphicsSystem->startRecording(CommandBufferType::Frame);
			{
				SET_GPU_DEBUG_LABEL("Auto Exposure Limits", Color::transparent);
				framebufferView->beginRenderPass(float4(0.0f));
				pipelineView->bind();
				pipelineView->setViewportScissor();
				pipelineView->bindDescriptorSet(limitsDescriptorSet);
				auto pushConstants = pipelineView->getPushConstants<PushConstants>();
				pushConstants->minLum = std::exp2(autoExposureSystem->minLogLum);
				pushConstants->maxLum = std::exp2(autoExposureSystem->maxLogLum);
				pipelineView->pushConstants();
				pipelineView->drawFullscreen();
				framebufferView->endRenderPass();
			}
			graphicsSystem->stopRecording();
		}
	}
}

//**********************************************************************************************************************
void AutoExposureRenderEditorSystem::swapchainRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	const auto& swapchainChanges = graphicsSystem->getSwapchainChanges();

	if (swapchainChanges.bufferCount && readbackBuffer)
	{
		graphicsSystem->destroy(readbackBuffer);
		readbackBuffer = createReadbackBuffer();
	}
}
void AutoExposureRenderEditorSystem::gBufferRecreate()
{
	if (limitsDescriptorSet)
	{
		auto limitsDescriptorSetView = GraphicsSystem::Instance::get()->get(limitsDescriptorSet);
		auto uniforms = getLimitsUniforms();
		limitsDescriptorSetView->recreate(std::move(uniforms));
	}
}

void AutoExposureRenderEditorSystem::editorBarTool()
{
	if (ImGui::MenuItem("Automatic Exposure (AE)"))
		showWindow = true;
}
#endif