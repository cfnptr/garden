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

#include "garden/system/graphics/editor/auto-exposure.hpp"

#if GARDEN_EDITOR
#include "garden/system/graphics/editor.hpp"
#include "garden/system/graphics/tone-mapping.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

namespace
{
	struct PushConstants final
	{
		float minLum;
		float maxLum;
	};
}

//--------------------------------------------------------------------------------------------------
static ID<Buffer> createReadbackBuffer(GraphicsSystem* graphicsSystem)
{
	auto swapchainSize = graphicsSystem->getSwapchainSize();
	auto size = (sizeof(ToneMappingRenderSystem::Luminance) +
		AE_HISTOGRAM_SIZE * sizeof(uint32)) * swapchainSize;
	auto buffer = graphicsSystem->createBuffer(
		Buffer::Bind::TransferDst, Buffer::Access::RandomReadWrite, size,
		Buffer::Usage::PreferGPU, Buffer::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, buffer,
		"buffer.auto-exposure.editor.readback");
	return buffer;
}

//--------------------------------------------------------------------------------------------------
static map<string, DescriptorSet::Uniform> getLimitsUniforms(
	Manager* manager, GraphicsSystem* graphicsSystem)
{
	auto deferredSystem = manager->get<DeferredRenderSystem>();
	auto toneMappingSystem = manager->get<ToneMappingRenderSystem>();
	auto hdrFramebufferView = graphicsSystem->get(deferredSystem->getHdrFramebuffer());
				
	map<string, DescriptorSet::Uniform> uniforms =
	{ 
		{ "hdrBuffer", DescriptorSet::Uniform(
			hdrFramebufferView->getColorAttachments()[0].imageView) },
		{ "luminance", DescriptorSet::Uniform(toneMappingSystem->getLuminanceBuffer()) }
	};
	return uniforms;
}

//--------------------------------------------------------------------------------------------------
AutoExposureEditor::AutoExposureEditor(AutoExposureRenderSystem* system)
{
	auto manager = system->getManager();
	auto editorSystem = manager->get<EditorRenderSystem>();
	editorSystem->registerBarTool([this]() { onBarTool(); });
	this->system = system;
}
AutoExposureEditor::~AutoExposureEditor()
{
	delete[] histogramSamples;
}

//--------------------------------------------------------------------------------------------------
void AutoExposureEditor::render()
{
	if (!showWindow) return;

	if (ImGui::Begin("Automatic Exposure", &showWindow,
		ImGuiWindowFlags_AlwaysAutoResize))
	{
		auto manager = system->getManager();
		auto graphicsSystem = system->getGraphicsSystem();

		if (!readbackBuffer)
		{
			readbackBuffer = createReadbackBuffer(graphicsSystem);
			histogramSamples = new float[AE_HISTOGRAM_SIZE]();
		}

		ImGui::Checkbox("Enabled", &system->isEnabled);
		ImGui::DragFloat("Min Log Luminance", &system->minLogLum, 0.1f);
		ImGui::DragFloat("Max Log Luminance", &system->maxLogLum, 0.1f);

		ImGui::DragFloat("Dark Adaptation Rate",
			&system->darkAdaptRate, 0.01f, 0.001f);
		ImGui::DragFloat("Bright Adaptation Rate",
			&system->brightAdaptRate, 0.01f, 0.001f);
			
		auto readbackBufferView = graphicsSystem->get(readbackBuffer);
		auto size = sizeof(ToneMappingRenderSystem::Luminance) +
			AE_HISTOGRAM_SIZE * sizeof(uint32);
		auto offset = size * graphicsSystem->getSwapchainIndex();
		readbackBufferView->invalidate(size, offset);
		auto map = readbackBufferView->getMap() + offset;
		auto luminance = (const ToneMappingRenderSystem::Luminance*)map;
		auto histogram = (const uint32*)(map +
			sizeof(ToneMappingRenderSystem::Luminance));
		auto maxHistoValue = 0;

		for (int i = 0; i < AE_HISTOGRAM_SIZE; i++)
		{
			if (histogram[i] > maxHistoValue)
				maxHistoValue = histogram[i];
		}

		for (int i = 0; i < AE_HISTOGRAM_SIZE; i++)
			histogramSamples[i] = (float)histogram[i] / maxHistoValue;

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
				system->minLogLum, system->maxLogLum);
			ImGui::EndTooltip();
		}

		ImGui::PlotHistogram("", histogramSamples, AE_HISTOGRAM_SIZE,
			0, nullptr, 0.0f, 1.0f, { 320.0f, 64.0f });

		if (limitsPipeline)
		{
			auto pipelineView = graphicsSystem->get(limitsPipeline);
			if (!pipelineView->isReady())
				ImGui::TextDisabled("Limits pipeline is loading...");
		}

		auto toneMappingSystem = manager->get<ToneMappingRenderSystem>();
		Buffer::CopyRegion copyRegion;
		copyRegion.dstOffset = offset;
		copyRegion.size = sizeof(ToneMappingRenderSystem::Luminance);
		Buffer::copy(toneMappingSystem->getLuminanceBuffer(), readbackBuffer, copyRegion);

		copyRegion.dstOffset = offset + sizeof(ToneMappingRenderSystem::Luminance);
		copyRegion.size = AE_HISTOGRAM_SIZE * sizeof(uint32);
		Buffer::copy(system->histogramBuffer, readbackBuffer, copyRegion);
	}
	ImGui::End();

	if (visualizeLimits)
	{
		auto graphicsSystem = system->getGraphicsSystem();
		if (!limitsPipeline)
		{
			limitsPipeline = ResourceSystem::getInstance()->loadGraphicsPipeline(
				"editor/auto-exposure-limits", graphicsSystem->getSwapchainFramebuffer());
		}
		
		auto pipelineView = graphicsSystem->get(limitsPipeline);
		if (pipelineView->isReady())
		{
			if (!limitsDescriptorSet)
			{
				auto uniforms = getLimitsUniforms(system->getManager(), graphicsSystem);
				limitsDescriptorSet = graphicsSystem->createDescriptorSet(
					limitsPipeline, std::move(uniforms));
				SET_RESOURCE_DEBUG_NAME(graphicsSystem, limitsDescriptorSet,
					"descriptorSet.auto-exposure.editor.limits");
			}

			auto framebufferView = graphicsSystem->get(
				graphicsSystem->getSwapchainFramebuffer());

			SET_GPU_DEBUG_LABEL("Auto Exposure Limits", Color::transparent);
			framebufferView->beginRenderPass(float4(0.0f));
			pipelineView->bind();
			pipelineView->setViewportScissor(float4(float2(0),
				graphicsSystem->getFramebufferSize()));
			pipelineView->bindDescriptorSet(limitsDescriptorSet);
			auto pushConstants = pipelineView->getPushConstants<PushConstants>();
			pushConstants->minLum = std::exp2(system->minLogLum);
			pushConstants->maxLum = std::exp2(system->maxLogLum);
			pipelineView->pushConstants();
			pipelineView->drawFullscreen();
			framebufferView->endRenderPass();
		}
	}
}

//--------------------------------------------------------------------------------------------------
void AutoExposureEditor::recreateSwapchain(const IRenderSystem::SwapchainChanges& changes)
{
	auto graphicsSystem = system->getGraphicsSystem();
	if (changes.bufferCount && readbackBuffer)
	{
		graphicsSystem->destroy(readbackBuffer);
		readbackBuffer = createReadbackBuffer(graphicsSystem);
	}

	if (changes.framebufferSize && limitsDescriptorSet)
	{
		auto uniforms = getLimitsUniforms(system->getManager(), graphicsSystem);
		auto limitsDescriptorSetView = graphicsSystem->get(limitsDescriptorSet);
		limitsDescriptorSetView->recreate(std::move(uniforms));
	}
}

void AutoExposureEditor::onBarTool()
{
	if (ImGui::MenuItem("Automatic Exposure")) showWindow = true;
}
#endif