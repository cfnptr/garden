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

#include "garden/system/graphics/editor/bloom.hpp"

#if GARDEN_EDITOR
#include "garden/system/graphics/editor.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/settings.hpp"

using namespace garden;

namespace
{
	struct PushConstants final
	{
		float threshold;
	};
}

//--------------------------------------------------------------------------------------------------
static map<string, DescriptorSet::Uniform> getThresholdUniforms(
	GraphicsSystem* graphicsSystem, DeferredRenderSystem* deferredSystem)
{
	auto hdrFramebufferView = graphicsSystem->get(deferredSystem->getHdrFramebuffer());
	map<string, DescriptorSet::Uniform> uniforms =
	{ 
		{ "hdrBuffer", DescriptorSet::Uniform(
			hdrFramebufferView->getColorAttachments()[0].imageView) },
	};
	return uniforms;
}

//--------------------------------------------------------------------------------------------------
BloomEditor::BloomEditor(BloomRenderSystem* system)
{
	auto manager = system->getManager();
	auto editorSystem = manager->get<EditorRenderSystem>();
	editorSystem->registerBarTool([this]() { onBarTool(); });
	this->system = system;
}

//--------------------------------------------------------------------------------------------------
void BloomEditor::render()
{
	if (!showWindow) return;

	if (ImGui::Begin("Light Bloom (Glow)", &showWindow,
		ImGuiWindowFlags_AlwaysAutoResize))
	{
		auto useThreshold = system->useThreshold;
		auto useAntiFlickering = system->useAntiFlickering;

		if (ImGui::Checkbox("Enabled", &system->isEnabled))
		{
			auto settingsSystem = system->getManager()->tryGet<SettingsSystem>();
			if (settingsSystem) settingsSystem->setBool("useBloom", system->isEnabled);
		}

		ImGui::SliderFloat("Intensity", &system->intensity, 0.0f, 1.0f);

		if (ImGui::Checkbox("Use Anti Flickering", &useAntiFlickering) ||
			ImGui::Checkbox("Use Threshold", &useThreshold))
		{
			system->setConsts(useThreshold, useAntiFlickering);
		}

		ImGui::DragFloat("Threshold", &system->threshold, 0.01f, 0.0f, FLT_MAX);
		
		ImGui::Checkbox("Visualize Threshold", &visualizeThreshold);
		if (ImGui::BeginItemTooltip())
		{
			ImGui::Text("Red = less than a threshold");
			ImGui::EndTooltip();
		}

		if (thresholdPipeline)
		{
			auto graphicsSystem = system->getGraphicsSystem();
			auto pipelineView = graphicsSystem->get(thresholdPipeline);
			if (!pipelineView->isReady())
				ImGui::TextDisabled("Threshold pipeline is loading...");
		}
	}
	ImGui::End();

	if (visualizeThreshold)
	{
		auto graphicsSystem = system->getGraphicsSystem();
		if (!thresholdPipeline)
		{
			thresholdPipeline = ResourceSystem::getInstance()->loadGraphicsPipeline(
				"editor/bloom-threshold", graphicsSystem->getSwapchainFramebuffer());
		}
		
		auto pipelineView = graphicsSystem->get(thresholdPipeline);
		if (pipelineView->isReady())
		{
			if (!thresholdDescriptorSet)
			{
				auto uniforms = getThresholdUniforms(
					graphicsSystem, system->getDeferredSystem());
				thresholdDescriptorSet = graphicsSystem->createDescriptorSet(
					thresholdPipeline, std::move(uniforms));
				SET_RESOURCE_DEBUG_NAME(graphicsSystem, thresholdDescriptorSet,
					"descriptorSet.bloom.editor.threshold");
			}

			auto framebufferView = graphicsSystem->get(
				graphicsSystem->getSwapchainFramebuffer());
			graphicsSystem->startRecording(CommandBufferType::Frame);

			{
				SET_GPU_DEBUG_LABEL("Bloom Threshold", Color::transparent);
				framebufferView->beginRenderPass(float4(0.0f));
				pipelineView->bind();
				pipelineView->setViewportScissor(float4(float2(0),
					graphicsSystem->getFramebufferSize()));
				pipelineView->bindDescriptorSet(thresholdDescriptorSet);
				auto pushConstants = pipelineView->getPushConstants<PushConstants>();
				pushConstants->threshold = system->threshold;
				pipelineView->pushConstants();
				pipelineView->drawFullscreen();
				framebufferView->endRenderPass();
			}

			graphicsSystem->stopRecording();
		}
	}
}

void BloomEditor::onBarTool()
{
	if (ImGui::MenuItem("Light Bloom (Glow)")) showWindow = true;
}
#endif