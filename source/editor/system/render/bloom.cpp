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

#include "garden/editor/system/render/bloom.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/bloom.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/settings.hpp"

using namespace garden;

static DescriptorSet::Uniforms getThresholdUniforms()
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto hdrFramebufferView = GraphicsSystem::Instance::get()->get(deferredSystem->getHdrFramebuffer());
	return { { "hdrBuffer", DescriptorSet::Uniform(hdrFramebufferView->getColorAttachments()[0].imageView) } };
}

//**********************************************************************************************************************
BloomRenderEditorSystem::BloomRenderEditorSystem()
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", BloomRenderEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", BloomRenderEditorSystem::deinit);
}
BloomRenderEditorSystem::~BloomRenderEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", BloomRenderEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", BloomRenderEditorSystem::deinit);
	}
}

void BloomRenderEditorSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("PreUiRender", BloomRenderEditorSystem::preUiRender);
	ECSM_SUBSCRIBE_TO_EVENT("UiRender", BloomRenderEditorSystem::uiRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", BloomRenderEditorSystem::gBufferRecreate);
	ECSM_SUBSCRIBE_TO_EVENT("EditorBarToolPP", BloomRenderEditorSystem::editorBarToolPP);
}
void BloomRenderEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(thresholdDescriptorSet);
		graphicsSystem->destroy(thresholdPipeline);

		ECSM_UNSUBSCRIBE_FROM_EVENT("PreUiRender", BloomRenderEditorSystem::preUiRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("UiRender", BloomRenderEditorSystem::uiRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", BloomRenderEditorSystem::gBufferRecreate);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorBarToolPP", BloomRenderEditorSystem::editorBarToolPP);
	}
}

//**********************************************************************************************************************
void BloomRenderEditorSystem::preUiRender()
{
	if (!showWindow)
		return;

	if (ImGui::Begin("Bloom (Light Glow)", &showWindow, ImGuiWindowFlags_AlwaysAutoResize))
	{
		auto bloomSystem = BloomRenderSystem::Instance::get();
		auto useThreshold = bloomSystem->getUseThreshold();
		auto useAntiFlickering = bloomSystem->getUseAntiFlickering();

		if (ImGui::Checkbox("Enabled", &bloomSystem->isEnabled))
		{
			auto settingsSystem = SettingsSystem::Instance::tryGet();
			if (settingsSystem)
				settingsSystem->setBool("bloom.isEnabled", bloomSystem->isEnabled);
		}

		ImGui::SliderFloat("Intensity", &bloomSystem->intensity, 0.0f, 1.0f);

		if (ImGui::Checkbox("Use Anti Flickering", &useAntiFlickering) ||
			ImGui::Checkbox("Use Threshold", &useThreshold))
		{
			bloomSystem->setConsts(useThreshold, useAntiFlickering);
		}
			
		ImGui::DragFloat("Threshold", &bloomSystem->threshold, 0.01f, 0.0f, FLT_MAX);
		ImGui::Spacing();

		ImGui::Checkbox("Visualize Threshold", &visualizeThreshold);
		if (ImGui::BeginItemTooltip())
		{
			ImGui::Text("Red = less than a threshold");
			ImGui::EndTooltip();
		}

		if (visualizeThreshold)
		{
			if (!thresholdPipeline)
			{
				auto deferredSystem = DeferredRenderSystem::Instance::get();
				thresholdPipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline(
					"editor/bloom-threshold", deferredSystem->getUiFramebuffer());
			}

			auto graphicsSystem = GraphicsSystem::Instance::get();
			auto pipelineView = graphicsSystem->get(thresholdPipeline);
			if (pipelineView->isReady())
			{
				if (!thresholdDescriptorSet)
				{
					auto uniforms = getThresholdUniforms();
					thresholdDescriptorSet = graphicsSystem->createDescriptorSet(thresholdPipeline, std::move(uniforms));
					SET_RESOURCE_DEBUG_NAME(thresholdDescriptorSet, "descriptorSet.editor.bloom.threshold");
				}
			}
			else
			{
				ImGui::TextDisabled("Threshold pipeline is loading...");
			}
		}
	}
	ImGui::End();
}
void BloomRenderEditorSystem::uiRender()
{
	if (!visualizeThreshold)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(thresholdPipeline);
	if (!pipelineView->isReady())
		return;

	auto bloomSystem = BloomRenderSystem::Instance::get();
	auto pushConstants = pipelineView->getPushConstants<PushConstants>();
	pushConstants->threshold = bloomSystem->threshold;

	SET_GPU_DEBUG_LABEL("Bloom Threshold", Color::transparent);
	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->bindDescriptorSet(thresholdDescriptorSet);
	pipelineView->pushConstants();
	pipelineView->drawFullscreen();
}

//**********************************************************************************************************************
void BloomRenderEditorSystem::gBufferRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	const auto& swapchainChanges = graphicsSystem->getSwapchainChanges();

	if (swapchainChanges.framebufferSize && thresholdDescriptorSet)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(thresholdDescriptorSet);
		auto uniforms = getThresholdUniforms();
		thresholdDescriptorSet = graphicsSystem->createDescriptorSet(thresholdPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(thresholdDescriptorSet, "descriptorSet.editor.bloom.threshold");
	}
}

void BloomRenderEditorSystem::editorBarToolPP()
{
	if (ImGui::MenuItem("Bloom (Light Glow)"))
		showWindow = true;
}
#endif