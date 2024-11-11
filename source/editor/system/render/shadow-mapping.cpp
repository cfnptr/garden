//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "garden/editor/system/render/shadow-mapping.hpp"

#if GARDEN_EDITOR
#include "garden/system/resource.hpp"

using namespace garden;

//--------------------------------------------------------------------------------------------------
static map<string, DescriptorSet::Uniform> getCascadesUniforms()
{
	auto deferredSystem = DeferredRenderSystem::getInstance();
	auto gFramebufferView = graphicsSystem->get(deferredSystem->getGFramebuffer());		
	map<string, DescriptorSet::Uniform> uniforms =
	{ 
		{ "depthBuffer", DescriptorSet::Uniform(
			gFramebufferView->getDepthStencilAttachment().imageView) },
	};
	return uniforms;
}

//--------------------------------------------------------------------------------------------------
ShadowMappingEditor::ShadowMappingEditor(ShadowMappingRenderSystem* system)
{
	EditorRenderSystem::getInstance()->registerBarTool([this]() { onBarTool(); });
	this->system = system;
}

//--------------------------------------------------------------------------------------------------
void ShadowMappingEditor::render()
{
	if (!showWindow)
		return;

	if (ImGui::Begin("Cascade Shadow Mapping", &showWindow, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::SliderFloat("Intensity", &system->intensity, 0.0f, 1.0f);
		ImGui::DragFloat("Far Plane", &system->farPlane, 1.0f);

		if (ImGui::DragFloat("Min Bias", &system->minBias, 0.0001f, 0.0f, FLT_MAX, "%.4f"))
		{
			if (system->minBias > system->maxBias)
				system->minBias = system->maxBias;
		}
		if (ImGui::DragFloat("Max Bias", &system->maxBias, 0.0001f, 0.0f, FLT_MAX, "%.4f"))
		{
			if (system->maxBias < system->minBias)
				system->maxBias = system->minBias;
		}

		ImGui::DragFloat("Z-Axis Offset Coefficient", &system->zCoeff, 0.01f);
		ImGui::SliderFloat3("Cascade Split Coefficients",
			&system->splitCoefs, 0.0f, 1.0f);

		ImGui::Checkbox("Visualize Cascades", &visualizeCascades);
		if (ImGui::BeginItemTooltip())
		{
			ImGui::Text("Cascade 1 = green, 2 = yellow, 3 = red, outside = magenta");
			ImGui::EndTooltip();
		}

		ImGui::Text("Cascade distances: %f, %f, %f",
			system->splitCoefs[0] * system->farPlane,
			system->splitCoefs[1] * system->farPlane,
			system->splitCoefs[2] * system->farPlane);

		// TODO: set shadow map size, also set it in the settings

		if (cascadesPipeline)
		{
			auto graphicsSystem = system->getGraphicsSystem();
			auto pipelineView = graphicsSystem->get(cascadesPipeline);
			if (!pipelineView->isReady())
				ImGui::TextDisabled("Cascades pipeline is loading...");
		}
	}
	ImGui::End();

	if (visualizeCascades)
	{
		auto graphicsSystem = system->getGraphicsSystem();
		if (!cascadesPipeline)
		{
			cascadesPipeline = ResourceSystem::getInstance()->loadGraphicsPipeline(
				"editor/shadow-cascades", graphicsSystem->getSwapchainFramebuffer());
		}
		
		auto pipelineView = graphicsSystem->get(cascadesPipeline);
		if (pipelineView->isReady())
		{
			if (!cascadesDescriptorSet)
			{
				auto uniforms = getCascadesUniforms();
				cascadesDescriptorSet = graphicsSystem->createDescriptorSet(
					cascadesPipeline, std::move(uniforms));
				SET_RESOURCE_DEBUG_NAME(cascadesDescriptorSet,
					"descriptorSet.shadow-mapping.editor.cascades");
			}

			auto framebufferView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());
			const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
			auto pushConstants = pipelineView->getPushConstants<PushConstants>();
			pushConstants->farPlanes = float4(cameraConstants.nearPlane / system->farPlanes, 0.0f);

			graphicsSystem->startRecording(CommandBufferType::Frame);
			{
				SET_GPU_DEBUG_LABEL("Shadow Map Cascades", Color::transparent);
				framebufferView->beginRenderPass(float4(0.0f));
				pipelineView->bind();
				pipelineView->setViewportScissor();
				pipelineView->bindDescriptorSet(cascadesDescriptorSet);
				pipelineView->pushConstants();
				pipelineView->drawFullscreen();
				framebufferView->endRenderPass();
			}
			graphicsSystem->stopRecording();
		}
	}
}

void ShadowMappingEditor::onBarTool()
{
	if (ImGui::MenuItem("Cascade Shadow Mapping"))
		showWindow = true;
}
#endif